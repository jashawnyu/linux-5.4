#!/bin/bash

if [ ! -e vmlinux ]; then 
#make  ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
#sed -i '26s/^/extern /' scripts/dtc/dtc-lexer.l
scripts/kconfig/merge_config.sh -m .config my.config
fi
unset https_proxy
unset http_proxy
#bear -- make -j8 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- 
make -j$(nproc) ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-
[ $? -eq 0 ] && ./gen_compile_commands.py && sed -i 's/-mabi=lp64//g' compile_commands.json

