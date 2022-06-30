/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_STACK_POINTER_H
#define __ASM_STACK_POINTER_H

/*
 * how to get the current stack pointer from C
 */
////表示访问current_stack_pointer 直接使用sp寄存器里的值
register unsigned long current_stack_pointer asm ("sp");

#endif /* __ASM_STACK_POINTER_H */
