#ifndef _TVLOG_H
#define _TVLOG_H

#include "timestone_i.h"
#include "util.h"
#include "debug.h"

#ifdef __cplusplus
extern "C" {
#endif

int tvlog_create(ts_thread_struct_t *self, ts_tvlog_t *tvlog);
void tvtlog_destroy(ts_tvlog_t *tvlog);

static inline unsigned long tvlog_used(ts_tvlog_t *log)
{
	unsigned long used = log->tail_cnt - log->head_cnt;
	return used;
}

ts_cpy_hdr_struct_t *tvlog_append_begin(ts_tvlog_t *, volatile ts_act_vhdr_t *,
					unsigned int, int *);

static inline void tvlog_append_abort(ts_tvlog_t *tvlog,
				      ts_cpy_hdr_struct_t *chs)
{
	/* Do nothing since tail_cnt and num_objs are not updated yet.
	 * Let's keep this for readability of the code. */
}

static inline void tvlog_append_end(ts_tvlog_t *tvlog, ts_cpy_hdr_struct_t *chs,
				    int bogus_allocated)
{
	tvlog->tail_cnt += get_entry_size(chs);
	tvlog->cur_wrt_set->num_objs++;
	if (bogus_allocated) {
		tvlog->cur_wrt_set->num_objs++;
	}

#ifdef TS_ENABLE_STATS
	stat_thread_asgn(tvlog_to_thread(tvlog), n_tvlog_written_bytes,
			 tvlog->tail_cnt);
#endif
}

void tvlog_commit(ts_tvlog_t *tvlog, ts_oplog_t *oplog, ts_ptr_set_t *free_set,
		  unsigned long local_clk, ts_op_info_t *op_info);
void tvlog_abort(ts_tvlog_t *tvlog, ts_ptr_set_t *free_ptrs);
void tvlog_reclaim(ts_tvlog_t *, ts_ckptlog_t *);
void tvlog_reclaim_below_high_watermark(ts_tvlog_t *, ts_ckptlog_t *);
void tvlog_flush(ts_tvlog_t *, ts_ckptlog_t *);

#ifdef __cplusplus
}
#endif
#endif
