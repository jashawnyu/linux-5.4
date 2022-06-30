// SPDX-License-Identifier: GPL-2.0-only
/*
 * Based on arch/arm/mm/context.c
 *
 * Copyright (C) 2002-2003 Deep Blue Solutions Ltd, all rights reserved.
 * Copyright (C) 2012 ARM Ltd.
 */

#include <linux/bitops.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mm.h>

#include <asm/cpufeature.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/tlbflush.h>

//保存ASID长度
static u32 asid_bits;
static DEFINE_RAW_SPINLOCK(cpu_asid_lock);

//全局变量asid_generation的高56位保存全局ASID版本号
static atomic64_t asid_generation;
//记录哪些ASID被分配
static unsigned long *asid_map;

//保存处理器正在使用的ASID
static DEFINE_PER_CPU(atomic64_t, active_asids);
static DEFINE_PER_CPU(u64, reserved_asids);
static cpumask_t tlb_flush_pending;

#define ASID_MASK		(~GENMASK(asid_bits - 1, 0))
#define ASID_FIRST_VERSION	(1UL << asid_bits)

#ifdef CONFIG_UNMAP_KERNEL_AT_EL0
#define NUM_USER_ASIDS		(ASID_FIRST_VERSION >> 1)
#define asid2idx(asid)		(((asid) & ~ASID_MASK) >> 1)
#define idx2asid(idx)		(((idx) << 1) & ~ASID_MASK)
#else
#define NUM_USER_ASIDS		(ASID_FIRST_VERSION)
#define asid2idx(asid)		((asid) & ~ASID_MASK)
#define idx2asid(idx)		asid2idx(idx)
#endif

/* Get the ASIDBits supported by the current CPU */
static u32 get_cpu_asid_bits(void)
{
	u32 asid;
	int fld = cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64MMFR0_EL1),
						ID_AA64MMFR0_ASID_SHIFT);

	switch (fld) {
	default:
		pr_warn("CPU%d: Unknown ASID size (%d); assuming 8-bit\n",
					smp_processor_id(),  fld);
		/* Fallthrough */
	case 0:
		asid = 8;
		break;
	case 2:
		asid = 16;
	}

	return asid;
}

/* Check if the current cpu's ASIDBits is compatible with asid_bits */
void verify_cpu_asid_bits(void)
{
	u32 asid = get_cpu_asid_bits();

	if (asid < asid_bits) {
		/*
		 * We cannot decrease the ASID size at runtime, so panic if we support
		 * fewer ASID bits than the boot CPU.
		 */
		pr_crit("CPU%d: smaller ASID size(%u) than boot CPU (%u)\n",
				smp_processor_id(), asid, asid_bits);
		cpu_panic_kernel();
	}
}

static void flush_context(void)
{
	int i;
	u64 asid;

	/* Update the list of reserved ASIDs and the ASID bitmap. */
  //把ASID位图清零
	bitmap_clear(asid_map, 0, NUM_USER_ASIDS);

  //把每个处理器的active_asids设置为0，active_asids为0具有特殊含义，说明全局ASID版本号变化，ASID回绕。然后把每个处理器正在执行的进程的ASID设置为保留ASID，为保留ASID在ASID位图中设置已分配的标志
	for_each_possible_cpu(i) {
		asid = atomic64_xchg_relaxed(&per_cpu(active_asids, i), 0);
		/*
		 * If this CPU has already been through a
		 * rollover, but hasn't run another task in
		 * the meantime, we must preserve its reserved
		 * ASID, as this is the only trace we have of
		 * the process it is still running.
		 */
		if (asid == 0)
			asid = per_cpu(reserved_asids, i);
		__set_bit(asid2idx(asid), asid_map);
		per_cpu(reserved_asids, i) = asid;
	}

	/*
	 * Queue a TLB invalidation for each CPU to perform on next
	 * context-switch
	 */
  //所有处理器需要清空页表缓存，在位图tlb_flush_pending中设置所有处理器对应的位
	cpumask_setall(&tlb_flush_pending);
}

static bool check_update_reserved_asid(u64 asid, u64 newasid)
{
	int cpu;
	bool hit = false;

	/*
	 * Iterate over the set of reserved ASIDs looking for a match.
	 * If we find one, then we can update our mm to use newasid
	 * (i.e. the same ASID in the current generation) but we can't
	 * exit the loop early, since we need to ensure that all copies
	 * of the old ASID are updated to reflect the mm. Failure to do
	 * so could result in us missing the reserved ASID in a future
	 * generation.
	 */
	for_each_possible_cpu(cpu) {
		if (per_cpu(reserved_asids, cpu) == asid) {
			hit = true;
			per_cpu(reserved_asids, cpu) = newasid;
		}
	}

	return hit;
}

static u64 new_context(struct mm_struct *mm)
{
	static u32 cur_idx = 1;
	u64 asid = atomic64_read(&mm->context.id);
	u64 generation = atomic64_read(&asid_generation);

	if (asid != 0) {
		u64 newasid = generation | (asid & ~ASID_MASK);

		/*
		 * If our current ASID was active during a rollover, we
		 * can continue to use it and this was just a false alarm.
		 */
    //如果进程已经有ASID，并且进程的ASID是保留ASID，那么继续使用原来的ASID，只需更新ASID版本号
		if (check_update_reserved_asid(asid, newasid))
			return newasid;

		/*
		 * We had a valid ASID in a previous life, so try to re-use
		 * it if possible.
		 */
    //如果进程已经有ASID，并且ASID在位图中是空闲的，那么继续使用原来的ASID，只需更新ASID版本号
		if (!__test_and_set_bit(asid2idx(asid), asid_map))
			return newasid;
	}

	/*
	 * Allocate a free ASID. If we can't find one, take a note of the
	 * currently active ASIDs and mark the TLBs as requiring flushes.  We
	 * always count from ASID #2 (index 1), as we use ASID #0 when setting
	 * a reserved TTBR0 for the init_mm and we allocate ASIDs in even/odd
	 * pairs.
	 */
  //从上一次分配的ASID开始分配ASID，如果存在空闲的ASID，那么分配给进程，然后跳转到第28行的标号set_asid去设置ASID位图
	asid = find_next_zero_bit(asid_map, NUM_USER_ASIDS, cur_idx);
	if (asid != NUM_USER_ASIDS)
		goto set_asid;

	/* We're out of ASIDs, so increment the global generation count */
  //如果ASID已经分配完，那么把全局ASID版本号加1
	generation = atomic64_add_return_relaxed(ASID_FIRST_VERSION,
						 &asid_generation);
  //调用函数flush_context重新初始化ASID分配状态
	flush_context();

	/* We have more ASIDs than CPUs, so this will always succeed */
  //从1开始分配ASID
	asid = find_next_zero_bit(asid_map, NUM_USER_ASIDS, 1);

set_asid:
  //为刚分配的ASID在位图中设置已分配的标志
	__set_bit(asid, asid_map);
  //使用静态变量cur_idx记录刚分配的ASID，下次分配ASID时从这次分配的ASID开始查找
	cur_idx = asid;
  //返回ASID和版本号
	return idx2asid(asid) | generation;
}
//检查是否需要给进程重新分配ASID
void check_and_switch_context(struct mm_struct *mm, unsigned int cpu)
{
	unsigned long flags;
	u64 asid, old_active_asid;

	if (system_supports_cnp()) //1
		cpu_set_reserved_ttbr0();

  //Address Space Identifier
	asid = atomic64_read(&mm->context.id);

	/*
	 * The memory ordering here is subtle.
	 * If our active_asids is non-zero and the ASID matches the current
	 * generation, then we update the active_asids entry with a relaxed
	 * cmpxchg. Racing with a concurrent rollover means that either:
	 *
	 * - We get a zero back from the cmpxchg and end up waiting on the
	 *   lock. Taking the lock synchronises with the rollover and so
	 *   we are forced to see the updated generation.
	 *
	 * - We get a valid ASID back from the cmpxchg, which means the
	 *   relaxed xchg in flush_context will treat us as reserved
	 *   because atomic RmWs are totally ordered for a given location.
	 */
	old_active_asid = atomic64_read(&per_cpu(active_asids, cpu));
  //如果进程的ASID版本号和全局ASID版本号相同
  //atomic64_cmpxchg_relaxed把当前处理器的active_asids设置成进程的ASID
  //,并且返回active_asids的旧值
	if (old_active_asid &&
	    !((asid ^ atomic64_read(&asid_generation)) >> asid_bits) &&
	    atomic64_cmpxchg_relaxed(&per_cpu(active_asids, cpu),
				     old_active_asid, asid))
		goto switch_mm_fastpath;
  //如果active_asids的旧值是0，说明其他处理器在分配ASID时把全局ASID版本号加1了，那么执行慢速路径

  //禁止硬中断并且申请自旋锁cpu_asid_lock
	raw_spin_lock_irqsave(&cpu_asid_lock, flags);
  //在申请自旋锁cpu_asid_lock之后重新比较进程的ASID版本号和全局ASID版本号，如果进程的ASID版本号和全局ASID版本号不同，那么调用函数new_context给进程重新分配ASID
	/* Check that our ASID belongs to the current generation. */
	asid = atomic64_read(&mm->context.id);
	if ((asid ^ atomic64_read(&asid_generation)) >> asid_bits) {
		asid = new_context(mm);
		atomic64_set(&mm->context.id, asid);
	}

	if (cpumask_test_and_clear_cpu(cpu, &tlb_flush_pending))
		local_flush_tlb_all();

	atomic64_set(&per_cpu(active_asids, cpu), asid);
	raw_spin_unlock_irqrestore(&cpu_asid_lock, flags);

switch_mm_fastpath:

	arm64_apply_bp_hardening();

	/*
	 * Defer(延迟) TTBR0_EL1 setting for user threads to uaccess_enable() when
	 * emulating PAN.
	 */
	if (!system_uses_ttbr0_pan())
		cpu_switch_mm(mm->pgd, mm);
}

/* Errata workaround post TTBRx_EL1 update. */
asmlinkage void post_ttbr_update_workaround(void)
{
	asm(ALTERNATIVE("nop; nop; nop",
			"ic iallu; dsb nsh; isb",
			ARM64_WORKAROUND_CAVIUM_27456,
			CONFIG_CAVIUM_ERRATUM_27456));
}

static int asids_init(void)
{
	asid_bits = get_cpu_asid_bits();
	/*
	 * Expect allocation after rollover to fail if we don't have at least
	 * one more ASID than CPUs. ASID #0 is reserved for init_mm.
	 */
	WARN_ON(NUM_USER_ASIDS - 1 <= num_possible_cpus());
	atomic64_set(&asid_generation, ASID_FIRST_VERSION);
	asid_map = kcalloc(BITS_TO_LONGS(NUM_USER_ASIDS), sizeof(*asid_map),
			   GFP_KERNEL);
	if (!asid_map)
		panic("Failed to allocate bitmap for %lu ASIDs\n",
		      NUM_USER_ASIDS);

	pr_info("ASID allocator initialised with %lu entries\n", NUM_USER_ASIDS);
	return 0;
}
early_initcall(asids_init);
