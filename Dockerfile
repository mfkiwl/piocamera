FROM debian:buster-slim 
#FROM ubuntu:18.04
#RUN sed -i 's@archive.ubuntu.com@ftp.jaist.ac.jp/pub/Linux@g' /etc/apt/sources.list
RUN apt update && apt -y install --no-install-recommends sudo make libtbb2 wget coreutils udev curl time tar nano \
    && apt -y install git cmake libstdc++-arm-none-eabi-newlib gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential python3 \
    && apt clean && rm -rf /var/lib/apt/lists/*

WORKDIR /root
RUN git clone -b master https://github.com/raspberrypi/pico-sdk.git
SHELL ["/bin/bash", "-c"]
WORKDIR /root/pico-sdk
RUN git submodule update --init
# if you want to clone pico-examples, un-comment below 2 lines.
WORKDIR /root
RUN git clone -b master https://github.com/raspberrypi/pico-examples.git
WORKDIR /root
RUN git clone https://github.com/panda5mt/piocamera.git
WORKDIR /root/piocamera
RUN echo -e '#!/bin/bash\nNOW=`date "+%Y%m%d_%H%M%S"`\ngit add .\n# git commit -m "automatically uploaded at "$NOW\ngit commit -m "Automatically uploaded"\ngit push origin main' > add_git.sh
RUN chmod +x add_git.sh
RUN cp ../pico-sdk/external/pico_sdk_import.cmake .
RUN echo -e '#!/bin/bash\nmkdir -p build\ncd build\nexport PICO_SDK_PATH=../../pico-sdk\ncmake ..\nmake' > pico_build.sh 
RUN chmod +x pico_build.sh
RUN git config --global core.autocrlf false

