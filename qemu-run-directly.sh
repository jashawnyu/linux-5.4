#!/bin/bash

#qemu-system-aarch64 -machine virt -cpu cortex-a57 -nographic -smp 2 -m 1024 -kernel arch/arm64/boot/Image --append "loglevel=8" --fsdev local,id=share9p,path=$PWD/sharefs,security_model=none -device virtio-9p-device,fsdev=share9p,mount_tag=net9p 
sudo qemu-system-aarch64 -machine virt -cpu cortex-a57 -nographic -smp 2 -m 1024 -kernel arch/arm64/boot/Image --append "rootwait root=/dev/vda rw console=ttyAMA0 init=/lib/systemd/systemd loglevel=8" --fsdev local,id=share9p,path=$PWD/sharefs,security_model=none -device virtio-9p-device,fsdev=share9p,mount_tag=net9p \
-drive file=ubuntu-base.ext4,if=none,format=raw,id=hd0 \
-device virtio-blk-device,drive=hd0
 # -netdev type=user,id=eth0 -device virtio-net-device,netdev=eth0 
