#ifndef _OPLOG_H
#define _OPLOG_H

#include "nvlog.h"

#ifdef __cplusplus
extern "C" {
#endif

int oplog_create(ts_thread_struct_t *, ts_oplog_t *, unsigned long,
		 unsigned short);
void oplog_destroy(ts_nvlog_t *);
ts_op_entry_t *oplog_enq(ts_oplog_t *, unsigned long, unsigned long,
			 ts_op_info_t *);
ts_op_entry_t *oplog_deq(ts_oplog_t *);
void oplog_enq_persist(ts_oplog_t *);
void oplog_deq_persist(ts_oplog_t *);
int oplog_reclaim(ts_oplog_t *, int);
int oplog_try_request_ckpt_if_needed(ts_oplog_t *);

static inline unsigned long oplog_used(ts_oplog_t *oplog)
{
	return nvlog_used(oplog);
}

#ifdef __cplusplus
}
#endif
#endif
