#include <stdio.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/binary_info.h"
#include "piocamera.pio.h"
#include "iot_sram.pio.h"
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "sccb_if.h"

#define SYS_CLK_KHZ         (192000)// 192000 ~ 264000
#define CAM_BASE_PIN        (1)     // GP1 (camera module needs 11pin)
#define PIN_PWM0            (0)     // GP0 (camera's xclk(24MHz))
#define IOT_DAT_BASE_PIN    (14)    // IoT SRAM's data-pin(D0~D3)
#define IOT_SIG_BASE_PIN    (18)    // IoT SRAM's control pin (nCS, SCLK)

#define DMA_BASE_CH     (0)
#define DMA_CAM_RD_CH   (DMA_BASE_CH)
#define DMA_IOT_RD_CH   (DMA_BASE_CH + 1)

void set_pwm_freq_kHz(uint32_t freq_khz, uint32_t system_clk_khz, uint8_t gpio_num);
void iot_sram_init(PIO pio, uint32_t sm);
void iot_sram_write(PIO pio, uint32_t sm, uint32_t *send_data, uint32_t address, uint32_t length_in_byte);
void *iot_sram_read(PIO pio, uint32_t sm, uint32_t *read_data, uint32_t address, uint32_t length_in_byte);


int main() {
    
    set_sys_clock_khz(SYS_CLK_KHZ, true);
    stdio_init_all();

    PIO pio00 = pio0;
    PIO pio01 = pio1;

    // Initialize write buffer
    uint32_t in_data[1024*60];
    for(uint32_t i=0; i<1024*2; i++) {
        in_data[i]=0x12345678+i;
    }

    // Initialize CAMERA
    set_pwm_freq_kHz(24000, SYS_CLK_KHZ, PIN_PWM0); // XCLK 24MHz -> OV5642,OV2640
    sleep_ms(500);
    sccb_init(DEV_OV5642); // sda,scl=(gp12,gp13). see 'sccb_if.c'
    sleep_ms(3000);
    uint32_t offset = pio_add_program(pio00, &piocamera_program);
    uint32_t sm = pio_claim_unused_sm(pio00, true);
    piocamera_program_init(pio00, sm, offset, CAM_BASE_PIN, 11);// VSYNC,HREF,PCLK,D[2:9] : total 11 pins


    // Initialize IoT SRAM
    uint32_t offset01 = pio_add_program(pio01, &iot_sram_program);
    uint32_t sm01 = pio_claim_unused_sm(pio01, true);
    iot_sram_program_init(pio01, sm01, offset01, IOT_DAT_BASE_PIN, 4, IOT_SIG_BASE_PIN, 2); // : total 6 pins
    iot_sram_init(pio01, sm01);

    // ------------------ Read and Write SRAM --------------------------------
    printf("!srt\r\n");
    iot_sram_write(pio01, sm01, in_data, 0, 1024); //pio, sm, buffer, start_address, length
    iot_sram_write(pio01, sm01, &in_data[1024/4], 1024, 1024); //pio, sm, buffer, start_address, length
    sleep_ms(200);
    // Initialize read_buffer
    uint32_t *dat;
    dat = (uint32_t *)malloc(sizeof(uint32_t) * 1024);
    uint32_t ii=0;
    while(1) {
        dat = (uint32_t *)iot_sram_read(pio01, sm01,(uint32_t *)dat, 0, 2048); //pio, sm, buffer, start_address, length 
        for(uint32_t i = 0 ; i < 2048/4 ; i++) {
            printf("0x%08X\r\n",dat[i]);
        }
        printf("%d------------------\r\n",ii++);
        // sleep_ms(200);
    }
    free(dat);
    while(1);



    // ------------------ Read and Write SRAM:end --------------------------------


    // ------------------ CAMERA READ: withoutDMA --------------------------------
    // for (uint32_t h = 0 ; h < 480 ; h++) {
    //     pio_sm_put_blocking(pio00, sm, 640*2-1); // X: total bytes 
    //     pio_sm_put_blocking(pio00, sm, h); // Y: Count Hsync 
    //     for(uint i = 0; i < 640*2/4; i++) {
    //         in_data[i] = pio_sm_get_blocking(pio00, sm);
    //     }
    //     for(uint i = 0; i < 640*2/4; i++) {
    //         printf("0x%08X\r\n",( in_data[i]));
    //     }
    // }
    // ------------------ CAMERA READ: withDMA   --------------------------------
    pio_sm_set_enabled(pio00, sm, false);
    pio_sm_clear_fifos(pio00, sm);
    pio_sm_restart(pio00, sm);
    
    
    char din=0;


    for (uint32_t h = 0 ; h < 480 ; h+=160) {
        dma_channel_config c = dma_channel_get_default_config(DMA_CAM_RD_CH);    
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, pio_get_dreq(pio00, sm, false));

        dma_channel_configure(DMA_CAM_RD_CH, &c,
            in_data,                // Destination pointer
            &pio00->rxf[sm],        // Source pointer
            640*160*2/4,            // Number of transfers
            true                    // Start immediately
        );

        // pio_sm_exec(pio, sm, pio_encode_wait_gpio(trigger_level, trigger_pin));
        pio_sm_set_enabled(pio00, sm, true);

        pio_sm_put_blocking(pio00, sm, 640*160*2-1); // X: total bytes 
        pio_sm_put_blocking(pio00, sm, h); // Y: Count Hsync 
        
        dma_channel_wait_for_finish_blocking(DMA_CAM_RD_CH);
    
        for(uint i = 0; i < 640*160*2/4; i++) {
            printf("0x%08X\r\n",( in_data[i]));
        }
    }
    
    // ------------------ CAMERA READ: end --------------------------------

    while(1);
}

void set_pwm_freq_kHz(uint32_t freq_khz, uint32_t system_clk_khz, uint8_t gpio_num) {

    uint32_t pwm0_slice_num;
    uint32_t period;
    static pwm_config pwm0_slice_config;

    period = system_clk_khz / freq_khz - 1;
    if(period < 2) period = 2;

  
    gpio_set_function( gpio_num, GPIO_FUNC_PWM );
    pwm0_slice_num = pwm_gpio_to_slice_num( gpio_num );

    
    // config
    pwm0_slice_config = pwm_get_default_config();
    pwm_config_set_wrap( &pwm0_slice_config, period );
    
    // set clk div
    pwm_config_set_clkdiv( &pwm0_slice_config, 1 );

    // set PWM start
    pwm_init( pwm0_slice_num, &pwm0_slice_config, true );
    pwm_set_gpio_level( gpio_num, ( pwm0_slice_config.top * 0.50 ) ); // duty:50%
    
}

void iot_sram_init(PIO pio, uint32_t sm) {
    // ----------send reset
    pio_sm_put_blocking(pio, sm, 8-1);      // x=8
    pio_sm_put_blocking(pio, sm, 0);        // y=0
    {
        pio_sm_put_blocking(pio, sm, 0xeffeeffe); 
    }
    pio_sm_put_blocking(pio, sm, 8-1);      // x=8
    pio_sm_put_blocking(pio, sm, 0);        // y=0    
    {
        pio_sm_put_blocking(pio, sm, 0xfeeffeef);  
    }
    sleep_ms(1);
    // ----------send qpi enable mode
    pio_sm_put_blocking(pio, sm, 8-1);      // x=8
    pio_sm_put_blocking(pio, sm, 0);        // y=0    
    {
        pio_sm_put_blocking(pio, sm, 0xeeffefef);  
    }
    sleep_ms(1);
}

void iot_sram_write(PIO pio, uint32_t sm, uint32_t *send_data, uint32_t address, uint32_t length_in_byte) {
    // ----------send data
    pio_sm_put_blocking(pio, sm, 8+(length_in_byte*2)-1);      // x=8
    pio_sm_put_blocking(pio, sm, 0);                // y=0    
    {
        pio_sm_put_blocking(pio, sm, 0x38000000 | address);
        for(uint32_t i = 0; i < length_in_byte/4; i++) {
            pio_sm_put_blocking(pio, sm,(uint32_t) *send_data);
            send_data++;
        }  
    }   
}

void *iot_sram_read(PIO pio, uint32_t sm, uint32_t *read_data, uint32_t address, uint32_t length_in_byte) {
    // without DMA
    // uint32_t *b;
    // b = read_data;
    // pio_sm_put_blocking(pio, sm, 8-1);         // comm + addr
    // pio_sm_put_blocking(pio, sm, (length_in_byte * 2)-1); // y=512byte + waitcycle    
    // {
    //     pio_sm_put_blocking(pio, sm, 0xEB000000 | address);
    //     for(uint32_t i = 0; i < length_in_byte/4; i++) {
    //         *b = pio_sm_get_blocking(pio, sm);
    //         b++;
    //     }  
    // }
    // return (void *)read_data;

    // with DMA
    uint32_t *b;
    b = read_data;
    dma_channel_config c = dma_channel_get_default_config(DMA_IOT_RD_CH);    
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, false));

    dma_channel_configure(DMA_IOT_RD_CH, &c,
        b,                  // Destination pointer
        &pio->rxf[sm],      // Source pointer
        length_in_byte/4,   // Number of transfers
        true                // Start immediately
    );

    pio_sm_set_enabled(pio, sm, true);
    pio_sm_put_blocking(pio, sm, 8-1);                      // x counter: comm + addr
    pio_sm_put_blocking(pio, sm, (length_in_byte * 2)-1);   // y counter: up to 512byte    

    pio_sm_put_blocking(pio, sm, 0xEB000000 | address);     // send write command + address
    
    dma_channel_wait_for_finish_blocking(DMA_IOT_RD_CH);
    
    return (void *)read_data;
}
