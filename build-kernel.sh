#!/bin/bash

#make  ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
#scripts/kconfig/merge_config.sh -m .config my.config
unset https_proxy
unset http_proxy
#bear -- make -j8 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- 
make -j8 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- 
./gen_compile_commands.py
sed -i 's/-mabi=lp64//g' compile_commands.json

