#!/bin/bash

# make  ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
# scripts/kconfig/merge_config.sh -m .config my.config
bear -- make -j8 ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- 
