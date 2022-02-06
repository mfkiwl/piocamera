cmake_minimum_required(VERSION 3.13)

include(pico_sdk_import.cmake)

project(piocamera C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

add_executable(piocamera
    piocamera.c
    sccb_if.c
)

pico_generate_pio_header(piocamera ${CMAKE_CURRENT_LIST_DIR}/piocamera.pio)
pico_generate_pio_header(piocamera ${CMAKE_CURRENT_LIST_DIR}/iot_sram.pio)

pico_enable_stdio_usb(piocamera 1)
#pico_enable_stdio_uart(piocamera 1)

pico_add_extra_outputs(piocamera)

target_link_libraries(piocamera PRIVATE
        pico_malloc 
        pico_mem_ops
        pico_stdlib
        hardware_pio
        hardware_i2c
        hardware_pwm
        hardware_dma
        )

