#include <stdio.h>

#define current get_current
/* #define __always_inline __attribute__((__always_inline__)) inline */

static __always_inline  struct task_struct *get_current()
{
  unsigned long sp_el0;
  asm("mrs %0, sp_el0" : "=r" (sp_el0));
  return (struct task_struct*)sp_el0;
}

int main(){
  printf("%s result: %p\n", __func__, get_current());
  return 0;
}
