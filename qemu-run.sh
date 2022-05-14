#!/bin/bash

qemu-system-aarch64 -S -s -machine virt -cpu cortex-a57 -nographic -smp 2 -m 1024 -kernel arch/arm64/boot/Image --append "loglevel=8" --fsdev local,id=share9p,path=$PWD/sharefs,security_model=none -device virtio-9p-device,fsdev=share9p,mount_tag=net9p
