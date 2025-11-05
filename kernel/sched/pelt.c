// SPDX-License-Identifier: GPL-2.0
/*
 * Per Entity Load Tracking
 *
 *  Copyright (C) 2007 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 *  Interactivity improvements by Mike Galbraith
 *  (C) 2007 Mike Galbraith <efault@gmx.de>
 *
 *  Various enhancements by Dmitry Adamushko.
 *  (C) 2007 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 *  Group scheduling enhancements by Srivatsa Vaddagiri
 *  Copyright IBM Corporation, 2007
 *  Author: Srivatsa Vaddagiri <vatsa@linux.vnet.ibm.com>
 *
 *  Scaled math optimizations by Thomas Gleixner
 *  Copyright (C) 2007, Thomas Gleixner <tglx@linutronix.de>
 *
 *  Adaptive scheduling granularity, math enhancements by Peter Zijlstra
 *  Copyright (C) 2007 Red Hat, Inc., Peter Zijlstra
 *
 *  Move PELT related code from fair.c into this pelt.c file
 *  Author: Vincent Guittot <vincent.guittot@linaro.org>
 */

#include <linux/sched.h>
#include "sched.h"
#include "pelt.h"

#include <trace/events/sched.h>

/*
 * Approximate:
 *   val * y^n,    where y^32 ~= 0.5 (~1 scheduling period)
 */
static u64 decay_load(u64 val, u64 n)
{
	unsigned int local_n;

  /* 我们认为当时间经过2016个周期后，衰减后的值为0。即val*y^n=0, n > 2016 */
	if (unlikely(n > LOAD_AVG_PERIOD * 63)) //2016
		return 0; //为了避免浮点数运算，采用移位和乘法运算提高计算速度

	/* after bounds checking we can collapse(折叠) to 32-bit */
	local_n = n;//会截断高 32 位，只保留低 32 位的值

	/*
	 * As y^PERIOD = 1/2, we can combine
	 *    y^n = 1/2^(n/PERIOD) * y^(n%PERIOD)
	 * With a look-up table which covers y^n (n<PERIOD)
	 *
	 * To achieve constant time decay_load.
	 */
	/*由于y^32=0.5，因此我们只需要计算y*2^32~y^31*2^32的值保存到数组中即可。当n大于31的时候，为了计算yn*2^32我们可以借助y^32=0.5公式间接计算*/
	/*http://www.wowotech.net/process_management/450.html*/
	if (unlikely(local_n >= LOAD_AVG_PERIOD)) {
		val >>= local_n / LOAD_AVG_PERIOD;
		local_n %= LOAD_AVG_PERIOD;
	}
//runnable_avg_yN_inv[local_n]中val is y^local_n * 2^32
	val = mul_u64_u32_shr(val, runnable_avg_yN_inv[local_n], 32); //>>32 ，表示在计算完后缩小
	return val;
}

static u32 __accumulate_pelt_segments(u64 periods, u32 d1, u32 d3)
{
	u32 c1, c2, c3 = d3; /* y^0 == 1 */

	/*
	 * c1 = d1 y^p
	 */
	c1 = decay_load((u64)d1, periods);

	/*
	 *            p-1
	 * c2 = 1024 \Sum y^n
	 *            n=1
	 *
	 *              inf        inf
	 *    = 1024 ( \Sum y^n - \Sum y^n - y^0 )
	 *              n=0        n=p
	 */
  /*  y^0+...+y^n=y^0+...y^p-1+y^p+...+y^n
   * y^1+...y^p-1=y^0+...+y^n-(y^p+...+y^n)-y^0
   * */
	/*****easy understanding: Sp=y^p+y^(p+1)+⋯=y^p(1+y+y2+⋯)=y^p*S*/
	c2 = LOAD_AVG_MAX - decay_load(LOAD_AVG_MAX, periods) - 1024; //在完整的衰减总和中，去掉最早的 y^0 和最晚 y^p~y^n，剩下的是中间段的负载
	/* another解释一下(y^p+ ... y^n)怎么算, 根据等比级数求和公式，极限和S=a/(1-r)，首项为a，公比为r；
	 * 可以知道y^p+...+y^n的值为，S=y^p / (1-y)；
	 * 因为LOAD_AVG_MAX=1024 * (y^0 + y^1 + y^2 + ... + y^n)= 1024 * 1/(1-y)，n无穷大；
	 * 所以S * 1024=y^p * LOAD_AVG_MAX，可由 `decay_load` 求出*/

	return c1 + c2 + c3;
}

#define cap_scale(v, s) ((v)*(s) >> SCHED_CAPACITY_SHIFT)

/*
 * Accumulate the three separate parts of the sum; d1 the remainder
 * of the last (incomplete) period, d2 the span of full periods and d3
 * the remainder of the (incomplete) current period.
 *
 *           d1          d2           d3
 *           ^           ^            ^
 *           |           |            |
 *         |<->|<----------------->|<--->|
 * ... |---x---|------| ... |------|-----x (now)
 *
 *                           p-1
 * u' = (u + d1) y^p + 1024 \Sum y^n + d3 y^0
 *                           n=1
 *
 *    = u y^p +					(Step 1)
 *
 *                     p-1
 *      d1 y^p + 1024 \Sum y^n + d3 y^0		(Step 2)
 *                     n=1
 */
static __always_inline u32
accumulate_sum(u64 delta, struct sched_avg *sa, // 用來計算計算 sched_entity 對 load 的貢獻
	       unsigned long load, unsigned long runnable, int running)
{
	u32 contrib = (u32)delta; /* p == 0 -> delta < 1024 */
	u64 periods;

  //period_contrib记录的是上次更新负载不足1024us周期的时间
  //delta是经过的时间，为了计算经过的周期个数需要加上period_contrib，然后整除1024
	delta += sa->period_contrib;
  //计算周期个数
	periods = delta / 1024; /* A period is 1024us (~1ms) */

	/*
	 * Step 1: decay old *_sum if we crossed period boundaries.
	 */
	if (periods) {
    //调用decay_load()函数计算公式中的step1部分, sa->load_sum是上个周期负载贡献总和
		sa->load_sum = decay_load(sa->load_sum, periods);//decay sa->load_sum according to the periods
		sa->runnable_load_sum =
			decay_load(sa->runnable_load_sum, periods); // same as above
		sa->util_sum = decay_load((u64)(sa->util_sum), periods);//sa->util_sum's datatype is u32

		/*
		 * Step 2
		 */
		delta %= 1024;
		contrib = __accumulate_pelt_segments(periods,
				1024 - sa->period_contrib, delta);
	}
  //period_contrib记录的是上次更新负载不足1024us周期的时间
	sa->period_contrib = delta; //sa->period_contrib 記下 d3，下一次週期時會需要
//可以將 contrib 總結到 *_sum 中
	if (load) // 这里的load可能为cfs_rq->load.weight, rq 下所有 se 的 weight 之和 , 参考account_entity_enqueue
		sa->load_sum += load * contrib;//load为1 ,也可能是当前进程的权重,需要根据上下文判断
	if (runnable)
		sa->runnable_load_sum += runnable * contrib;
	if (running)
		sa->util_sum += contrib << SCHED_CAPACITY_SHIFT; //<<10; 左移的作用是为了让 util_sum 和 util_avg 的数值保持“定点整数精度”

	return periods;
}

/* (我们可以将历史对可运行平均值的贡献表示为几何级数的系数)
 * We can represent the historical contribution to runnable average as the
 * coefficients of a geometric series.  To do this we sub-divide(细分) our runnable
 * history into segments of approximately 1ms (1024us); label the segment that
 * occurred N-ms ago p_N, with p_0 corresponding to the current period, e.g.
 *
 * [<- 1024us ->|<- 1024us ->|<- 1024us ->| ...
 *      p0            p1           p2
 *     (now)       (~1ms ago)  (~2ms ago)
 *
 * Let u_i denote the fraction of p_i that the entity was runnable.
 *
 * We then designate the fractions u_i as our co-efficients, yielding the
 * following representation of historical load:
 *   u_0 + u_1*y + u_2*y^2 + u_3*y^3 + ...
 *
 * We choose y based on the with of a reasonably scheduling period, fixing:
 *   y^32 = 0.5
 *
 * This means that the contribution to load ~32ms ago (u_32) will be weighted
 * approximately half as much as the contribution to load within the last ms
 * (u_0).
 *
 * When a period "rolls over" and we have new u_0`, multiplying the previous
 * sum again by y is sufficient to update:
 *   load_avg = u_0` + y*(u_0 + u_1*y + u_2*y^2 + ... )
 *            = u_0 + u_1*y + u_2*y^2 + ... [re-labeling u_i --> u_{i+1}]
 */
static __always_inline int
___update_load_sum(u64 now, struct sched_avg *sa,
		  unsigned long load, unsigned long runnable, int running)// load:当前负载权重（task 的静态权重)se->load.weight;runnable: 在运行队列上;running:当前实体正在执行
{
	u64 delta;

	delta = now - sa->last_update_time;
	/*
	 * This should only happen when time goes backwards, which it
	 * unfortunately does during sched clock init when we swap over to TSC.
	 */
	if ((s64)delta < 0) {
		sa->last_update_time = now;
		return 0;
	}

	/*
	 * Use 1024ns as the unit of measurement since it's a reasonable
	 * approximation of 1us and fast to compute.
	 */
	delta >>= 10; //round down, Remove the part that is less than 1024ns
	if (!delta)
		return 0;

	sa->last_update_time += delta << 10;

	/*
	 * running is a subset of runnable (weight) so running can't be set if
	 * runnable is clear. But there are some corner cases where the current
	 * se has been already dequeued but cfs_rq->curr still points to it.
	 * This means that weight will be 0 but not running for a sched_entity
	 * but also for a cfs_rq if the latter becomes idle. As an example,
	 * this happens during idle_balance() which calls
	 * update_blocked_averages()
	 */
	if (!load)
		runnable = running = 0;

	/*
	 * Now we know we crossed measurement unit boundaries. The *_avg
	 * accrues by two steps:
	 *
	 * Step 1: accumulate *_sum since last_update_time. If we haven't
	 * crossed period boundaries, finish.
	 */
	if (!accumulate_sum(delta, sa, load, runnable, running))
		return 0;

	return 1;
}

static __always_inline void
___update_load_avg(struct sched_avg *sa, unsigned long load, unsigned long runnable)
{//计算当前时间的负载只需要上个周期负载贡献总和乘以衰减系数y ,所以 LOAD_AVG_MAX*y + sa->period_contrib
	u32 divider = LOAD_AVG_MAX - 1024 + sa->period_contrib;  //可根据等比级数求和公式求得: S = a / (1 - r); a：首项, r：公比（common ratio）
	
	/*
	 * Step 2: update *_avg.
	 */
	sa->load_avg = div_u64(load * sa->load_sum, divider); //同样这里的load可能为1，也可能为cfs_rq->load.weight,取决于context
	sa->runnable_load_avg =	div_u64(runnable * sa->runnable_load_sum, divider);
	WRITE_ONCE(sa->util_avg, sa->util_sum / divider); //util_sum 类型为u32,所以不需要div_u64
}

/*
 * sched_entity:
 *
 *   task:
 *     se_runnable() == se_weight()
 *
 *   group: [ see update_cfs_group() ]
 *     se_weight()   = tg->weight * grq->load_avg / tg->load_avg
 *     se_runnable() = se_weight(se) * grq->runnable_load_avg / grq->load_avg
 *
 *   load_sum := runnable_sum
 *   load_avg = se_weight(se) * runnable_avg
 *
 *   runnable_load_sum := runnable_sum
 *   runnable_load_avg = se_runnable(se) * runnable_avg
 *
 * XXX collapse load_sum and runnable_load_sum
 *
 * cfq_rq:
 *
 *   load_sum = \Sum se_weight(se) * se->avg.load_sum
 *   load_avg = \Sum se->avg.load_avg
 *
 *   runnable_load_sum = \Sum se_runnable(se) * se->avg.runnable_load_sum
 *   runnable_load_avg = \Sum se->avg.runable_load_avg
 */
//当任务 sleep、阻塞（即不在运行队列中）时，它的负载也应该随着时间“慢慢变小”
//Purpose: 用于 更新处于“阻塞”状态（blocked，即不在运行队列上）的调度实体（sched_entity）的负载平均值(sched_avg)
int __update_load_avg_blocked_se(u64 now, struct sched_entity *se)
{//True:负载状态有更新(util_sum、runnable_load_sum、load_sum至少一个值发生变化)
	if (___update_load_sum(now, &se->avg, 0, 0, 0)) {//参数 0, 0, 0 表示当前这个更新不添加新的活跃贡献值，仅做指数衰减
		___update_load_avg(&se->avg, se_weight(se), se_runnable(se));
	  //Following tracepoints are not exported in tracefs and provide hooking* mechanisms only for testing and debugging purposes. Postfixed with _tp to make them easily identifiable in the code.
		trace_pelt_se_tp(se);
		return 1;
	}

	return 0;
}

int __update_load_avg_se(u64 now, struct cfs_rq *cfs_rq, struct sched_entity *se)
{//每次 __update_load_avg_se 時，首先都要先透過 ___update_load_sum 更新 _*sum 相關的數據
	if (___update_load_sum(now, &se->avg, !!se->on_rq, !!se->on_rq,
				cfs_rq->curr == se)) { //return 1(periods>0) if update sucessful

		___update_load_avg(&se->avg, se_weight(se), se_runnable(se)); //(,1024,)
		cfs_se_util_change(&se->avg);
		trace_pelt_se_tp(se);
		return 1;
	}

	return 0;
}

int __update_load_avg_cfs_rq(u64 now, struct cfs_rq *cfs_rq)
{
	if (___update_load_sum(now, &cfs_rq->avg,
				scale_load_down(cfs_rq->load.weight),
				scale_load_down(cfs_rq->runnable_weight),
				cfs_rq->curr != NULL)) {

		___update_load_avg(&cfs_rq->avg, 1, 1);
		trace_pelt_cfs_tp(cfs_rq);
		return 1;
	}

	return 0;
}

/*
 * rt_rq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_load_sum = load_sum
 *
 *   load_avg and runnable_load_avg are not supported and meaningless.
 *
 */

int update_rt_rq_load_avg(u64 now, struct rq *rq, int running)
{
	if (___update_load_sum(now, &rq->avg_rt,
				running,
				running,
				running)) {

		___update_load_avg(&rq->avg_rt, 1, 1);
		trace_pelt_rt_tp(rq);
		return 1;
	}

	return 0;
}

/*
 * dl_rq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_load_sum = load_sum
 *
 */

int update_dl_rq_load_avg(u64 now, struct rq *rq, int running)
{
	if (___update_load_sum(now, &rq->avg_dl,
				running,
				running,
				running)) {

		___update_load_avg(&rq->avg_dl, 1, 1);
		trace_pelt_dl_tp(rq);
		return 1;
	}

	return 0;
}

#ifdef CONFIG_HAVE_SCHED_AVG_IRQ //1
/*
 * irq:
 *
 *   util_sum = \Sum se->avg.util_sum but se->avg.util_sum is not tracked
 *   util_sum = cpu_scale * load_sum
 *   runnable_load_sum = load_sum
 *
 */
/*@running: 表示从上次更新以来该 CPU 的 中断运行时间（ns）*/
/*update rq->irq_avg::*_sum&*_avg */
int update_irq_load_avg(struct rq *rq, u64 running)
{
	int ret = 0;

	/*
	 * We can't use clock_pelt because irq time is not accounted in
	 * clock_task. Instead we directly scale the running time to
	 * reflect the real amount of computation
	 */
	running = cap_scale(running, arch_scale_freq_capacity(cpu_of(rq))); //The value remains unchanged.
	running = cap_scale(running, arch_scale_cpu_capacity(cpu_of(rq))); //The value remains unchanged.


	/*
	 * We know the time that has been used by interrupt since last update
	 * but we don't when. Let be pessimistic(悲观) and assume that interrupt has
	 * happened just before the update. This is not so far from reality
	 * because interrupt will most probably wake up task and trig an update
	 * of rq clock during which the metric is updated.
	 * We start to decay with normal context time and then we add the
	 * interrupt context time.
	 * We can safely remove running from rq->clock because
	 * rq->clock += delta with delta >= running
	 */
	/*中断时间段包含两种状态：
	 * 中断前：CPU 正常运行任务（非中断负载）
	 * 中断时：CPU 被中断抢占（irq 负载）
	 * 所以只能分两次调用，表示这两个状态区间
	 * */
	/*用来对历史值做衰减（decay），相当于“推进时间到中断开始之前”*/
	ret = ___update_load_sum(rq->clock - running, &rq->avg_irq, //更新时间推进到中断发生前的时刻(rq->clock - running)
				0, //参数全是 0 → 表示这一段时间没有活动（sleeping 状态）
				0,
				0);
/*把这段中断时间 (running) 的负载贡献加入到统计中*/
	ret += ___update_load_sum(rq->clock, &rq->avg_irq,  //更新时间推进到当前时刻（rq->clock）；
				1, //参数全为 1 → 表示这段时间是活跃的（中断在运行）
				1, 
				1);

	if (ret) {
		___update_load_avg(&rq->avg_irq, 1, 1);
		trace_pelt_irq_tp(rq);
	}

	return ret;
}
#endif
