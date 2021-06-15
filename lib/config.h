#ifndef _CONFIG_H
#define _CONFIG_H
#include "arch.h"

//#define TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
//#define TS_NESTED_LOCKING
#define TS_ENABLE_HELP_RECLAIM

#define TS_N_READ_SET_ENTRIES 100
#define TS_N_WRITE_SET_ENTRIES 10
#define TS_ENABLE_STATS
//#define TS_TIME_MEASUREMENT
#define TS_ATTACH_GDB_ASSERT_FAIL 0 /* attach gdb at TS_ASSERT() failure */

#define TS_ORDO_TIMESTAMPING /* We should delete non-ordo code. */

#define TS_MAX_THREAD_NUM (1ul << 14) /* 16384 (2**18 * 2**14 = 2**32) */
#define TS_INIT_PTR_SET_SIZE 512
#define TS_QP_INTERVAL_USEC 500 /* 0.5 msec */

#define TS_TVLOG_SIZE (1ul << 20) /* 1MB */
#define TS_TVLOG_MASK (~(TS_TVLOG_SIZE - 1))
#define TS_TVLOG_HIGH_MARK (TS_TVLOG_SIZE - (TS_TVLOG_SIZE >> 2)) /* 75% */
#define TS_TVLOG_LOW_MARK (TS_TVLOG_SIZE >> 1) /* 50% */

#define TS_OPLOG_SIZE (1ul << 20) /* 1MB */
#define TS_OPLOG_HIGH_MARK (TS_OPLOG_SIZE - (TS_OPLOG_SIZE >> 2)) /* 75% */

#define TS_CKPTLOG_SIZE                                                        \
	(1ul << 22) /* 4MB */ /* TODO: should be 10x TVLOG_SIZE */
#define TS_CKPTLOG_HIGH_MARK                                                   \
	(TS_CKPTLOG_SIZE - (TS_CKPTLOG_SIZE >> 2)) /* 75% */
#define TS_CKPTLOG_LOW_MARK                                                    \
	(TS_CKPTLOG_HIGH_MARK - (TS_CKPTLOG_SIZE >> 3)) /* 62.5% */

#define TS_PTR_SIZE 8
#define TS_PTR_MASK (~(TS_PTR_SIZE - 1))
#define TS_CACHE_LINE_SIZE L1_CACHE_BYTES
#define TS_CACHE_LINE_MASK (~(TS_CACHE_LINE_SIZE - 1))
#define TS_PMEM_PAGE_SIZE (2 * 1024 * 1024) /* 2MB */
#define TS_PMEM_PAGE_MASK (~(TS_PMEM_PAGE_SIZE - 1))
#define TS_DEFAULT_PADDING CACHE_DEFAULT_PADDING

#endif /* _CONFIG_H */
