#ifndef _DEBUG_H
#define _DEBUG_H

#include "arch.h"
#include "config.h"
#include "timestone_i.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { TS_ERROR,
       TS_WARNING,
       TS_INFO,
       TS_OPLOG,
       TS_CKPTLOG,
       TS_TVLOG,
       TS_DUMP,
       TS_QP,
       TS_DEBUG,
       TS_ISOLATION,
       __TS_TRACE_LEVEL_DEFAULT__,
       TS_FP,
       TS_NVMFREE,
};

#ifndef TS_TRACE_LEVEL
#define TS_TRACE_LEVEL TS_INFO
#endif

#define ts_trace(level, fmt, ...)                                              \
	do {                                                                   \
		if (level <= TS_TRACE_LEVEL) {                                 \
			printf(fmt, ##__VA_ARGS__);                            \
		}                                                              \
	} while (0)

/*
 * Assert, panic, warning, etc.
 */
#ifdef TS_ENABLE_ASSERT
#define ts_assert(cond)                                                        \
	if (unlikely(!(cond))) {                                               \
		extern void ts_assert_fail(void);                              \
		printf("\n-----------------------------------------------\n"); \
		printf("\nAssertion failure: %s:%d '%s'\n", __FILE__,          \
		       __LINE__, #cond);                                       \
		ts_assert_fail();                                              \
	}

#define ts_assert_msg(cond, self, fmt, ...)                                    \
	if (unlikely(!(cond))) {                                               \
		extern void ts_assert_fail(void);                              \
		printf("\n-----------------------------------------------\n"); \
		printf("\nAssertion failure: %s:%d '%s'\n", __FILE__,          \
		       __LINE__, #cond);                                       \
		TS_TRACE(self, fmt, ##__VA_ARGS__);                            \
		ts_assert_fail();                                              \
	}
#else
#define ts_assert(cond)
#define ts_assert_msg(cond, self, fmt, ...)
#endif /* TS_ENABLE_ASSERT */

#define ts_warning(cond) ts_assert(cond)
#define ts_warning_msg(cond, self, fmt, ...)                                   \
	ts_assert_msg(cond, self, fmt, ##__VA_ARGS__)

/*
 * Time measurement
 */
#ifdef TS_TIME_MEASUREMENT
#define ts_start_timer()                                                       \
	{                                                                      \
		unsigned long __b_e_g_i_n__;                                   \
		__b_e_g_i_n__ = read_tsc()
#define ts_stop_timer(tick)                                                    \
	(tick) += read_tsc() - __b_e_g_i_n__;                                  \
	}
#else
#define ts_start_timer()
#define ts_stop_timer(tick)
#endif /* TS_TIME_MEASUREMENT */

/*
 * Statistics functions
 */
static inline void stat_reset(ts_stat_t *stat)
{
	int i;
	for (i = 0; i < stat_max__; ++i) {
		stat->cnt[i] = 0;
	}
}
static inline void stat_atomic_merge(ts_stat_t *tgt, ts_stat_t *src)
{
	int i;
	for (i = 0; i < stat_max__; ++i) {
		smp_faa(&tgt->cnt[i], src->cnt[i]);
	}
}

static inline void stat_inc(ts_stat_t *stat, int s)
{
	stat->cnt[s]++;
}

static inline void stat_acc(ts_stat_t *stat, int s, unsigned long v)
{
	stat->cnt[s] += v;
}

static inline void stat_asgn(ts_stat_t *stat, int s, unsigned long v)
{
	stat->cnt[s] = v;
}

static inline void stat_max(ts_stat_t *stat, int s, unsigned long v)
{
	if (v > stat->cnt[s])
		stat->cnt[s] = v;
}

#define tvlog_to_thread(__log)                                                 \
	({                                                                     \
		void *p = (void *)(__log);                                     \
		void *q;                                                       \
		q = p - ((size_t) & ((ts_thread_struct_t *)0)->tvlog);         \
		(ts_thread_struct_t *)q;                                       \
	})

#ifdef TS_ENABLE_STATS
extern ts_stat_t g_stat;

#define stat_thread_inc(self, x) stat_inc(&(self)->stat, stat_##x)
#define stat_thread_acc(self, x, y) stat_acc(&(self)->stat, stat_##x, y)
#define stat_thread_asgn(self, x, y) stat_asgn(&(self)->stat, stat_##x, y)
#define stat_thread_max(self, x, y) stat_max(&(self)->stat, stat_##x, y)
#define stat_thread_merge(self) stat_atomic_merge(&g_stat, &(self)->stat)
#define stat_qp_inc(qp, x) stat_inc(&(qp)->stat, stat_##x)
#define stat_qp_acc(qp, x, y) stat_acc(&(qp)->stat, stat_##x, y)
#define stat_qp_max(qp, x, y) stat_max(&(qp)->stat, stat_##x, y)
#define stat_qp_merge(qp) stat_atomic_merge(&g_stat, &(qp)->stat)
#else /* TS_ENABLE_STATS */
#define stat_thread_inc(self, x)
#define stat_thread_acc(self, x, y)
#define stat_thread_asgn(self, x, y)
#define stat_thread_max(self, x, y)
#define stat_thread_merge(self)
#define stat_qp_inc(qp, x)
#define stat_qp_acc(qp, x, y)
#define stat_qp_max(qp, x, y)
#define stat_qp_merge(qp)
#endif /* TS_ENABLE_STATS */

/*
	 * dump functions
	 */
#if TS_TRACE_LEVEL >= TS_DUMP
void ts_dbg_dump_act_vhdr(const char *, const int, ts_act_vhdr_t *);
void ts_dbg_dump_obj_hdr(const char *, const int, ts_obj_hdr_t *);
void ts_dbg_dump_cpy_hdr(const char *, const int, ts_cpy_hdr_t *);
void ts_dbg_dump_cpy_hdr_struct(const char *, const int, ts_cpy_hdr_struct_t *);
void ts_dbg_dump_version_chain(const char *, const int, ts_cpy_hdr_struct_t *,
			       unsigned long);
void ts_dbg_dump_all_version_chain_act_hdr(const char *, const int,
					   ts_act_vhdr_t *);
void ts_dbg_dump_all_version_chain_chs(const char *, const int,
				       ts_cpy_hdr_struct_t *);
#else
#define ts_dbg_dump_act_vhdr(...)
#define ts_dbg_dump_obj_hdr(...)
#define ts_dbg_dump_cpy_hdr(...)
#define ts_dbg_dump_cpy_hdr_struct(...)
#define ts_dbg_dump_version_chain(...)
#define ts_dbg_dump_all_version_chain_act_hdr(...)
#define ts_dbg_dump_all_version_chain_chs(...)
#endif /* TS_TRACE_LEVEL >= TS_DUMP */

#ifdef __cplusplus
}
#endif
#endif /* _DEBUG_H */
