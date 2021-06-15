#ifndef _CKPTLOG_H
#define _CKPTLOG_H

#include "nvlog.h"

#ifdef __cplusplus
extern "C" {
#endif

int ckptlog_create(ts_thread_struct_t *, ts_ckptlog_t *, unsigned long,
		   unsigned short);
void ckptlog_destroy(ts_ckptlog_t *);
ts_ckpt_entry_t *ckptlog_enq(ts_ckptlog_t *, unsigned long, unsigned long,
			     ts_cpy_hdr_struct_t *);
ts_ckpt_entry_t *ckptlog_enq_tombstone(ts_ckptlog_t *, unsigned long,
				       unsigned long, ts_cpy_hdr_struct_t *);
ts_ckpt_entry_t *ckptlog_deq(ts_ckptlog_t *);
void ckptlog_enq_persist(ts_ckptlog_t *);
void ckptlog_deq_persist(ts_ckptlog_t *);
void ckptlog_reclaim(ts_ckptlog_t *);
void ckptlog_flush(ts_ckptlog_t *);
void ckptlog_cleanup(ts_ckptlog_t *, unsigned long);

static inline unsigned long ckptlog_used(ts_ckptlog_t *ckptlog)
{
	return nvlog_used(ckptlog);
}

#ifdef __cplusplus
}
#endif
#endif
