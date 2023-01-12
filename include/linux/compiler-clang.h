/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_COMPILER_TYPES_H
#error "Please don't include <linux/compiler-clang.h> directly, include <linux/compiler.h> instead."
#endif

/* Compiler specific definitions for Clang compiler */

#define uninitialized_var(x) x = *(&(x))

/* same as gcc, this was present in clang-2.6 so we can assume it works
 * with any version that can compile the kernel
 */
#define __UNIQUE_ID(prefix) __PASTE(__PASTE(__UNIQUE_ID_, prefix), __COUNTER__)

/* all clang versions usable with the kernel support KASAN ABI version 5 */
#define KASAN_ABI_VERSION 5

#if __has_feature(address_sanitizer) || __has_feature(hwaddress_sanitizer)
/* emulate gcc's __SANITIZE_ADDRESS__ flag */
#define __SANITIZE_ADDRESS__
#define __no_sanitize_address \
		__attribute__((no_sanitize("address", "hwaddress")))
#else
#define __no_sanitize_address
#endif

/*
 * Not all versions of clang implement the the type-generic versions
 * of the builtin overflow checkers. Fortunately, clang implements
 * __has_builtin allowing us to avoid awkward version
 * checks. Unfortunately, we don't know which version of gcc clang
 * pretends to be, so the macro may or may not be defined.
 */
#if __has_builtin(__builtin_mul_overflow) && \
    __has_builtin(__builtin_add_overflow) && \
    __has_builtin(__builtin_sub_overflow)
#define COMPILER_HAS_GENERIC_BUILTIN_OVERFLOW 1
#endif

/* The following are for compatibility with GCC, from compiler-gcc.h,
 * and may be redefined here because they should not be shared with other
 * compilers, like ICC.
 */
/* 破坏列表（clobber list） “memory”告诉编译器： 内存中变量的值可能变化，
不要继续使用加载到寄存器中的值， 应该重新从内存中加载变量的值 */
#define barrier() __asm__ __volatile__("" : : : "memory") // barrier() 编译屏障是为了防止编译器对代码优化时,改变代码的先后顺序
