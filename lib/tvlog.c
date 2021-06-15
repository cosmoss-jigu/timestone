#ifndef __KERNEL__
#include "timestone.h"
#else
#include <linux/timestone.h>
#endif

#include "util.h"
#include "debug.h"
#include "nvm.h"
#include "port.h"
#include "tvlog.h"
#include "ckptlog.h"
#include "oplog.h"
#include "clock.h"
#include "qp.h"

#define TS_FREE_POSION ((unsigned char)(0xbd))

tvlog_region_allocator_t g_lr;
void *g_start_addr __read_mostly;
void *g_end_addr __read_mostly;

static inline unsigned int tvlog_index(unsigned long cnt)
{
	return cnt & ~TS_TVLOG_MASK;
}

static inline void *tvlog_at(ts_tvlog_t *log, unsigned long cnt)
{
	return (void *)&log->buffer[tvlog_index(cnt)];
}

static inline unsigned int add_extra_padding(ts_tvlog_t *tvlog,
					     unsigned int tvlog_size,
					     unsigned int extra_size, int bogus)
{
	unsigned int padding = 0;

	extra_size = extra_size + sizeof(ts_cpy_hdr_struct_t);
	extra_size = align_uint_to_cacheline(extra_size);

	if (bogus == 0 &&
	    tvlog_index(tvlog->tail_cnt + tvlog_size + extra_size) <
		    tvlog_index(tvlog->tail_cnt)) {
		padding = TS_TVLOG_SIZE -
			  tvlog_index(tvlog->tail_cnt + tvlog_size);
	}
	if (padding) {
		ts_assert(tvlog_index(tvlog->tail_cnt + tvlog_size + padding) ==
			  0);
	}

	return padding;
}

static ts_cpy_hdr_struct_t *tvlog_alloc(ts_tvlog_t *tvlog,
					unsigned int obj_size, int *bogus)
{
	ts_cpy_hdr_struct_t *chs;
	ts_wrt_set_struct_t *wss;
	unsigned int entry_size;
	unsigned int extra_pad;

	/* Make entry_size cacheline aligned */
	entry_size = obj_size + sizeof(ts_cpy_hdr_struct_t);
	entry_size = align_uint_to_cacheline(entry_size);
	ts_assert(entry_size < TS_TVLOG_SIZE);

	/* If an allocation wraps around the end of a tvlog,
	 * insert a bogus object to prevent such case in real
	 * object access.
	 *
	 *   +-----------------------------------+
	 *   |                             |bogus|
	 *   +-----------------------------------+
	 *      \                           \
	 *       \                           +- 1) tvlog->tail_cnt
	 *        +- 2) tvlog->tail_cnt + entry_size
	 */

	if (unlikely(tvlog_index(tvlog->tail_cnt + entry_size) <
		     tvlog_index(tvlog->tail_cnt))) {
		unsigned int bogus_size;

		chs = tvlog_at(tvlog, tvlog->tail_cnt);
		memset(chs, 0, sizeof(*chs));
		bogus_size = TS_TVLOG_SIZE - tvlog_index(tvlog->tail_cnt);
		chs->obj_hdr.padding_size = bogus_size;
		chs->obj_hdr.type = TYPE_BOGUS;

		tvlog->tail_cnt += bogus_size;
		ts_assert(tvlog_index(tvlog->tail_cnt) == 0);
		*bogus = 1;
	}
	ts_assert(tvlog_index(tvlog->tail_cnt) <
		  tvlog_index(tvlog->tail_cnt + entry_size));
	extra_pad = add_extra_padding(tvlog, entry_size, sizeof(*wss), *bogus);
	entry_size += extra_pad;

	/*
	 *   +--- ts_cpy_hdr_struct_t ---+
	 *  /                                \
	 * +---------------------------------------------+----
	 * | ts_cpy_hdr_t | ts_obj_hdr  | copy obj | ...
	 * +---------------------------------------------+----
	 */
	chs = tvlog_at(tvlog, tvlog->tail_cnt);
	memset(chs, 0, sizeof(*chs));
	chs->cpy_hdr.__wrt_clk = MAX_VERSION;
	chs->obj_hdr.obj_size = obj_size;
	chs->obj_hdr.padding_size = entry_size - obj_size;

	return chs;
}

static inline ts_wrt_set_struct_t *tvlog_at_wss(ts_tvlog_t *log,
						unsigned long cnt)
{
	ts_wrt_set_struct_t *wss;
	wss = (ts_wrt_set_struct_t *)tvlog_at(log, cnt);
	ts_assert(wss->chs.obj_hdr.type == TYPE_WRT_SET);
	return wss;
}

static inline ts_cpy_hdr_struct_t *tvlog_at_chs(ts_tvlog_t *tvlog,
						unsigned long cnt)
{
	ts_cpy_hdr_struct_t *chs;
	chs = (ts_cpy_hdr_struct_t *)tvlog_at(tvlog, cnt);
	ts_assert(chs == align_ptr_to_cacheline(chs));
	return chs;
}

ts_cpy_hdr_struct_t *tvlog_append_begin(ts_tvlog_t *tvlog,
					volatile ts_act_vhdr_t *p_act_vhdr,
					unsigned int obj_size, int *bogus)
{
	ts_cpy_hdr_struct_t *chs;
	*bogus = 0;

	/* Add a write set if it is not allocated */
	if (unlikely(!tvlog->cur_wrt_set)) {
		ts_wrt_set_t *ws;
		ts_assert(tvlog_index(tvlog->tail_cnt) <
			  tvlog_index(tvlog->tail_cnt + sizeof(*ws)));
		chs = tvlog_alloc(tvlog, sizeof(*ws), bogus);
		ws = (ts_wrt_set_t *)chs->obj_hdr.obj;
		chs->cpy_hdr.p_ws = ws;
		chs->obj_hdr.type = TYPE_WRT_SET;
		ws->wrt_clk = MAX_VERSION;
		ws->pending_wrt_clk = MAX_VERSION;
		ws->num_objs = 0;
		ws->start_tail_cnt = tvlog->tail_cnt;
		ws->thread = tvlog_to_thread(tvlog);
		tvlog->cur_wrt_set = ws;
		tvlog->tail_cnt += get_entry_size(chs);
		ts_assert(*bogus == 0);
	}

	/* allocate an object */
	chs = tvlog_alloc(tvlog, obj_size, bogus);
	chs->cpy_hdr.p_ws = tvlog->cur_wrt_set;
	chs->cpy_hdr.p_act_vhdr = p_act_vhdr;
	chs->obj_hdr.type = TYPE_COPY;

#ifdef TS_ENABLE_STATS
	if (chs->obj_hdr.type == TYPE_COPY) {
		stat_thread_acc(tvlog_to_thread(tvlog),
				n_tvlog_cpobj_written_bytes,
				get_entry_size(chs));
	}
#endif
	return chs;
}

#define ws_for_each(tvlog, ws, obj_idx, log_cnt)                               \
	for ((obj_idx) = 0, (log_cnt) = ws_iter_begin(ws);                     \
	     (obj_idx) < (ws)->num_objs;                                       \
	     ++(obj_idx), (log_cnt) = ws_iter_next(log_cnt, chs))

#define ws_for_each_safe(tvlog, ws, obj_idx, log_cnt, log_cnt_next)            \
	for ((obj_idx) = 0, (log_cnt) = ws_iter_begin(ws),                     \
	    (log_cnt_next) =                                                   \
		     ws_iter_next(log_cnt, tvlog_at_chs(tvlog, log_cnt));      \
	     (obj_idx) < (ws)->num_objs;                                       \
	     ++(obj_idx), (log_cnt) = (log_cnt_next),                          \
	    (log_cnt_next) =                                                   \
		     ws_iter_next(log_cnt, tvlog_at_chs(tvlog, log_cnt)))

static inline unsigned long ws_iter_begin(ts_wrt_set_t *ws)
{
	ts_cpy_hdr_struct_t *chs;

	chs = obj_to_chs(ws, TYPE_WRT_SET);
	return ws->start_tail_cnt + get_entry_size(chs);
}

static inline unsigned long ws_iter_next(unsigned long iter,
					 ts_cpy_hdr_struct_t *chs)
{
	return iter + get_entry_size(chs);
}

static void ws_move_lock_to_copy(ts_tvlog_t *tvlog, ts_ptr_set_t *free_set)
{
	ts_wrt_set_t *ws;
	ts_cpy_hdr_struct_t *chs;
	void *p_act;
	unsigned long cnt;
	unsigned long wrt_clk_next;
	unsigned int num_free;
	unsigned int i;

	ws = tvlog->cur_wrt_set;
	num_free = free_set->num_ptrs;

	ws_for_each (tvlog, ws, i, cnt) {
		volatile ts_act_vhdr_t *p_act_vhdr;
		volatile void *p_old_copy, *p_old_copy2;

		chs = tvlog_at_chs(tvlog, cnt);
		assert_chs_type(chs);
		if (unlikely(chs->obj_hdr.type == TYPE_BOGUS)) {
			continue;
		}
		p_act_vhdr = chs->cpy_hdr.p_act_vhdr;
		p_act = (void *)p_act_vhdr->np_org_act;
		ts_assert(chs->obj_hdr.type == TYPE_COPY);
		ts_assert(p_act_vhdr->p_lock == chs->obj_hdr.obj);

		/* If an object is free()-ed, change its type. */
		if (unlikely(num_free > 0 &&
			     ptrset_is_member(free_set, p_act))) {
			chs->obj_hdr.type = TYPE_FREE;
			--num_free;
			/* Freed copy should not be accessible
			 * from the version chain.*/
			continue;
		}

		/* If the size of copied object is zero, that is
		 * for try_lock_const() so we do not insert it
		 * to the version list. */
		if (!chs->obj_hdr.obj_size)
			continue;

		/* Set wrt_clk_prev to MAX_VERSION to prevent
		 * reclamation in a best effort reclamation of a tvlog.
		 * Later when a newer version is inserted,
		 * wrt_clk_prev will be updated in ws_unlock() */
		chs->cpy_hdr.wrt_clk_prev = MAX_VERSION;

		/* Move a locked object to the version chain
		 * of an actual object. */
		p_old_copy = p_act_vhdr->p_copy;
		while (1) {
			/* Initialize p_copy and wrt_clk_next. */
			chs->cpy_hdr.p_copy = p_old_copy;
			if (p_old_copy == NULL) {
				/* If we do NOT have an older version,
				 * set wrt_clk_next to MIN_VERSION
				 * so we can stop version chain traversal
				 * and fall back to np_cur_master. */
				chs->cpy_hdr.wrt_clk_next = MIN_VERSION;
			} else {
				/* If we have an older version,
				 * that is our wrt_clk_next. */
				wrt_clk_next = get_raw_wrt_clk(
					vobj_to_chs(p_old_copy, TYPE_COPY));
				chs->cpy_hdr.wrt_clk_next = wrt_clk_next;
			}
			ts_assert(chs->cpy_hdr.wrt_clk_next != MAX_VERSION);
			smp_wmb_tso();

			/* Since p_copy of p_act can be set to NULL upon
			 * reclaim, we should update it using smp_cas(). */
			if (smp_cas_v(&p_act_vhdr->p_copy, p_old_copy,
				      chs->obj_hdr.obj, p_old_copy2))
				break;

			/* smp_cas_v() failed. Retry.
			 * p_old_copy2 is updated by smp_cas_v(). */
			p_old_copy = p_old_copy2;
		}

		/* Now we succeed in moving a new copy to the version chain. */
	}

	/* We should be able to find out all freed pointers. */
	ts_assert(num_free == 0);
}

static void ws_unlock_commit(ts_tvlog_t *tvlog)
{
	ts_wrt_set_t *ws;
	ts_cpy_hdr_struct_t *chs, *old_chs;
	ts_act_vhdr_t *p_act_vhdr;
	void *p_old_copy;
	unsigned long cnt;
	volatile unsigned long wrt_clk;
	unsigned int i;

	ws = tvlog->cur_wrt_set;
	wrt_clk = ws->wrt_clk;
	ws_for_each (tvlog, ws, i, cnt) {
		chs = tvlog_at_chs(tvlog, cnt);
		assert_chs_type(chs);

		switch (chs->obj_hdr.type) {
		case TYPE_COPY:
			/* Mark version for version traversal and reclamation. */
			smp_atomic_store(&chs->cpy_hdr.__wrt_clk, wrt_clk);
			smp_mb();
			smp_wmb_tso();

			/* Unlock */
			p_act_vhdr = (ts_act_vhdr_t *)chs->cpy_hdr.p_act_vhdr;
			ts_assert(p_act_vhdr->p_lock == chs->obj_hdr.obj);
			if (!smp_cas(&p_act_vhdr->p_lock, chs->obj_hdr.obj,
				     NULL)) {
				ts_assert(0 && "Not possbile");
			}

			/* Update wrt_clk_prev of p_old_copy
			 * so p_old_copy can be reclaimed
			 * by a best effort reclamation of tvlog. */
			p_old_copy = (void *)chs->cpy_hdr.p_copy;
			if (p_old_copy) {
				old_chs = vobj_to_chs(p_old_copy, TYPE_COPY);
				old_chs->cpy_hdr.wrt_clk_prev = wrt_clk;
			}
			break;
		case TYPE_FREE:
			/* Mark version, which represent an object is committed. */
			smp_atomic_store(&chs->cpy_hdr.__wrt_clk, wrt_clk);
			smp_mb();
			smp_wmb_tso();

			/* Unlink this from the version chain for ease of debugging*/
			p_act_vhdr = (ts_act_vhdr_t *)chs->cpy_hdr.p_act_vhdr;
			ts_assert(p_act_vhdr->p_lock == chs->obj_hdr.obj);
			chs->cpy_hdr.p_copy = NULL;
			break;
		case TYPE_BOGUS:
			break;
		default:
			ts_assert(0 && "Incorrect object type");
			break;
		}
	}
}

static void ws_unlock_abort(ts_tvlog_t *tvlog)
{
	ts_wrt_set_t *ws;
	ts_cpy_hdr_struct_t *chs;
	ts_act_vhdr_t *p_act_vhdr;
	unsigned long cnt;
	unsigned int i;

	ws = tvlog->cur_wrt_set;
	ws_for_each (tvlog, ws, i, cnt) {
		chs = tvlog_at_chs(tvlog, cnt);
		assert_chs_type(chs);

		switch (chs->obj_hdr.type) {
		case TYPE_COPY:
		case TYPE_FREE:
			/* Unlock */
			p_act_vhdr = (ts_act_vhdr_t *)chs->cpy_hdr.p_act_vhdr;
			ts_assert(p_act_vhdr->p_lock == chs->obj_hdr.obj);
			if (!smp_cas(&p_act_vhdr->p_lock, chs->obj_hdr.obj,
				     NULL)) {
				ts_assert(0 && "Not possbile");
			}
			break;
		case TYPE_BOGUS:
			break;
		default:
			ts_assert(0 && "Incorrect object type");
			break;
		}
	}
}

static void tvlog_try_request_reclaim_if_needed(ts_tvlog_t *tvlog)
{
	unsigned long used;

	/* If it needs to trigger reclamation */
	used = tvlog_used(tvlog);
	if (unlikely(used > TS_TVLOG_LOW_MARK)) {
		if (unlikely(used > TS_TVLOG_HIGH_MARK)) {
			request_tvlog_reclaim(RECLAIM_TVLOG_CKPT);
		} else {
			request_tvlog_reclaim(RECLAIM_TVLOG_BEST_EFFORT);
		}
	}
}

void tvlog_commit(ts_tvlog_t *tvlog, ts_oplog_t *oplog, ts_ptr_set_t *free_set,
		  unsigned long local_clk, ts_op_info_t *op_info)
{
	unsigned long pending_wrt_clk;

	ts_assert(tvlog->cur_wrt_set);
	ts_assert(obj_to_chs(tvlog->cur_wrt_set, TYPE_WRT_SET)->obj_hdr.type ==
		  TYPE_WRT_SET);

	/* Move a committed object to its version chain */
	ws_move_lock_to_copy(tvlog, free_set);

	/* Get the write clock */
	smp_atomic_store(&tvlog->cur_wrt_set->pending_wrt_clk,
			 new_clock(local_clk));
	advance_clock();
	pending_wrt_clk = tvlog->cur_wrt_set->pending_wrt_clk;

	/* Persistently commit oplog */
	oplog_enq(oplog, local_clk, pending_wrt_clk, op_info);
	oplog_enq_persist(oplog);

	/* Make them public atomically */
	smp_wmb();
	smp_atomic_store(&tvlog->cur_wrt_set->wrt_clk, pending_wrt_clk);
	smp_mb();

	/* Unlock objects with marking wrt_clk*/
	ws_unlock_commit(tvlog);

	/* Make changes visible immediately */
	smp_mb();

	/* Request checkpointing if oplog is over
	 * high water mark even after its reclamation. */
	if (likely(!oplog_try_request_ckpt_if_needed(oplog))) {
		tvlog_try_request_reclaim_if_needed(tvlog);
	}

	/* Clean up */
	tvlog->cur_wrt_set = NULL;
	ptrset_reset(free_set);
}

void tvlog_abort(ts_tvlog_t *tvlog, ts_ptr_set_t *free_set)
{
	/* Unlock objects without marking wrt_clk */
	ws_unlock_abort(tvlog);

	/* Reset the current write set */
	tvlog->tail_cnt = tvlog->cur_wrt_set->start_tail_cnt;
	tvlog->cur_wrt_set = NULL;
	ptrset_reset(free_set);
}

static int is_copy_latest(ts_cpy_hdr_struct_t *chs)
{
	volatile ts_act_vhdr_t *p_act_vhdr;

	p_act_vhdr = chs->cpy_hdr.p_act_vhdr;
	return p_act_vhdr->p_copy == chs->obj_hdr.obj;
}

static void try_detach_obj(ts_cpy_hdr_struct_t *chs)
{
	volatile ts_act_vhdr_t *p_act_vhdr;
	void *p_copy;

	ts_assert(chs->obj_hdr.status != STATUS_DETACHED);

	chs->obj_hdr.status = STATUS_DETACHED;

	/* Copy to the actual object when it is the latest copy */
	p_act_vhdr = chs->cpy_hdr.p_act_vhdr;
	p_copy = (void *)chs->obj_hdr.obj;
	if (p_act_vhdr->p_copy != p_copy)
		return;

	/* Set p_copy of the actual object to NULL */
	if (smp_cas(&p_act_vhdr->p_copy, p_copy, NULL)) {
		/* Succeed in detaching the object */
		return;
	}
}

static inline int is_at_borderline(ts_cpy_hdr_struct_t *chs,
				   unsigned long ckpt_s_clk,
				   unsigned long last_ckpt_clk)
{
	/*
	 * newer (larger)
	 *
	 *  +------------------+
	 *  |   wrt_clk_prev   |
	 *  +------------------+
	 *            | <----------- ckpt_s_clk
	 *  +------------------+
	 *  |  wrt_clk of chs  |
	 *  +------------------+
	 *            |
	 *           ... <---------- last_ckpt_clk
	 *            |
	 *
	 * older (smaller)
	 */
	unsigned long wrt_clk, wrt_clk_prev;

	ts_assert(chs->cpy_hdr.__wrt_clk != MAX_VERSION);
	wrt_clk = get_raw_wrt_clk(chs);
	ts_assert(wrt_clk != MAX_VERSION);
	wrt_clk_prev = chs->cpy_hdr.wrt_clk_prev;

	if (wrt_clk_prev == MAX_VERSION) {
		return (lt_clock(wrt_clk, ckpt_s_clk) /*&&
			gte_clock(wrt_clk, last_ckpt_clk)*/); /* ckpt fails due to gte check*/
	} else {
		ts_assert(wrt_clk_prev != MAX_VERSION) return gte_clock(
			chs->cpy_hdr.wrt_clk_prev, ckpt_s_clk) // TIME: >=
			&& lt_clock(wrt_clk, ckpt_s_clk) // TIME: <
			&& gte_clock(wrt_clk, last_ckpt_clk); // TIME: >=
	}
}

static ts_cpy_hdr_struct_t *get_ckpt_chs(ts_cpy_hdr_struct_t *chs,
					 unsigned long ckpt_s_clk,
					 unsigned long last_ckpt_clk)
{
	volatile void *p_copy;
	volatile ts_act_vhdr_t *p_act_vhdr;
	unsigned long wrt_clk;

	p_act_vhdr = chs->cpy_hdr.p_act_vhdr;
	p_copy = p_act_vhdr->p_copy;
	if (likely(p_copy)) {
		do {
			chs = vobj_to_chs(p_copy, TYPE_COPY);
			wrt_clk = get_raw_wrt_clk(chs);

			/* TODO: simple skip to the next cpy if its wrt_clk is 
			 * not yet updated or retry after the wrt_clk is updated*/
			if (wrt_clk == MAX_VERSION) {
				p_copy = chs->cpy_hdr.p_copy;
				continue;
			}

			if (lt_clock(wrt_clk, ckpt_s_clk)) /* TIME: < */ {
				return chs;
			}

			if (unlikely(lt_clock(chs->cpy_hdr.wrt_clk_next,
					      last_ckpt_clk))) /* TIME: < */ {
				break;
			}
			p_copy = chs->cpy_hdr.p_copy;
		} while (p_copy);
	}
	return NULL;
}

static void _dbg_assert_is_at_borderline(const char *f, const int l,
					 ts_cpy_hdr_struct_t *chs,
					 unsigned long ckpt_s_clk,
					 unsigned long last_ckpt_clk)
{
#ifdef TS_ENABLE_ASSERT
	int ret;
	volatile void *p_copy;
	volatile ts_act_vhdr_t *p_act_vhdr;
	ts_cpy_hdr_struct_t *ckpt_chs, *chs_latest;
	ckpt_chs = get_ckpt_chs(chs, ckpt_s_clk, last_ckpt_clk);
	ret = is_at_borderline(chs, ckpt_s_clk, last_ckpt_clk);

	if (ret != (chs == ckpt_chs)) {
		if (lt_clock(chs->cpy_hdr.__wrt_clk, last_ckpt_clk)) {
			return;
		}
		ts_trace(TS_DUMP, "===========================\n");
		ts_dbg_dump_version_chain(f, l, chs, last_ckpt_clk);
		ts_trace(TS_DUMP, "---------------------------\n");
		ts_dbg_dump_cpy_hdr_struct("chs", l, chs);
		ts_trace(TS_DUMP, "---------------------------\n");
		ts_dbg_dump_cpy_hdr_struct("ckpt_chs", l, ckpt_chs);
		ts_trace(TS_DUMP, "---------------------------\n");
		ts_trace(TS_DUMP, "          ckpt_s_clk:    %lu\n", ckpt_s_clk);
		ts_trace(TS_DUMP, "          last_ckpt_clk: %lu\n",
			 last_ckpt_clk);

		p_act_vhdr = chs->cpy_hdr.p_act_vhdr;
		p_copy = p_act_vhdr->p_copy;
		chs_latest = vobj_to_chs(p_copy, TYPE_COPY);

		ts_trace(TS_ERROR,
			 "************ ASSERT INFO ******************\n");
		ts_trace(TS_ERROR, "ret= %d\n", ret);
		ts_trace(TS_ERROR, "chs= %p, ckpt_chs= %p, chs_latest= %p \n",
			 chs, ckpt_chs, chs_latest);
		ts_trace(TS_ERROR, "ckpt_s_clk= %ld\n", ckpt_s_clk);
		ts_trace(TS_ERROR, "last_ckpt_clk= %ld\n", last_ckpt_clk);
		ts_trace(TS_ERROR, "wrt_clk_prev= %ld\n",
			 chs->cpy_hdr.wrt_clk_prev);
		ts_trace(TS_ERROR, "wrt_clk= %ld\n", chs->cpy_hdr.__wrt_clk);
		ts_trace(TS_ERROR,
			 "*******************************************\n");

		ts_assert(0 && "ckpt_chs != chs");
	}
#endif
}

static void _dbg_poision_wrt_set(ts_tvlog_t *tvlog, unsigned long start_cnt)
{
#ifdef TS_ENABLE_FREE_POISIONING
	ts_wrt_set_t *ws;
	ts_wrt_set_struct_t *wss;
	ts_cpy_hdr_struct_t *chs = NULL;
	unsigned long cnt, cnt_next;
	unsigned int index, i;

	index = tvlog_index(start_cnt);
	wss = tvlog_at_wss(tvlog, index);
	ws = &(wss->wrt_set);

	ws_for_each_safe (tvlog, ws, i, cnt, cnt_next) {
		chs = tvlog_at_chs(tvlog, cnt);
		memset((void *)chs->obj_hdr.obj, TS_FREE_POSION,
		       chs->obj_hdr.obj_size);
		memset((void *)chs, TS_FREE_POSION, sizeof(*chs));
	}
#endif
}

static void _dbg_assert_is_latest_copy(ts_cpy_hdr_struct_t *chs,
				       unsigned long last_ckpt_clk,
				       unsigned long ckpt_s_clk, int mode,
				       int type)
{
#ifdef TS_ENABLE_ASSERT
	volatile ts_act_vhdr_t *p_act_vhdr;
	volatile void *p_copy;
	void *obj;

	p_act_vhdr = chs->cpy_hdr.p_act_vhdr;
	p_copy = p_act_vhdr->p_copy;
	obj = chs->obj_hdr.obj;
	if (obj == p_copy) {
		ts_trace(TS_INFO, "reclaim mode= %04x\n", mode);
		ts_trace(TS_INFO, "reclaim type= %04x\n", type);
		ts_trace(TS_INFO, "latest cpy= %p\n", p_copy);
		ts_trace(TS_INFO, "reclai cpy= %p\n", obj);
		ts_trace(TS_INFO, "vhdr= %p\n", p_act_vhdr);
		ts_trace(TS_INFO, "org master= %p\n", p_act_vhdr->np_org_act);
		ts_trace(TS_INFO, "cur master= %p\n", p_act_vhdr->np_cur_act);
		ts_trace(TS_INFO, "__last_ckpt_clk__= %ld\n", last_ckpt_clk);
		ts_trace(TS_INFO, "___ckpt_s_clk____= %ld\n", ckpt_s_clk);
		ts_trace(TS_INFO, "_____wrt_clk_____= %ld\n",
			 chs->cpy_hdr.__wrt_clk);
		ts_trace(TS_INFO, "___wrt_clk_nxt___= %ld\n",
			 chs->cpy_hdr.wrt_clk_next);
		ts_trace(TS_INFO, "___wrt_clk_prv___= %ld\n",
			 chs->cpy_hdr.wrt_clk_prev);
		ts_assert(obj != p_copy);
	}
#endif
}

static int tvlog_reclaim_x(ts_tvlog_t *tvlog, ts_ckptlog_t *ckptlog, int mode)
{
	ts_wrt_set_t *ws;
	ts_wrt_set_struct_t *wss;
	ts_cpy_hdr_struct_t *chs = NULL;
	ts_act_vhdr_t *p_act_vhdr;
	ts_ckpt_entry_t *ckpt_entry;
	ts_sys_clks_t *clks;
	unsigned long cnt, ws_start_cnt, start_cnt, tail_cnt;
	unsigned long wrt_clk, ckpt_s_clk, last_ckpt_clk;
	unsigned long prev_head_cnt, new_head_cnt, old_head_cnt;
	unsigned long reclaimed_bytes, ckpt_bytes;
	unsigned int index, i;
	int is_ckptlog_dirty = 0;

	/* lock */
	if (!*tvlog->need_reclaim)
		return 0;
	if (!try_lock(&tvlog->reclaim_lock))
		return 0;

	/*
	 *                                             ckpt_s (qp0)
	 *                                             tail
	 *            head                  prev_head  /
	 *              \                      /      /
	 *     +---------+====================+======+-+----+
	 *     |         |....................|//////| |    |
	 *     +---------+====================+======+-+----+
	 *               ~~~~~~~~~~~~~~~~~~~~~>
	 *               reclaim
	 *                  |                 ~~~~~~>
	 *                  |                 checkpoint
	 *                  |                    |
	 *                  +--------------------+
	 *                  delta = one or more qp
	 */
	clks = tvlog->clks;
	ckpt_s_clk = clks->__qp0;
	last_ckpt_clk = clks->__last_ckpt;

	start_cnt = old_head_cnt = tvlog->head_cnt;
	tail_cnt = tvlog->tail_cnt;
	reclaimed_bytes = ckpt_bytes = 0;

	/* Reclaim copy objects between [head, prev_head)
	 *
	 * - Nobody accesses this copy because a thread accesses
	 * either of the new checkpointed master or a newer copy
	 * in transient version log.
	 * Thus, we can safely reclaim this copy. */
	prev_head_cnt = tvlog->prev_head_cnt;
	while (start_cnt < tail_cnt) {
		ws_start_cnt = start_cnt;

		/* Is it time to checkpoint? */
		if (unlikely(start_cnt == prev_head_cnt)) {
			break;
		}

		/* Is it committed? */
		index = tvlog_index(start_cnt);
		wss = tvlog_at_wss(tvlog, index);
		ws = &(wss->wrt_set);
		if (unlikely(ws->wrt_clk == MAX_VERSION)) {
			break;
		}
		ws_for_each (tvlog, ws, i, cnt) {
			chs = tvlog_at_chs(tvlog, cnt);
			assert_chs_type(chs);
			ts_assert(cnt <= tvlog->tail_cnt);

			switch (chs->obj_hdr.type) {
			case TYPE_COPY:
				/* In the best effort reclamation, we can only
				 * reclaim copies of which wrt_clk_prev is
				 * older than qp0 meaning that those copies
				 * are guaranteed not accessible. When reclaiming,
				 * ckpt_s_clk is qp0. */
				if (mode == RECLAIM_TVLOG_BEST_EFFORT &&
				    gte_clock(chs->cpy_hdr.wrt_clk_prev,
					      ckpt_s_clk)) { /* TIME: >= */
					start_cnt = ws_start_cnt;
					/* Revert start_cnt to the beginning
					 * of the write set because we should
					 * reclaim a whole write set. */
					start_cnt = ws_start_cnt;
					goto stop_checkpoint_step1;
				}
				_dbg_assert_is_latest_copy(chs, mode,
							   last_ckpt_clk,
							   ckpt_s_clk,
							   TYPE_COPY);
				/* Reclaim this copy */
				break;
			case TYPE_BOGUS:
				/* Reclaim this copy */
				break;
			case TYPE_FREE:
				/* This CHS is already tombstone-marked in the previous
				 * best effort reclamation (i.e, stop_checkpoint_step1)
				 * so just skip. */
				if (unlikely(chs->obj_hdr.status ==
					     STATUS_TOMBSTONE_MARKED))
					break;

				/* Even if the original master is the current master,
				 * it does not guarantee that there is no checkpointed
				 * mastser. There could be checkpointed masters that
				 * havn't been reclaimed yet since the most recent one
				 * is written back to the original master setting
				 * the current master to the original master. There is
				 * no race condition here because we still hold a lock
				 * of the freeing object. */
				p_act_vhdr = (ts_act_vhdr_t *)
						     chs->cpy_hdr.p_act_vhdr;
				ts_assert(p_act_vhdr->p_lock);
				if (p_act_vhdr->np_cur_act ==
				    p_act_vhdr->np_org_act) {
					/* Replace __wrt_clk to tombstone_clk,
					 * which is ckpt_s_clk. */
					p_act_vhdr->tombstone_clk = ckpt_s_clk;
					smp_wmb_tso();

					/* ... then append its tombstone. */
					ckptlog_enq_tombstone(ckptlog,
							      ws->wrt_clk,
							      ckpt_s_clk, chs);
					is_ckptlog_dirty = 1;

				}
				/* Otherwise, mark TOMBSTONE status to
				 * the current checkpointed master. */
				else {
					const char TOMBSTONE = 1;
					volatile char *np_tombstone;

					ckpt_entry = vobj_to_ckpt_ent(
						p_act_vhdr->np_cur_act);
					np_tombstone = &ckpt_entry->ckptlog_hdr
								.tombstone;
					ts_assert(*np_tombstone == 0);
					smp_atomic_store(np_tombstone,
							 TOMBSTONE);
					clwb(np_tombstone);
					ts_trace(
						TS_NVMFREE,
						"[%s:%d] MARK_TOMBSTONE (ORG_ACT): %p\n",
						__func__, __LINE__,
						p_act_vhdr->np_cur_act);
				}
				/* Change status of chs to prevent marking tombstone twice. */
				chs->obj_hdr.status = STATUS_TOMBSTONE_MARKED;
				_dbg_assert_is_latest_copy(chs, mode,
							   last_ckpt_clk,
							   ckpt_s_clk,
							   TYPE_FREE);
				/* Reclaim this copy */
				break;
			default:
				ts_assert(0 && "Never be here!");
				break;
			}
		}
		_dbg_poision_wrt_set(tvlog, ws_start_cnt);

		start_cnt = cnt;
		ts_assert(start_cnt <= tvlog->tail_cnt);
	}
stop_checkpoint_step1:

	/* Memorize the reclaim-checkpoint boundary */
	new_head_cnt = start_cnt;

	/* Checkpoint copy objects between [prev_head_cnt, ckpt_s_clk]
	 *
	 * - Nobody accesses this copy because a thread accesses either
	 * of the new checkpointed master or a newer copy in transient
	 * version log. Thus, we can safely reclaim this copy. */
	while (start_cnt < tail_cnt) {
		ws_start_cnt = start_cnt;
		index = tvlog_index(start_cnt);
		wss = tvlog_at_wss(tvlog, index);
		ws = &(wss->wrt_set);

		/* Is it committed? */
		wrt_clk = ws->wrt_clk;
		if (gte_clock(wrt_clk, ckpt_s_clk) ||
		    wrt_clk == MAX_VERSION) { /* TIME: >= */
			ts_trace(TS_TVLOG,
				 "== TVLog Reclaim: ws->wrt_clk= %ld \n",
				 wrt_clk);
			break;
		}
		ws_for_each (tvlog, ws, i, cnt) {
			chs = tvlog_at_chs(tvlog, cnt);
			assert_chs_type(chs);
			ts_assert(chs->cpy_hdr.__wrt_clk != MAX_VERSION);
			ts_assert(cnt <= tvlog->tail_cnt);

			/* For the copy ... */
			if (unlikely(chs->obj_hdr.type != TYPE_COPY)) {
				continue;
			}

			/* If this copy is one that is the nearest
			 * to the checkpoint clock, ckpt_s_clk,
			 * checkpoint the copy. Otherwise, we can skip
			 * because other thread having the nearest copy
			 * will perform checkpoint. */

			if (chs->obj_hdr.status != STATUS_DETACHED) {
				ts_assert(chs->cpy_hdr.__wrt_clk !=
					  MAX_VERSION);
				_dbg_assert_is_at_borderline(__func__, __LINE__,
							     chs, ckpt_s_clk,
							     last_ckpt_clk);
				if (is_at_borderline(chs, ckpt_s_clk,
						     last_ckpt_clk)) {
					if (mode == RECLAIM_TVLOG_CKPT) {
						ckptlog_enq(ckptlog, wrt_clk,
							    ckpt_s_clk, chs);
						try_detach_obj(chs);
						ckpt_bytes += sizeof_chs(chs);
						is_ckptlog_dirty = 1;
					} else {
						/* Revert start_cnt to the beginning
						 * of the write set because we should
						 * reclaim a whole write set. */
						start_cnt = ws_start_cnt;
						goto stop_checkpoint_step2;
					}
				}
			}
		}
		start_cnt = cnt;
		ts_assert(start_cnt <= tvlog->tail_cnt);
	}
stop_checkpoint_step2:

	/* Memorize the end of writeback, which is the end of
	 * the reclamation in the next. */
	tvlog->prev_head_cnt = start_cnt;

	/* Revert head to the reclaim-checkpoint boundary */
	tvlog->head_cnt = new_head_cnt;

	/* Persist checkpoint log if we wrote something */
	if (is_ckptlog_dirty) {
		ckptlog_enq_persist(ckptlog);
	}

	/* If we cannot reclaim tvlog using best effort approach,
	 * trigger checkpointing in advance to avoid blocking. */
	ts_assert(new_head_cnt >= old_head_cnt);
	if (unlikely(new_head_cnt == old_head_cnt &&
		     tvlog_used(tvlog) > TS_TVLOG_LOW_MARK &&
		     mode == RECLAIM_TVLOG_BEST_EFFORT)) {
		request_tvlog_reclaim(RECLAIM_TVLOG_CKPT);
	}
	if (new_head_cnt != old_head_cnt) {
		reclaimed_bytes = new_head_cnt - old_head_cnt;
		stat_thread_acc(tvlog_to_thread(tvlog), n_tvlog_reclaimed_bytes,
				reclaimed_bytes);
		ts_trace(
			TS_TVLOG,
			"%s [%p]: %ld bytes reclaimed (h=%ld, ph=%ld, t=%ld)\n",
			req2str(mode), tvlog, reclaimed_bytes, tvlog->head_cnt,
			tvlog->prev_head_cnt, tvlog->tail_cnt);
	}
	stat_thread_acc(tvlog_to_thread(tvlog), n_tvlog_ckpt_bytes, ckpt_bytes);

	/* unlock */
	smp_wmb_tso();
	smp_atomic_store(tvlog->need_reclaim, 0);
	unlock(&tvlog->reclaim_lock);
	return 1;
}

void tvlog_reclaim(ts_tvlog_t *tvlog, ts_ckptlog_t *ckptlog)
{
	int ret;

	switch (*tvlog->need_reclaim) {
	case RECLAIM_TVLOG_BEST_EFFORT:
		ret = tvlog_reclaim_x(tvlog, ckptlog,
				      RECLAIM_TVLOG_BEST_EFFORT);
		if (likely(ret)) {
			stat_thread_inc(tvlog_to_thread(tvlog),
					n_tvlog_best_effort);
		}
		break;
	case RECLAIM_TVLOG_CKPT:
		ret = tvlog_reclaim_x(tvlog, ckptlog, RECLAIM_TVLOG_CKPT);
		if (likely(ret)) {
			stat_thread_inc(tvlog_to_thread(tvlog), n_tvlog_ckpt);
		}
		break;
	case 0:
		/* Not requested! Do nothing. */
		break;
	default:
		ts_assert(0 && "Never be here!");
	};
}

static int reclaim_tvlog_ckptlog(ts_tvlog_t *tvlog, ts_ckptlog_t *ckptlog)
{
	int reclaim_cnt = 0;

	/* Since tvlog reclamation can trigger
	 * ckptlog reclamation, we have to check both. */
	if (*tvlog->need_reclaim) {
		tvlog_reclaim(tvlog, ckptlog);
		reclaim_cnt++;
	}
	if (*ckptlog->need_reclaim) {
		ckptlog_reclaim(ckptlog);
		reclaim_cnt++;
	}
	return reclaim_cnt;
}

void tvlog_reclaim_below_high_watermark(ts_tvlog_t *tvlog,
					ts_ckptlog_t *ckptlog)
{
	if (tvlog_used(tvlog) >= TS_TVLOG_HIGH_MARK) {
		ts_trace(TS_TVLOG,
			 "tvlog: <<<<< blocking reclamation (%ld/%ld)\n",
			 tvlog_used(tvlog), TS_TVLOG_HIGH_MARK);

		while (tvlog_used(tvlog) >= TS_TVLOG_HIGH_MARK) {
			int count = 0, reclaim_cnt;

			request_tvlog_reclaim(RECLAIM_TVLOG_CKPT);
			do {
				port_cpu_relax_and_yield();
				smp_mb();
				count++;
				if (count == 1000) {
					request_tvlog_reclaim(
						RECLAIM_TVLOG_CKPT);
					count = 0;
				}
				reclaim_cnt =
					reclaim_tvlog_ckptlog(tvlog, ckptlog);
			} while (!reclaim_cnt);
		}

		ts_trace(TS_TVLOG,
			 "tvlog: >>>>> blocking reclamation (%ld/%ld)\n",
			 tvlog_used(tvlog), TS_TVLOG_HIGH_MARK);
	}
}

void tvlog_flush(ts_tvlog_t *tvlog, ts_ckptlog_t *ckptlog)
{
	while (tvlog->head_cnt != tvlog->tail_cnt) {
		int count = 0, reclaim_cnt;
		request_tvlog_reclaim(RECLAIM_TVLOG_CKPT);
		do {
			port_cpu_relax_and_yield();
			smp_mb();
			count++;
			if (count == 1000) {
				request_tvlog_reclaim(RECLAIM_TVLOG_CKPT);
				count = 0;
			}
			reclaim_cnt = reclaim_tvlog_ckptlog(tvlog, ckptlog);
		} while (!reclaim_cnt);
	}
}

int tvlog_create(ts_thread_struct_t *self, ts_tvlog_t *tvlog)
{
	memset(tvlog, 0, sizeof(*tvlog));
	tvlog->clks = &self->clks;
	tvlog->need_reclaim = &self->reclaim.tvlog;
	tvlog->buffer = port_alloc_tvlog_mem();
	if (tvlog->buffer == NULL) {
		return ENOMEM;
	}
	ts_assert(tvlog->buffer ==
		  align_ptr_to_cacheline((void *)tvlog->buffer));

	return 0;
}

void tvtlog_destroy(ts_tvlog_t *tvlog)
{
	if (likely(tvlog && tvlog->buffer)) {
		port_free_tvlog_mem((void *)tvlog->buffer);
		tvlog->buffer = NULL;
	}
}
