// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#include "timestone.h"
#include "timestone_i.h"
#include "port.h"
#include "util.h"
#include "debug.h"
#include "clock.h"
#include "tvlog.h"
#include "ckptlog.h"
#include "oplog.h"
#include "qp.h"

static ts_qp_thread_t g_qp_thread ____cacheline_aligned2;

/*
 * thread list manipulation
 */
static ts_thread_list_t g_live_threads ____cacheline_aligned2;
static ts_thread_list_t g_zombie_threads ____cacheline_aligned2;

#define list_to_thread(__list)                                                 \
	({                                                                     \
		void *p = (void *)(__list);                                    \
		void *q;                                                       \
		q = p - ((size_t) & ((ts_thread_struct_t *)0)->list);          \
		(ts_thread_struct_t *)q;                                       \
	})

#define thread_list_for_each_safe(tl, pos, n, thread)                          \
	for (pos = (tl)->list.next, n = (pos)->next,                           \
	    thread = list_to_thread(pos);                                      \
	     pos != &(tl)->list;                                               \
	     pos = n, n = (pos)->next, thread = list_to_thread(pos))

static inline int thread_list_has_waiter(ts_thread_list_t *tl)
{
	return tl->thread_wait;
}

static inline void init_thread_list(ts_thread_list_t *tl)
{
	port_spin_init(&tl->lock);

	tl->cur_tid = 0;
	tl->num = 0;
	init_ts_list(&tl->list);
}

static inline void thread_list_destroy(ts_thread_list_t *tl)
{
	port_spin_destroy(&tl->lock);
}

static inline void thread_list_lock(ts_thread_list_t *tl)
{
	/* Lock acquisition with a normal priority */
	port_spin_lock(&tl->lock);
}

static inline void thread_list_lock_force(ts_thread_list_t *tl)
{
	/* Lock acquisition with a high priority
	 * which turns on the thread_wait flag
	 * so a lengthy task can stop voluntarily
	 * stop and resume later. */
	if (!port_spin_trylock(&tl->lock)) {
		smp_cas(&tl->thread_wait, 0, 1);
		port_spin_lock(&tl->lock);
	}
}

static inline void thread_list_unlock(ts_thread_list_t *tl)
{
	if (tl->thread_wait)
		smp_atomic_store(&tl->thread_wait, 0);
	port_spin_unlock(&tl->lock);
}

static inline void thread_list_add(ts_thread_list_t *tl,
				   ts_thread_struct_t *self)
{
	thread_list_lock_force(tl);
	{
		self->tid = tl->cur_tid++;
		tl->num++;
		ts_list_add(&self->list, &tl->list);
	}
	thread_list_unlock(tl);
}

static inline void thread_list_del_unsafe(ts_thread_list_t *tl,
					  ts_thread_struct_t *self)
{
	tl->num--;
	ts_list_del(&self->list);
	self->list.prev = self->list.next = NULL;
}

static inline void thread_list_del(ts_thread_list_t *tl,
				   ts_thread_struct_t *self)
{
	thread_list_lock_force(tl);
	{
		thread_list_del_unsafe(tl, self);
	}
	thread_list_unlock(tl);
}

static inline void thread_list_rotate_left_unsafe(ts_thread_list_t *tl)
{
	/* NOTE: A caller should hold a lock */
	ts_list_rotate_left(&tl->list);
}

static inline int thread_list_empty(ts_thread_list_t *tl)
{
	int ret;

	thread_list_lock_force(tl);
	{
		ret = ts_list_empty(&tl->list);
	}
	thread_list_unlock(tl);

	return ret;
}

/*
 * System clock functions
 */
static inline void advance_qp_clock(ts_sys_clks_t *clks, unsigned long qp0_clk)
{
	clks->__qp2 = clks->__qp1;
	clks->__qp1 = clks->__qp0;
	clks->__qp0 = qp0_clk;
}

/*
 * Quiescent detection functions
 */

static void qp_init(ts_qp_thread_t *qp_thread, unsigned long qp_clk)
{
	ts_thread_struct_t *thread;
	ts_list_t *pos, *n;

	thread_list_lock(&g_live_threads);
	{
		smp_mb();
		thread_list_for_each_safe (&g_live_threads, pos, n, thread) {
			thread->qp_info.run_cnt = thread->run_cnt;
			thread->qp_info.need_wait =
				thread->qp_info.run_cnt & 0x1;
		}
	}
	thread_list_unlock(&g_live_threads);
}

static void qp_wait(ts_qp_thread_t *qp_thread, unsigned long qp_clk)
{
	ts_thread_struct_t *thread;
	ts_list_t *pos, *n;

retry:
	thread_list_lock(&g_live_threads);
	{
		thread_list_for_each_safe (&g_live_threads, pos, n, thread) {
			if (!thread->qp_info.need_wait)
				continue;

			while (1) {
				/* Check if a thread passed quiescent period. */
				if (thread->qp_info.run_cnt !=
					    thread->run_cnt ||
				    gte_clock(thread->local_clk, qp_clk)) {
					thread->qp_info.need_wait = 0;
					break;
				}

				/* If a thread is waiting for adding or deleting
				 * from/to the thread list, yield and retry. */
				if (thread_list_has_waiter(&g_live_threads)) {
					thread_list_unlock(&g_live_threads);
					goto retry;
				}

				port_cpu_relax_and_yield();
				smp_mb();
			}
		}
	}
	thread_list_unlock(&g_live_threads);
}

static void qp_take_nap(ts_qp_thread_t *qp_thread)
{
	port_initiate_nap(&qp_thread->cond_mutex, &qp_thread->cond,
			  TS_QP_INTERVAL_USEC);
}

static void qp_cleanup_ckptlogs(ts_qp_thread_t *qp_thread)
{
	ts_thread_struct_t *thread;
	ts_list_t *pos, *n;
	unsigned long until_clk;

	until_clk = correct_qp_clock(qp_thread->clks.__min_ckpt_reclaimed);

	thread_list_lock(&g_live_threads);
	{
		thread_list_for_each_safe (&g_live_threads, pos, n, thread) {
			/* Free original masters and their volatile headers */
			ckptlog_cleanup(&thread->ckptlog, until_clk);
		}
	}
	thread_list_unlock(&g_live_threads);
}

static void qp_detect(ts_qp_thread_t *qp_thread, unsigned char need_free)
{
	unsigned long qp0_clk;

	/* Init qp */
	qp0_clk = get_clock();
	qp_init(qp_thread, qp0_clk);
	stat_qp_inc(qp_thread, n_qp_detect);

	/* If we reclaimed ckptlog, free the master and its vheader. */
	if (need_free) {
		qp_cleanup_ckptlogs(qp_thread);
	}

	/* If not urgent, take a nap */
	if (!qp_thread->reclaim.requested) {
		qp_take_nap(qp_thread);
		stat_qp_inc(qp_thread, n_qp_nap);
	}

	/* Wait until quiescent state */
	qp_wait(qp_thread, qp0_clk);
	qp0_clk = correct_qp_clock(qp0_clk);
	advance_qp_clock(&qp_thread->clks, qp0_clk);
}

static void qp_help_reclaim_log(ts_qp_thread_t *qp_thread)
{
#ifdef TS_ENABLE_HELP_RECLAIM
	ts_thread_struct_t *thread;
	ts_list_t *pos, *n;

retry:
	thread_list_lock(&g_live_threads);
	{
		smp_mb();
		thread_list_for_each_safe (&g_live_threads, pos, n, thread) {
			/* If a thread is waiting for adding or deleting
			 * from/to the thread list, yield and retry. */
			if (thread_list_has_waiter(&g_live_threads)) {
				thread_list_unlock(&g_live_threads);
				goto retry;
			}

			/* Help reclaiming */
			if (unlikely(thread->reclaim.requested)) {
				if (thread->reclaim.tvlog)
					tvlog_reclaim(&thread->tvlog,
						      &thread->ckptlog);
				if (thread->reclaim.ckptlog)
					ckptlog_reclaim(&thread->ckptlog);
			}
		}

		/* Rotate the thread list counter clockwise for fairness. */
		thread_list_rotate_left_unsafe(&g_live_threads);
	}
	thread_list_unlock(&g_live_threads);
#endif
}

static void qp_reap_zombie_threads(ts_qp_thread_t *qp_thread)
{
	ts_thread_struct_t *thread;
	ts_list_t *pos, *n;

retry:
	thread_list_lock(&g_zombie_threads);
	{
		smp_mb();
		thread_list_for_each_safe (&g_zombie_threads, pos, n, thread) {
			/* If a thread is waiting for adding or deleting
			 * from/to the thread list, yield and retry. */
			if (thread_list_has_waiter(&g_zombie_threads)) {
				thread_list_unlock(&g_zombie_threads);
				goto retry;
			}

			/* Propagate the system clock */
			thread->clks = qp_thread->clks;

			/* Enforce reclaiming logs */
			if (tvlog_used(&thread->tvlog)) {
				*thread->tvlog.need_reclaim =
					RECLAIM_TVLOG_CKPT;
				tvlog_reclaim(&thread->tvlog, &thread->ckptlog);
			}

			if (ckptlog_used(&thread->ckptlog)) {
				*thread->ckptlog.need_reclaim =
					RECLAIM_CKPTLOG_WRITEBACK_ALL;
				ckptlog_reclaim(&thread->ckptlog);
			}

			/* If the log is completely reclaimed, try next thread */
			if (tvlog_used(&thread->tvlog) == 0 &&
			    ckptlog_used(&thread->ckptlog) == 0) {
				tvtlog_destroy(&thread->tvlog);
				ckptlog_destroy(&thread->ckptlog);
				oplog_destroy(&thread->oplog);

				/* If it is a dead zombie, reap */
				if (thread->live_status == THREAD_DEAD_ZOMBIE) {
					thread_list_del_unsafe(
						&g_zombie_threads, thread);
					ts_thread_free(thread);
				}
			}
		}
	}
	thread_list_unlock(&g_zombie_threads);
}

static void qp_reclaim_barrier(ts_qp_thread_t *qp_thread)
{
	ts_thread_struct_t *thread;
	ts_list_t *pos, *n;
	unsigned long min_ckpt_reclaimed;

retry:
	min_ckpt_reclaimed = qp_thread->clks.__min_ckpt_reclaimed;
	thread_list_lock(&g_live_threads);
	{
		smp_mb();
		thread_list_for_each_safe (&g_live_threads, pos, n, thread) {
			/* Did we finish reclaiming? */
			if (thread->reclaim.requested) {
				thread_list_unlock(&g_live_threads);
				port_cpu_relax_and_yield();
				goto retry;
			}
			/* Update the minimum reclaimed ckpt timestamp */
			if (thread->clks.__min_ckpt_reclaimed <
			    min_ckpt_reclaimed) {
				min_ckpt_reclaimed =
					thread->clks.__min_ckpt_reclaimed;
			}
		}

		/* Now update the minimum timestamp of all reclaimed ckpt
		 * to decide until when we can free original masters. */
		qp_thread->clks.__min_ckpt_reclaimed = min_ckpt_reclaimed;
	}
	thread_list_unlock(&g_live_threads);
}

static void qp_trigger_reclaim(ts_qp_thread_t *qp_thread, ts_reclaim_t reclaim)
{
	ts_thread_struct_t *thread;
	ts_list_t *pos, *n;

	/* Check if at least one QP passed after the last reclamation. */
	ts_assert(lt_clock(qp_thread->clks.__last_ckpt, qp_thread->clks.__qp0));

	/* For each thread ... */
	thread_list_lock(&g_live_threads);
	{
		ts_trace(
			TS_QP,
			"[QP:%ld/%ld/%ld] ===== Log reclamation triggered: %s, %s =====\n",
			qp_thread->clks.__qp0, qp_thread->clks.__last_ckpt,
			qp_thread->clks.__min_ckpt_reclaimed,
			req2str(reclaim.tvlog), req2str(reclaim.ckptlog));

		/* For each live threads ... */
		thread_list_for_each_safe (&g_live_threads, pos, n, thread) {
			/* Check if the previous reclamation finished. */
			ts_assert(thread->reclaim.requested == 0);

			/* Propagate the system clock */
			thread->clks = qp_thread->clks;

			/* ... and then copy the reclamation requests.
			 * We should use the copy of qp_thread->reclaim, *reclaim,
			 * because qp_thread->reclaim could be updated while
			 * we are triggering reclaim. */
			smp_wmb_tso();
			thread->reclaim = reclaim;
		}
	}
	thread_list_unlock(&g_live_threads);
	smp_mb();
}

static inline int should_stop(ts_qp_thread_t *qp_thread)
{
	return qp_thread->stop_requested &&
	       thread_list_empty(&g_zombie_threads) &&
	       thread_list_empty(&g_live_threads);
}

static void __qp_thread_main(void *arg)
{
	ts_qp_thread_t *qp_thread = arg;
	unsigned char need_free;
	ts_reclaim_t reclaim;
	unsigned long last_ckpt_clk;

	/* Qp detection loop, which runs until stop is requested
	 * and all zombie threads are reclaimed after flushing
	 * all their logs. */
	need_free = 0;
	while (!should_stop(qp_thread)) {
		/* Detect QP and free act_vhdr if needed */
		qp_detect(qp_thread, need_free);
		need_free = 0;

		/* If a stop is requested, flush out all logs. */
		if (unlikely(qp_thread->stop_requested)) {
			request_tvlog_reclaim(RECLAIM_TVLOG_CKPT);
			request_ckptlog_reclaim(RECLAIM_CKPTLOG_WRITEBACK_ALL);
		}

		/* If a reclaim is not requested, go for another qp detection. */
		if (!qp_thread->reclaim.requested) {
			continue;
		}
		ts_trace(TS_QP,
			 "[QPT] Before Trigger %04x, %04x, %04x, %s, %s \n",
			 qp_thread->reclaim.requested, qp_thread->reclaim.tvlog,
			 qp_thread->reclaim.ckptlog,
			 req2str(qp_thread->reclaim.tvlog),
			 req2str(qp_thread->reclaim.ckptlog));

		/* Trigger reclaim if requested */
		reclaim.requested = smp_swap(&qp_thread->reclaim.requested, 0);
		ts_trace(TS_QP,
			 "[QPT] After Trigger %04x, %04x, %04x, %s, %s \n",
			 qp_thread->reclaim.requested, qp_thread->reclaim.tvlog,
			 qp_thread->reclaim.ckptlog,
			 req2str(qp_thread->reclaim.tvlog),
			 req2str(qp_thread->reclaim.ckptlog));

		ts_trace(
			TS_QP,
			"[QPT] After Trigger reclaim Local %04x, %04x, %04x, %s, %s \n",
			reclaim.requested, reclaim.tvlog, reclaim.ckptlog,
			req2str(reclaim.tvlog), req2str(reclaim.ckptlog));

		qp_trigger_reclaim(qp_thread, reclaim);

		/* Help reclamation */
		qp_reap_zombie_threads(qp_thread);
		qp_help_reclaim_log(qp_thread);

		/* Wait for a reclamation barrier */
		qp_reclaim_barrier(qp_thread);

		/* If a new checkpoint is created,
		 * update the last checkpoint timestamp. */
		if (reclaim.tvlog == RECLAIM_TVLOG_CKPT) {
			/* Persist ckpt clk first */
			last_ckpt_clk = qp_thread->clks.__qp0;
			nvlog_set_last_ckpt_clk(last_ckpt_clk);

			/* ... then make __last_ckpt public.*/
			qp_thread->clks.__last_ckpt = last_ckpt_clk;
		}

		/* If a checkpoint log is reclaimed,
		 * we need to free the original master objects
		 * freed after a reclamation barrier passed. */
		need_free = reclaim.ckptlog;
		smp_mb();

		/* Print out message */
		ts_trace(
			TS_QP,
			"[QP:%ld/%ld/%ld] ----- Reclamation barrier passed -----\n",
			qp_thread->clks.__qp0, qp_thread->clks.__last_ckpt,
			qp_thread->clks.__min_ckpt_reclaimed);
	}
}

static void *qp_thread_main(void *arg)
{
	__qp_thread_main(arg);
	return NULL;
}

static int init_qp_thread(ts_qp_thread_t *qp_thread)
{
	int rc;

	/* Zero out qp_thread and set __min_ckpt_reclaimed to infinite */
	memset(qp_thread, 0, sizeof(*qp_thread));
	qp_thread->clks.__last_ckpt = MIN_VERSION;
	qp_thread->clks.__min_ckpt_reclaimed = MAX_VERSION;

	/* Init thread-related stuffs */
	port_cond_init(&qp_thread->cond);
	port_mutex_init(&qp_thread->cond_mutex);
	rc = port_create_thread("qp_thread", &qp_thread->thread,
				&qp_thread_main, qp_thread,
				&qp_thread->completion);
	if (rc) {
		ts_trace(TS_ERROR, "Error creating builder thread: %d\n", rc);
		return rc;
	}
	return 0;
}

static inline void wakeup_qp_thread(ts_qp_thread_t *qp_thread)
{
	port_initiate_wakeup(&qp_thread->cond_mutex, &qp_thread->cond);
}

static void finish_qp_thread(ts_qp_thread_t *qp_thread)
{
	smp_atomic_store(&qp_thread->stop_requested, 1);
	wakeup_qp_thread(qp_thread);

	port_wait_for_finish(&qp_thread->thread, &qp_thread->completion);
	port_mutex_destroy(&qp_thread->cond_mutex);
	port_cond_destroy(&qp_thread->cond);
	stat_qp_merge(qp_thread);
}

static int _request_reclaim(volatile unsigned char *p_old_req,
			    unsigned char new_req)
{
	unsigned char old_req = *p_old_req;

	ts_trace(TS_QP, "[QP] status before request: %04x, %04x, %04x\n",
		 g_qp_thread.reclaim.requested, g_qp_thread.reclaim.tvlog,
		 g_qp_thread.reclaim.ckptlog);

	if (new_req > old_req && smp_cas(p_old_req, old_req, new_req)) {
		ts_trace(TS_QP, "[QP] %s requested!\n", req2str(new_req));
		ts_trace(TS_QP, "[QP] status after request: %04x, %04x, %04x\n",
			 g_qp_thread.reclaim.requested,
			 g_qp_thread.reclaim.tvlog,
			 g_qp_thread.reclaim.ckptlog);
		wakeup_qp_thread(&g_qp_thread);
		return 1;
	}
	return 0;
}

int request_tvlog_reclaim(unsigned char new_req)
{
	return _request_reclaim(&g_qp_thread.reclaim.tvlog, new_req);
}

int request_ckptlog_reclaim(unsigned char new_req)
{
	return _request_reclaim(&g_qp_thread.reclaim.ckptlog, new_req);
}

int init_qp(void)
{
	int rc;

	init_thread_list(&g_live_threads);
	init_thread_list(&g_zombie_threads);
	rc = init_qp_thread(&g_qp_thread);
	return rc;
}

void deinit_qp(void)
{
	ts_trace(TS_QP, "[QP] !!! Finishing the QP thread !!!\n");
	finish_qp_thread(&g_qp_thread);
	thread_list_destroy(&g_live_threads);
	thread_list_destroy(&g_zombie_threads);
}

void register_thread(ts_thread_struct_t *self)
{
	thread_list_add(&g_live_threads, self);
}

void deregister_thread(ts_thread_struct_t *self)
{
	thread_list_del(&g_live_threads, self);
}

void zombinize_thread(ts_thread_struct_t *self)
{
	thread_list_add(&g_zombie_threads, self);
}

void reset_all_stats(void)
{
#ifdef TS_ENABLE_STATS
	ts_thread_struct_t *thread;
	ts_list_t *pos, *n;

	/* Reset stats for all zombie threads */
	thread_list_lock(&g_zombie_threads);
	{
		thread_list_for_each_safe (&g_zombie_threads, pos, n, thread) {
			memset(&thread->stat, 0, sizeof(thread->stat));
		}
	}
	thread_list_unlock(&g_zombie_threads);

	/* Reset stats for all live threads */
	thread_list_lock(&g_live_threads);
	{
		thread_list_for_each_safe (&g_live_threads, pos, n, thread) {
			memset(&thread->stat, 0, sizeof(thread->stat));
		}
	}
	thread_list_unlock(&g_live_threads);
#endif
}
