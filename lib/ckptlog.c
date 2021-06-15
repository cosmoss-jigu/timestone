#ifndef __KERNEL__
#include "timestone.h"
#else
#include <linux/timestone.h>
#endif

#include "util.h"
#include "debug.h"
#include "port.h"
#include "ckptlog.h"
#include "nvm.h"
#include "clock.h"
#include "qp.h"

int ckptlog_create(ts_thread_struct_t *self, ts_ckptlog_t *ckptlog,
		   unsigned long size, unsigned short status)
{
	int rc;

	rc = nvlog_create(self, ckptlog, size, TYPE_CKPTLOG, status);
	if (likely(!rc)) {
		ckptlog->free_set = &self->ckpt_free_set;
		ckptlog->need_reclaim = &self->reclaim.ckptlog;
	}

	return rc;
}

void ckptlog_destroy(ts_ckptlog_t *ckptlog)
{
#ifndef TS_NVM_IS_MAKALU
	if (ckptlog->free_set)
		ckptlog_cleanup(ckptlog, MAX_VERSION);
#else
	/* NOTE: This looks like a makalu bug. */
	ts_trace(TS_INFO,
		 "[%s:%d]Do not free objects at ckptlog_destroy "
		 "when you use makalu allocator\n",
		 __func__, __LINE__);
#endif

	return nvlog_destroy(ckptlog);
}

static ts_ckpt_entry_t *_ckptlog_enq(ts_ckptlog_t *ckptlog,
				     unsigned long wrt_clk,
				     unsigned long ckpt_s_clk,
				     ts_cpy_hdr_struct_t *chs)
{
	ts_ckpt_entry_t *ckpt_entry;
	volatile ts_act_vhdr_t *p_act_vhdr;
	unsigned int obj_size;
	char is_tombstone = chs->obj_hdr.type == TYPE_FREE;

	/* sanity check */
	ts_assert(chs->cpy_hdr.p_act_vhdr->np_org_act);
	obj_size = chs->obj_hdr.obj_size;

	/* reserve a space in ckpt */
	ckpt_entry = (ts_ckpt_entry_t *)nvlog_enq(
		ckptlog, (sizeof(ts_ckpt_entry_hdr_t) + sizeof(ts_obj_hdr_t) +
			  obj_size));
#ifdef TS_GTEST
	if (unlikely(!ckpt_entry))
		return NULL;
#else
	ts_assert(ckpt_entry);
#endif

	/* initialize a nvlog entry */
	ckpt_entry->nvlog_hdr.wrt_clk = wrt_clk;

	/* initialize a ckptlog entry */
	ckpt_entry->ckptlog_hdr.ckpt_s_clk = ckpt_s_clk;
	ckpt_entry->ckptlog_hdr.np_org_act =
		chs->cpy_hdr.p_act_vhdr->np_org_act;
	ckpt_entry->ckptlog_hdr.tombstone = is_tombstone;

	/* initialize a obj_hdr */
	ckpt_entry->obj_hdr.obj_size = obj_size; /* object size */
	ckpt_entry->obj_hdr.type = TYPE_NVLOG_ENTRY;

	/* copy an object in tvlog to ckptlog if it is not a tombstone */
	if (likely(!is_tombstone)) {
		memcpy(ckpt_entry->obj_hdr.obj, chs->obj_hdr.obj, obj_size);
		smp_wmb_tso();
	}

	/* update np_cur_master to the new master */
	p_act_vhdr = chs->cpy_hdr.p_act_vhdr;
	smp_atomic_store(&p_act_vhdr->np_cur_act, ckpt_entry->obj_hdr.obj);
	smp_wmb_tso();

#if 0 /* TODO: temporarily disable */
	/* invalid gen_id */
	p_act_nvhdr = get_act_nvhdr(p_act_vhdr->np_org_act);
	p_act_nvhdr->gen_id = 0;
#endif
	return ckpt_entry;
}

ts_ckpt_entry_t *ckptlog_enq(ts_ckptlog_t *ckptlog, unsigned long wrt_clk,
			     unsigned long ckpt_s_clk, ts_cpy_hdr_struct_t *chs)
{
	ts_assert(chs->obj_hdr.type == TYPE_COPY);
	return _ckptlog_enq(ckptlog, wrt_clk, ckpt_s_clk, chs);
}

ts_ckpt_entry_t *ckptlog_enq_tombstone(ts_ckptlog_t *ckptlog,
				       unsigned long wrt_clk,
				       unsigned long ckpt_s_clk,
				       ts_cpy_hdr_struct_t *chs)
{
	ts_assert(chs->obj_hdr.type == TYPE_FREE);
	return _ckptlog_enq(ckptlog, wrt_clk, ckpt_s_clk, chs);
}

static void ckptlog_try_request_reclaim_if_needed(ts_ckptlog_t *ckptlog)
{
	unsigned long used;

	/* If it needs to trigger reclamation */
	used = ckptlog_used(ckptlog);
	if (unlikely(used > TS_CKPTLOG_LOW_MARK)) {
		if (unlikely(used > TS_CKPTLOG_HIGH_MARK)) {
			request_ckptlog_reclaim(RECLAIM_CKPTLOG_WRITEBACK);
		} else {
			request_ckptlog_reclaim(RECLAIM_CKPTLOG_BEST_EFFORT);
		}
	}
}

void ckptlog_enq_persist(ts_ckptlog_t *ckptlog)
{
	nvlog_enq_persist(ckptlog);
	ckptlog_try_request_reclaim_if_needed(ckptlog);
}

ts_ckpt_entry_t *ckptlog_deq(ts_ckptlog_t *ckptlog)
{
	return (ts_ckpt_entry_t *)nvlog_deq(ckptlog);
}

void ckptlog_deq_persist(ts_ckptlog_t *ckptlog)
{
	nvlog_deq_persist(ckptlog);
}

ts_ckpt_entry_t *ckptlog_peek_head(ts_ckptlog_t *ckptlog)
{
	return (ts_ckpt_entry_t *)nvlog_peek_head(ckptlog);
}

void ckptlog_cleanup(ts_ckptlog_t *ckptlog, unsigned long until_clk)
{
	/* WARNING: This is not a thread-reentrant function.
	 * TODO: This does not guarantee a crash consistency
	 * of nvm_free() operations.
	 */
	ts_ptr_set_t *free_set;
	ts_act_vhdr_t *p_act_vhdr;
	ts_act_hdr_struct_t *ahs;
	unsigned long tombstone_clk;
	unsigned int i, first, nfree;

	free_set = ckptlog->free_set;
	/* Check if it is already freed. */
	if (unlikely(free_set == NULL || free_set->ptrs == NULL))
		return;

	/* Clean up */
	for (i = 0, first = 0, nfree = 0; i < free_set->num_ptrs; ++i) {
		p_act_vhdr = (ts_act_vhdr_t *)free_set->ptrs[i];
		ts_assert(p_act_vhdr->p_lock);

		/* If a free object is tombstone-marked before the until_clk,
		 * deallocate its original master and vheader. */
		tombstone_clk = p_act_vhdr->tombstone_clk;
		if (lt_clock(tombstone_clk, until_clk)) { /* TIME: < */
			ahs = get_org_ahs(p_act_vhdr);
			ts_trace(
				TS_NVMFREE,
				"[%s:%d] NVM_FREE (ORG_ACT): %p (%lx <= %lx) \n",
				__func__, __LINE__, p_act_vhdr->np_org_act,
				tombstone_clk, until_clk);
			nvm_free(ahs);
			port_free((void *)p_act_vhdr);
			nfree++;
		}
		/* Otherwise compact it */
		else {
			free_set->ptrs[first++] = p_act_vhdr;
		}
	}
	ts_assert(free_set->num_ptrs - nfree == first);
	free_set->num_ptrs = first;
}

static int ckptlog_reclaim_x(ts_ckptlog_t *ckptlog, unsigned long watermark)
{
	ts_ckpt_entry_t *ckpt_entry;
	ts_ckpt_entry_hdr_t *ckptlog_hdr;
	volatile ts_act_vhdr_t *p_act_vhdr;
	ts_ptr_set_t *free_set;
	unsigned long wb_limit_clk, reclaimed_clk;
	unsigned long prev_head_cnt, new_head_cnt, old_head_cnt;
	unsigned long reclaimed_bytes, writeback_bytes;

	/* If there is on-going nvm write, wait until it is done. */
	wmb();

	/* Check if it is okay to reclaim */
	if (!*ckptlog->need_reclaim)
		return 0;
	if (!try_lock(&ckptlog->reclaim_lock))
		return 0;

	/*
	 *                              wb_limit = qp2
	 *                                 /     last_ckpt
	 *                                /      /
	 *            head  prev_head    /      /       tail
	 *              \      \        /      /        /
	 *     +---------+====================+===+====+----+
	 *     |         |......|//////|^^^^^^|^^^|____|    |
	 *     +---------+====================+===+====+----+
	 *               ~~~~~~~>      \__________/
	 *               reclaim        delta = qp
	 *                |     ~~~~~~~>
	 *                |     writeback
	 *                |          |
	 *                +----------+
	 *                delta = one or more qp
	 */
	reclaimed_bytes = writeback_bytes = 0;
	old_head_cnt = ckptlog->head_cnt;
	wb_limit_clk = ckptlog->clks->__qp2;

	/* Clean up free pointer array. */
	free_set = ckptlog->free_set;

	/* Reclaim copy objects in [head, prev_head)
	 *
	 * - Checkpointed masters in this range passed at least
	 * one grace perios once they were written back to the
	 * original masters. So the checkpointed masters are not
	 * accessible anymore. Thus, reclaim.
	 */
	prev_head_cnt = ckptlog->prev_head_cnt;
	while ((ckpt_entry = ckptlog_peek_head(ckptlog))) {
		ckptlog_hdr = &ckpt_entry->ckptlog_hdr;

		/* Is it time to write back?
		 *
		 * NOTE: we have to reclaim until where we perform
		 * writeback last reclamation, which is at prev_head_cnt.
		 */
		if (ckptlog->head_cnt == prev_head_cnt) {
			break;
		}

		/* Collect free object */
		if (unlikely(ckptlog_hdr->tombstone)) {
			/* If the reclaiming, checkpointed master has FREE flag,
			 * keep the original master to the free list. Here we
			 * cannot immediately free the original master and its
			 * volatile header because another thread, which is
			 * behind, would try to access the original master to
			 * check if it is the latest checkpointed master or not.
			 *
			 * Once the barrier is over, free the original masters and
			 * their volatile headers in the free list.
			 */
			p_act_vhdr = get_vact_vhdr(ckptlog_hdr->np_org_act);
			ts_assert(p_act_vhdr->p_lock);
			ptrset_push(free_set, (void *)p_act_vhdr);
		}

		/* Reclaim a checkpointed object */
		ckptlog_deq(ckptlog);
	}

	/* Update reclaimed clock */
	if (ckpt_entry) {
		reclaimed_clk = ckpt_entry->ckptlog_hdr.ckpt_s_clk;
		ckptlog->clks->__min_ckpt_reclaimed = reclaimed_clk;
	}

	/* Memorize the reclaim-writeback boundary */
	new_head_cnt = ckptlog->head_cnt;

	/* Write back checkpointed masters between
	 * [prev_head, wb_limit] to their original master
	 *
	 * - Checkpointed masters in this range passed at least
	 * one grace period once they became visible. So it is
	 * guaranteed that all their original masters are not
	 * accessible anymore.
	 * - Thus, it is safe to write back the checkpointed masters
	 * to their original masters only if it is still latest
	 * checkpointed master (i.e., cur_master == me).
	 */
	/* Reclaim checkpoint entries up to wb_clk0 */
	while ((ckpt_entry = ckptlog_peek_head(ckptlog))) {
		ckptlog_hdr = &ckpt_entry->ckptlog_hdr;

		/* Is it time to stop? */
		if (gt_clock(ckptlog_hdr->ckpt_s_clk,
			     wb_limit_clk)) { /* TIME: > */
			break;
		}

		/* If it is the latest and not a tombstone,
		 * we should perform writeback. */
		p_act_vhdr = get_vact_vhdr(ckptlog_hdr->np_org_act);
		if (p_act_vhdr->np_cur_act == ckpt_entry->obj_hdr.obj &&
		    !ckptlog_hdr->tombstone) {
			/* Wait a moment. If we already reclaim enough space,
			 * let's stop here. Deferring checkpoint reclamation
			 * helps to reduce likelihood of writeback. */
			if (unlikely(ckptlog_used(ckptlog) < watermark)) {
				break;
			}

			/* Write back the checkpointed master
			 * to the original master. */
			memcpy_to_nvm((void *)p_act_vhdr->np_org_act,
				      (void *)ckpt_entry->obj_hdr.obj,
				      ckpt_entry->obj_hdr.obj_size);
			/* ... and update np_cur_act. */
			smp_atomic_store(&p_act_vhdr->np_cur_act,
					 p_act_vhdr->np_org_act);
			/* ... and update statistics */
			writeback_bytes += ckpt_entry->obj_hdr.obj_size;
		}

		/* Advance checkpoint log */
		ckptlog_deq(ckptlog);
	}

	/* Memorize the end of writeback, which is the end of
	 * the reclamation in the next. */
	ckptlog->prev_head_cnt = ckptlog->head_cnt;

	/* Revert head to the reclaim-writeback boundary */
	ckptlog->head_cnt = new_head_cnt;

	/* Persist ckptlog */
	ts_assert(new_head_cnt >= old_head_cnt);
	if (new_head_cnt != old_head_cnt) {
		ckptlog_deq_persist(ckptlog);
		reclaimed_bytes = new_head_cnt - old_head_cnt;
		stat_thread_acc(ckptlog->thread, n_ckptlog_reclaimed_bytes,
				reclaimed_bytes);
		ts_trace(
			TS_CKPTLOG,
			"%s [%p]: %ld bytes reclaimed (h=%ld, ph=%ld, t=%ld)\n",
			req2str(*ckptlog->need_reclaim), ckptlog,
			reclaimed_bytes, ckptlog->head_cnt,
			ckptlog->prev_head_cnt, ckptlog->tail_cnt);
	}
	stat_thread_acc(ckptlog->thread, n_ckptlog_writeback_bytes,
			writeback_bytes);

	/* Unlock */
	smp_wmb_tso();
	smp_atomic_store(ckptlog->need_reclaim, 0);
	unlock(&ckptlog->reclaim_lock);
	return 1;
}

void ckptlog_reclaim(ts_ckptlog_t *ckptlog)
{
	int ret;

	switch (*ckptlog->need_reclaim) {
	case RECLAIM_CKPTLOG_BEST_EFFORT:
		ret = ckptlog_reclaim_x(ckptlog, ckptlog->log_size + 1);
		if (likely(ret)) {
			stat_thread_inc(ckptlog->thread, n_ckptlog_best_effort);
		}
		break;
	case RECLAIM_CKPTLOG_WRITEBACK:
		ret = ckptlog_reclaim_x(ckptlog, TS_CKPTLOG_LOW_MARK);
		if (likely(ret)) {
			stat_thread_inc(ckptlog->thread, n_ckptlog_writeback);
		}
		break;
	case RECLAIM_CKPTLOG_WRITEBACK_ALL:
		ret = ckptlog_reclaim_x(ckptlog, 0);
		if (likely(ret)) {
			stat_thread_inc(ckptlog->thread, n_ckptlog_writeback);
		}
		break;
	case 0:
		/* Not requested! Do nothing. */
		break;
	default:
		ts_assert(0 && "Never be here!");
	};
}

void ckptlog_flush(ts_ckptlog_t *ckptlog)
{
	if (*ckptlog->need_reclaim) {
		ckptlog_reclaim(ckptlog);
	}

	while (ckptlog->head_cnt != ckptlog->tail_cnt) {
		int count = 0;
		request_ckptlog_reclaim(RECLAIM_CKPTLOG_WRITEBACK_ALL);
		do {
			port_cpu_relax_and_yield();
			smp_mb();
			count++;
			if (count == 1000) {
				request_ckptlog_reclaim(
					RECLAIM_CKPTLOG_WRITEBACK_ALL);
				count = 0;
			}
		} while (!*ckptlog->need_reclaim);
		ckptlog_reclaim(ckptlog);
	}
}
