// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifndef _CLOCK_H
#define _CLOCK_H

#include "timestone_i.h"
#include "ordo_clock.h"
#include "util.h"
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TS_ORDO_TIMESTAMPING
static volatile unsigned long
	__g_wrt_clk[2 * CACHE_DEFAULT_PADDING] ____cacheline_aligned2;
#define g_wrt_clk __g_wrt_clk[CACHE_DEFAULT_PADDING]
#define gte_clock(__t1, __t2) ((__t1) >= (__t2))
#define lte_clock(__t1, __t2) ((__t1) <= (__t2))
#define gt_clock(__t1, __t2) ((__t1) > (__t2))
#define lt_clock(__t1, __t2) ((__t1) < (__t2))
#define get_clock() g_wrt_clk
#define get_clock_relaxed() get_clock()
#define init_lock()                                                            \
	do {                                                                   \
		g_wrt_clk = 0;                                                 \
	} while (0)
#define new_clock(__x) (g_wrt_clk + 1)
#define advance_clock() smp_faa(&g_wrt_clk, 1)
#define correct_qp_clock(qp_clk) qp_clk
#else /* TS_ORDO_TIMESTAMPING */
#include "ordo_clock.h"
#define gte_clock(__t1, __t2) (!ordo_lt_clock(__t1, __t2))
#define lte_clock(__t1, __t2) (!ordo_gt_clock(__t1, __t2))
#define gt_clock(__t1, __t2) ordo_gt_clock(__t1, __t2)
#define lt_clock(__t1, __t2) ordo_lt_clock(__t1, __t2)
#define get_clock() ordo_get_clock()
#define get_clock_relaxed() ordo_get_clock_relaxed()
#define init_clock() ordo_clock_init()
#define new_clock(__local_clk) ordo_new_clock((__local_clk) + ordo_boundary())
#define advance_clock()
#define correct_qp_clock(qp_clk) (qp_clk - ordo_boundary())
#endif /* TS_ORDO_TIMESTAMPING */

static inline unsigned long get_raw_wrt_clk(const ts_cpy_hdr_struct_t *chs)
{
	volatile unsigned long wrt_clk = chs->cpy_hdr.__wrt_clk;

#ifdef TS_ENABLE_ASSERT
	if (wrt_clk == MAX_VERSION) {
		ts_trace(TS_DUMP,
			 "************ ASSERT INFO ******************\n");
		ts_trace(TS_DUMP, "cpy obj status= %d\n", chs->obj_hdr.status);
		ts_trace(TS_DUMP, "cpy obj type= %d \n", chs->obj_hdr.type);
		ts_trace(TS_DUMP, "wrt_clk= %ld\n", wrt_clk);
		ts_trace(TS_DUMP, "cpy obj wrt_clk= %ld\n",
			 chs->cpy_hdr.__wrt_clk);
		ts_trace(TS_DUMP, "****************************************\n");
	}
//	ts_assert(wrt_clk != MAX_VERSION);
#endif
	return wrt_clk;
}

static inline unsigned long get_wrt_clk(const ts_cpy_hdr_struct_t *chs,
					unsigned long local_clk)
{
	/* getting the write clock of of a chs,
	 * which would be in the middle of committed. */

	ts_wrt_set_t *ws;
	unsigned long wrt_clk;

	/* Get the raw write clock of a chs. */
	wrt_clk = chs->cpy_hdr.__wrt_clk;

	/* If a chs is committed already, return wrt_clk. */
	if (likely(wrt_clk != MAX_VERSION)) {
		return wrt_clk;
	}

	/* If a chs is not committed, check if
	 * its write set is already committed. */
	ws = (ts_wrt_set_t *)chs->cpy_hdr.p_ws;

	/* If a commit procedure does not even start
	 * (i.e., pending_wrt_clk is MAX_VERSION),
	 * just return MAX_VERSION. */
	wrt_clk = ws->pending_wrt_clk;
	if (wrt_clk == MAX_VERSION) {
		return MAX_VERSION;
	}

	/* If the commit procedure already started,
	 * check if a thread does not need to access
	 * the committing version (i.e., local clock
	 * of a thread is greater than pending write
	 * clock), just return MAX_VERSION. */
	if (local_clk < wrt_clk) {
		return MAX_VERSION;
	}

	/* If a thread has to access the version,
	 * wait until the commit procedure is done. */
	smp_rmb();
	while ((wrt_clk = ws->wrt_clk) == MAX_VERSION) {
		port_cpu_relax_and_yield();
		smp_rmb();
	}
	ts_assert(ws->pending_wrt_clk == ws->wrt_clk);

	return wrt_clk;
}

#ifdef __cplusplus
}
#endif
#endif
