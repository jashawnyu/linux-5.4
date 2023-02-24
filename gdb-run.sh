#!/bin/sh

aarch64-linux-gnu-gdb  vmlinux -ex 'target remote :1234' -ex 'add-symbol-file vmlinux 0x40081000 -s .head.text 0x40080000 -s .rodata 0x40910000' 
