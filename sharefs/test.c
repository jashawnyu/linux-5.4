#include <stdio.h>
#include <stdint.h>

int main() {
    uint64_t big_val = 0x123456789ABCDEF0ULL;
    uint32_t small_val = (uint32_t)big_val;

    printf("u64 value: 0x%016lx\n", big_val);
    printf("u32 value: 0x%08x\n", small_val);

    return 0;
}

