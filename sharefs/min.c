#include <stdio.h>

typedef unsigned long long u64;
typedef long long s64;

static inline u64 min_vruntime(u64 min_vruntime, u64 vruntime) //(0xfffffff5, 5)
{
	s64 delta = (s64)(vruntime - min_vruntime);
	printf("delta = 0x%llx\n", delta);
	if (delta < 0)
		min_vruntime = vruntime;

	return min_vruntime;
}

int main(){
	/* u64 min_vruntime_t = ~0UL - 10;	 */
	s64 min_vruntime_t = 0x8000000000000001;	
	u64 vruntime = 0;
	/* u64 vruntime = ~0UL - 11; */

	u64 min = min_vruntime((u64)min_vruntime_t, vruntime);
	printf("min = 0x%llx\n", min);
  return 0;
}


