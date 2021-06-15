#ifndef __KERNEL__
#include "timestone.h"
#else
#include <linux/timestone.h>
#endif

#include "util.h"
#include "debug.h"
#include "port.h"
#include "oplog.h"
#include "clock.h"
#include "qp.h"

int oplog_create(ts_thread_struct_t *self, ts_oplog_t *oplog,
		 unsigned long size, unsigned short status)
{
	return nvlog_create(self, oplog, size, TYPE_OPLOG, status);
}

void oplog_destroy(ts_oplog_t *oplog)
{
	return nvlog_destroy(oplog);
}

ts_op_entry_t *oplog_enq(ts_oplog_t *oplog, unsigned long local_clk,
			 unsigned long wrt_clk, ts_op_info_t *op_info)
{
	ts_op_entry_t *op_entry;
	void *opd;
	unsigned long op_type;
	size_t opd_size;

	/* Get op_info */
	opd = op_info->op_entry.opd;
	op_type = op_info->op_entry.op_type;
	opd_size = op_info->curr;
	ts_assert(opd_size < TS_MAX_OPERAND_SIZE);

	/* Reserve a space in oplog */
	op_entry = (ts_op_entry_t *)nvlog_enq(oplog, sizeof(ts_op_entry_hdr_t) +
							     opd_size);
#ifdef TS_GTEST
	if (unlikely(!op_entry))
		return NULL;
#else
	ts_assert(op_entry);
#endif

	/* Initialize a log entry in oplog */
	op_entry->nvlog_hdr.wrt_clk = wrt_clk;
	op_entry->oplog_hdr.local_clk = local_clk;
	op_entry->oplog_hdr.op_type = op_type;
	memcpy(op_entry->oplog_hdr.opd, opd, opd_size);

#ifdef TS_ENABLE_STATS
	stat_thread_asgn(oplog->thread, n_oplog_written_bytes, oplog->tail_cnt);
#endif

	return op_entry;
}

void oplog_enq_persist(ts_oplog_t *oplog)
{
	nvlog_enq_persist(oplog);
}

ts_op_entry_t *oplog_deq(ts_oplog_t *oplog)
{
	return (ts_op_entry_t *)nvlog_deq(oplog);
}

void oplog_deq_persist(ts_oplog_t *oplog)
{
	nvlog_deq_persist(oplog);
}

ts_op_entry_t *oplog_peek_head(ts_oplog_t *oplog)
{
	return (ts_op_entry_t *)nvlog_peek_head(oplog);
}

int oplog_reclaim(ts_oplog_t *oplog, int force)
{
	ts_op_entry_t *op_entry;
	unsigned long last_ckpt;
	unsigned long old_head_cnt;
	unsigned long reclaimed_ops, reclaimed_bytes;

	/* Ensure all previous writes durable */
	smp_wmb();

	/* Lock */
	if (!try_lock(&oplog->reclaim_lock))
		return 0;

	/* Init */
	reclaimed_ops = reclaimed_bytes = 0;

	/* Reclaim all op that are older than the 1st qp clock */
	old_head_cnt = oplog->head_cnt;
	last_ckpt = oplog->clks->__last_ckpt;
	while ((op_entry = oplog_peek_head(oplog))) {
		if (lt_clock(op_entry->nvlog_hdr.wrt_clk, last_ckpt) ||
		    force == RECLAIM_OPLOG_FORCE) { /* TIME: < */
			/* dequeue an oplog entry for reclaim */
			oplog_deq(oplog);
			reclaimed_ops++;
			ts_assert(oplog->head_cnt <= oplog->tail_cnt);
		} else {
			/* oplog entries are ordered by nvlog_hdr.wrt_clk
			 * so, no need to check all entries */
			break;
		}
	}

	/* Persist oplog */
	ts_assert(oplog->head_cnt >= old_head_cnt);
	if (oplog->head_cnt != old_head_cnt) {
		oplog_deq_persist(oplog);
		reclaimed_bytes = oplog->head_cnt - old_head_cnt;
		ts_trace(TS_OPLOG,
			 "Oplog[%p]: %ld bytes reclaimed (h=%ld, t=%ld)\n",
			 oplog, reclaimed_bytes, oplog->head_cnt,
			 oplog->tail_cnt);
		stat_thread_inc(oplog->thread, n_oplog_reclaim);
		stat_thread_acc(oplog->thread, n_oplog_reclaimed_ops,
				reclaimed_ops);
		stat_thread_acc(oplog->thread, n_oplog_reclaimed_bytes,
				reclaimed_bytes);
	}

	/* lock */
	unlock(&oplog->reclaim_lock);

	/* Enforce ordering of ophead and ophead updates
	 * but it is optional to do pbarrier(). */
	smp_wmb_tso();
	return 1;
}

int oplog_try_request_ckpt_if_needed(ts_oplog_t *oplog)
{
	/* If oplog passes its high water mark, reclaim space. */
	if (unlikely(oplog_used(oplog) >= TS_OPLOG_HIGH_MARK)) {
		oplog_reclaim(oplog, RECLAIM_OPLOG_NORMAL);

		/* If reclamation is not possible, trigger checkpointing
		 * so we can reclaim further next time. */
		if (unlikely(oplog_used(oplog) >= TS_OPLOG_HIGH_MARK)) {
			request_tvlog_reclaim(RECLAIM_TVLOG_CKPT);
			return 1;
		}
	}
	return 0;
}
