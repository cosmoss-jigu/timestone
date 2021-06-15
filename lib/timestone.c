#ifndef __KERNEL__
#include "timestone.h"
#else
#include <linux/timestone.h>
#endif

#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include "timestone_i.h"
#include "port.h"
#include "util.h"
#include "debug.h"
#include "tvlog.h"
#include "oplog.h"
#include "ckptlog.h"
#include "nvm.h"
#include "clock.h"
#include "qp.h"
#include "isolation.h"
#include "recovery.h"

static unsigned long __g_gen_id;
static ts_recovery_t __g_recovery;

/*
 * Utility functions
 */

static inline unsigned long get_gen_id(void)
{
	return __g_gen_id;
}

/*
 * External APIs
 */
int __init ts_init(ts_conf_t *conf)
{
	static int init = 0;
	ts_nvm_root_obj_t *nvm_root_obj;
	int need_recovery, rc;

	/* Compile time sanity check */
	static_assert(TS_AHS_SIZE == sizeof(ts_act_hdr_struct_t));
	static_assert(sizeof(ts_act_hdr_struct_t) < L1_CACHE_BYTES);
	static_assert(sizeof(ts_cpy_hdr_struct_t) < L1_CACHE_BYTES);
	static_assert((TS_TVLOG_SIZE & (TS_TVLOG_SIZE - 1)) == 0);

	/* Make sure whether it is initialized once */
	if (!smp_cas(&init, 0, 1))
		return -EBUSY;

	/* Init */
	init_clock();
	nvm_root_obj =
		nvm_init_heap(conf->nvheap_path,
			      align_size_t_to_pmem_page(conf->nvheap_size),
			      &need_recovery);
	if (unlikely(!nvm_root_obj)) {
		ts_trace(TS_ERROR, "Fail to initialize an nvm heap\n");
		return ENOMEM;
	}

	rc = nvlog_init(nvm_root_obj);
	if (rc) {
		ts_trace(TS_ERROR, "Fail to initialize a nvlog region\n");
		return rc;
	}

	__g_gen_id = nvm_get_gen_id();
	rc = port_tvlog_region_init(TS_TVLOG_SIZE, TS_MAX_THREAD_NUM);
	if (rc) {
		ts_trace(TS_ERROR, "Fail to initialize a log region\n");
		return rc;
	}
	rc = init_qp();
	if (rc) {
		ts_trace(TS_ERROR, "Fail to initialize a qp thread\n");
		return rc;
	}

	/* Setup recovery information */
	__g_recovery.root = nvm_root_obj;
	__g_recovery.op_exec = conf->op_exec;

	/* If necessary, perform recovery */
	if (unlikely(need_recovery)) {
		rc = perform_recovery(&__g_recovery);
		if (unlikely(rc)) {
			ts_trace(TS_ERROR, "Fail to recover logs\n");
			return rc;
		}
	}

	return 0;
}
early_initcall(ts_init);

void __ts_finish(void)
{
	deinit_qp();
	port_tvlog_region_destroy();
	/* TODO: free_all_act_vhdrs(); */
}

void __ts_unload_nvm(void)
{
	nvm_heap_destroy();
}

void ts_finish(void)
{
	__ts_finish();
	__ts_unload_nvm();
}

ts_thread_struct_t *ts_thread_alloc(void)
{
	return port_alloc(sizeof(ts_thread_struct_t));
}
EXPORT_SYMBOL(ts_thread_alloc);

void ts_thread_free(ts_thread_struct_t *self)
{
	smp_atomic_store(&self->live_status, THREAD_DEAD_ZOMBIE);

	/* If the log is not completely reclaimed yet,
	 * defer the free until it is completely reclaimed.
	 * In this case, we just move the thread to the zombie
	 * list and the qp thread will eventually reclaim
	 * the log. */
	smp_mb();
	if (tvlog_used(&self->tvlog) == 0 &&
	    ckptlog_used(&self->ckptlog) == 0) {
		stat_thread_merge(self);
		tvtlog_destroy(&self->tvlog);
		ckptlog_destroy(&self->ckptlog);
		oplog_destroy(&self->oplog);

		ptrset_deinit(&self->tx_alloc_set);
		ptrset_deinit(&self->tx_free_set);
		ptrset_deinit(&self->ckpt_free_set);
		port_free(self);
	}
}
EXPORT_SYMBOL(ts_thread_free);

void ts_thread_init_x(ts_thread_struct_t *self, unsigned short flags)
{
	int rc;

	/* Zero out self */
	memset(self, 0, sizeof(*self));

	/* Initialize clocks*/
	self->clks.__last_ckpt = MIN_VERSION;
	self->clks.__min_ckpt_reclaimed = MAX_VERSION;

	/* Initialize free pointer arrays */
	rc = ptrset_init(&self->tx_alloc_set);
	if (unlikely(rc))
		goto err_out;

	rc = ptrset_init(&self->tx_free_set);
	if (unlikely(rc))
		goto err_out;

	rc = ptrset_init(&self->ckpt_free_set);
	if (unlikely(rc))
		goto err_out;

	/* Initialize isolation info */
	rc = isolation_init(&self->isolation);
	if (self->tid == 1) {
		printf("########## ISOLATION LEVEL:%d ########### \n",
		       self->isolation.level);
	}
	if (unlikely(rc))
		goto err_out;

	/* Allocate cacheline-aligned log space on DRAM */
	rc = tvlog_create(self, &self->tvlog);
	if (unlikely(rc))
		goto err_out;

	/* Set the status status if it is in a recovery mode */
	self->in_recovery_mode = (flags == STATUS_NVLOG_RECOVERY);

	/* Allocate cacheline-aligned oplog space on NVM */
	oplog_create(self, &self->oplog, TS_OPLOG_SIZE, flags);
	ts_assert(self->oplog.buffer ==
		  align_ptr_to_cacheline((void *)self->oplog.buffer));

	/* Allocate cacheline-aligned ckptlog space on NVM */
	ckptlog_create(self, &self->ckptlog, TS_CKPTLOG_SIZE, flags);
	ts_assert(self->ckptlog.buffer ==
		  align_ptr_to_cacheline((void *)self->ckptlog.buffer));

	/* Add this to the global list */
	register_thread(self);
	smp_mb();
	return;
err_out:
	/* TODO: change the return type to int to return an errno. */
	ts_assert(0);
	return;
}
EXPORT_SYMBOL(ts_thread_init_x);

void ts_thread_init(ts_thread_struct_t *self)
{
	ts_thread_init_x(self, STATUS_NVLOG_NORMAL);
}
EXPORT_SYMBOL(ts_thread_init);

static inline void try_reclaim_logs(ts_thread_struct_t *self)
{
	if (unlikely(self->reclaim.requested)) {
		if (self->reclaim.tvlog)
			tvlog_reclaim(&self->tvlog, &self->ckptlog);
		if (self->reclaim.ckptlog)
			ckptlog_reclaim(&self->ckptlog);
	}
}

void ts_thread_finish(ts_thread_struct_t *self)
{
	/* Reclaim logs as much as it can. */
	try_reclaim_logs(self);
	oplog_reclaim(&self->oplog, RECLAIM_OPLOG_NORMAL);

	/* Deregister this thread from the live list */
	self->qp_info.need_wait = 0;
	deregister_thread(self);

	/* If logs are not completely reclaimed, add it to
	 * the zombie list to reclaim the tvlog or ckptlog later. */
	if (tvlog_used(&self->tvlog) || ckptlog_used(&self->ckptlog)) {
		smp_atomic_store(&self->live_status, THREAD_LIVE_ZOMBIE);
		zombinize_thread(self);
	} else {
		isolation_deinit(&self->isolation);
		tvtlog_destroy(&self->tvlog);
		ckptlog_destroy(&self->ckptlog);
		oplog_destroy(&self->oplog);
		stat_thread_merge(self);
	}
}
EXPORT_SYMBOL(ts_thread_finish);

void ts_stat_alloc_act_obj(ts_thread_struct_t *self, size_t size)
{
	ts_act_hdr_struct_t *ahs;

	stat_thread_acc(self, n_alloc_act_obj_bytes, sizeof(*ahs) + size);
}
EXPORT_SYMBOL(ts_stat_alloc_act_obj);

void ts_update_index(void *obj, int index)
{
	ts_act_nvhdr_t *p_act_nvhdr;

	p_act_nvhdr = get_act_nvhdr(obj);
	p_act_nvhdr->index = index;
	/* TODO: need to flush?*/
	return;
}
EXPORT_SYMBOL(ts_update_index);

int ts_get_index(void *obj)
{
	ts_act_nvhdr_t *p_act_nvhdr;

	p_act_nvhdr = get_act_nvhdr(obj);
	return p_act_nvhdr->index;
}
EXPORT_SYMBOL(ts_get_index);

volatile char *ts_get_base_addr(volatile void *obj)
{
	ts_obj_hdr_t *obj_hdr;
	ts_act_hdr_struct_t *ahs;
	ts_act_nvhdr_t *p_act_nvhdr;
	int curr_index, size;
	void *base_obj;

	obj_hdr = vobj_to_obj_hdr(obj);
	if (!is_obj_actual(obj_hdr)) {
		ts_assert(obj_hdr->type == TYPE_COPY);
		obj = vobj_to_chs(obj, TYPE_COPY)
			      ->cpy_hdr.p_act_vhdr->np_org_act;
	}
	ahs = vobj_to_ahs(obj);
	curr_index = ahs->act_nvhdr.index;
	if (unlikely(!curr_index))
		return obj;
	size = ahs->obj_hdr.obj_size;
	base_obj = (((char *)obj) - ((sizeof(*ahs) + size) * curr_index));
	p_act_nvhdr = get_act_nvhdr((void *)base_obj);
	curr_index = p_act_nvhdr->index;
	ts_assert(curr_index == 0);
	return (char *)base_obj;
}

volatile void *ts_get_offset(void *obj, int index)
{
	ts_obj_hdr_t *obj_hdr;
	ts_act_nvhdr_t *p_act_nvhdr;
	int size;
	volatile char *base_obj;
	volatile void *offset;

	base_obj = ts_get_base_addr(obj);
	obj_hdr = vobj_to_obj_hdr(base_obj);
	size = obj_hdr->obj_size;
	offset = base_obj + ((sizeof(ts_act_hdr_struct_t) + size) * index);
	p_act_nvhdr = get_act_nvhdr(offset);
	if (!p_act_nvhdr) {
		ts_trace(TS_ERROR, "NVhdr does not exists, Invalid object\n");
		EFAULT;
	}
	ts_assert(p_act_nvhdr->index == index);
	return offset;
}
EXPORT_SYMBOL(ts_get_offset);

void *ts_alloc(size_t size)
{
	ts_act_hdr_struct_t *ahs;

	/* Alloc and init
	 * NOTE: It should be aligned by 16 bytes for smp_cas16b() */
	ahs = nvm_aligned_alloc(16, sizeof(*ahs) + size);
	if (unlikely(ahs == NULL))
		return NULL;

	memset(ahs, 0, sizeof(*ahs));
	ahs->obj_hdr.type = TYPE_ACTUAL;
	ahs->obj_hdr.obj_size = size;
	ahs->act_nvhdr.gen_id = INVALID_GEN_ID;

	return ahs->obj_hdr.obj;
}
EXPORT_SYMBOL(ts_alloc);

void ts_free(ts_thread_struct_t *self, void *obj)
{
	/* NOTE: free from non-volatile memory */
	ts_act_hdr_struct_t *ahs;
	void *p_act;

	if (unlikely(obj == NULL))
		return;

	if (unlikely(self == NULL)) {
		ahs = obj_to_ahs(obj);
		nvm_free(ahs);
		return;
	}
	ts_assert(self->run_cnt & 0x1);

	p_act = get_org_act_obj(obj);
	ts_assert(get_act_vhdr(obj) == obj_to_ahs(p_act)->act_nvhdr.p_act_vhdr);
	ts_assert(obj_to_ahs(p_act)->obj_hdr.type == TYPE_ACTUAL);
	ts_assert(obj_to_ahs(p_act)->act_nvhdr.p_act_vhdr->p_lock != NULL);

	ptrset_push(&self->tx_free_set, p_act);
}
EXPORT_SYMBOL(ts_free);

void ts_begin(ts_thread_struct_t *self, int isolation_level)
{
	/* Reclaim log if requested at the transaction boundary. */
	try_reclaim_logs(self);

	/* Secure enough tvlog space below high watermark */
	tvlog_reclaim_below_high_watermark(&self->tvlog, &self->ckptlog);

	/* Object data writes should not be reordered with metadata writes. */
	smp_wmb_tso();

	/* Get it started */
	isolation_reset(&self->isolation, isolation_level);
	smp_faa(&(self->run_cnt), 1);
	self->local_clk = get_clock_relaxed();

	/* Get the latest view */
	smp_rmb();

	stat_thread_inc(self, n_starts);
	ts_assert(self->tvlog.cur_wrt_set == NULL);
	ts_assert(self->tx_free_set.num_ptrs == 0);
}
EXPORT_SYMBOL(ts_begin);

static void flush_new_act(ts_thread_struct_t *self)
{
	ts_ptr_set_t *tx_alloc_set;
	ts_act_hdr_struct_t *ahs;
	size_t size;

	/* Call clwb for all newly allocated actual
	 * objects. Note we don't need to call wmb()
	 * here because they should be flushed before
	 * next checkpointing. */
	tx_alloc_set = &self->tx_alloc_set;
	while ((ahs = ptrset_pop(tx_alloc_set))) {
		size = sizeof(*ahs) + ahs->obj_hdr.obj_size;
		flush_to_nvm(ahs, size);
		stat_thread_acc(self, n_flush_new_act_bytes, size);
	}
}

int ts_end(ts_thread_struct_t *self)
{
	ts_assert(self->run_cnt & 0x1);

	/* Do not commit in a recovery mode. */
	if (unlikely(self->in_recovery_mode)) {
		return 1;
	}

	/* Read set validation */
	if (!validate_read_set(&self->isolation)) {
		ts_abort(self);
		stat_thread_inc(self, n_aborts_validation);
		return 0;
	}

	/* Object data writes should not be reordered with metadata writes. */
	smp_wmb_tso();

	/* If it is a writer, commit its changes. */
	if (self->is_write_detected) {
		tvlog_commit(&self->tvlog, &self->oplog, &self->tx_free_set,
			     self->local_clk, &self->op_info);
		self->is_write_detected = 0;
	}

	/* Now every thing is done. */
	self->run_cnt++;

	/* Flush all newly allocated objects */
	flush_new_act(self);
	ts_assert(self->tx_alloc_set.num_ptrs == 0);

	/* Reclaim log if requested at the transaction boundary. */
	try_reclaim_logs(self);

	stat_thread_inc(self, n_finish);
	ts_assert(self->tvlog.cur_wrt_set == NULL);
	ts_assert(self->tx_free_set.num_ptrs == 0);

	/* Success */
	return 1;
}
EXPORT_SYMBOL(ts_end);

static void free_new_act(ts_thread_struct_t *self)
{
	ts_ptr_set_t *tx_alloc_set;
	ts_act_hdr_struct_t *ahs;

	/* Call clwb for all newly allocated actual
	 * objects. Note we don't need to call wmb()
	 * here because they should be flushed before
	 * next checkpointing. */
	tx_alloc_set = &self->tx_alloc_set;
	while ((ahs = ptrset_pop(tx_alloc_set))) {
		nvm_free(ahs);
	}
}

void ts_abort(ts_thread_struct_t *self)
{
	/* Object data writes should not be reordered with metadata writes. */
	smp_wmb_tso();

	ts_assert(self->run_cnt & 0x1);
	self->run_cnt++;

	if (self->tvlog.cur_wrt_set) {
		tvlog_abort(&self->tvlog, &self->tx_free_set);
		self->is_write_detected = 0;
	}

	/* free all newly allocated objects */
	free_new_act(self);
	ts_assert(self->tx_alloc_set.num_ptrs == 0);

	/* Reclaim log if requested at the transaction boundary. */
	try_reclaim_logs(self);

	/* Help log reclamation upon abort */
	oplog_reclaim(&self->oplog, RECLAIM_OPLOG_NORMAL);

	/* Prepare next ts_reader_lock() by performing memory barrier. */
	smp_mb();

	stat_thread_inc(self, n_aborts);
	ts_assert(self->tvlog.cur_wrt_set == NULL);
	ts_assert(self->tx_free_set.num_ptrs == 0);
}
EXPORT_SYMBOL(ts_abort);

void _dbg_assert_chs_copy(const char *f, const int l, ts_cpy_hdr_struct_t *chs)
{
#ifdef TS_ENABLE_ASSERT
	if (chs->obj_hdr.type != TYPE_COPY) {
		ts_dbg_dump_all_version_chain_chs(f, l, chs);
	}
#endif
	ts_assert(chs->obj_hdr.type == TYPE_COPY);
}

void *ts_deref(ts_thread_struct_t *self, void *obj)
{
	volatile void *p_copy;
	void *p_latest;
	ts_cpy_hdr_struct_t *chs;
	ts_act_vhdr_t *p_act_vhdr;
	unsigned long wrt_clk, last_ckpt_clk, local_clk;

	if (unlikely(!obj))
		return NULL;

	/* Case 0: if it is not an actual, that means
	 * obj is already dereferenced. */
	if (unlikely(!is_obj_actual(obj_to_obj_hdr(obj)))) {
		return obj;
	}

	/* Case 1: it does not have a volatile header,
	 * which means the object is the original actual
	 * and hasn't been updated so far. */
	p_act_vhdr = get_act_vhdr(obj);
	if (unlikely(!p_act_vhdr)) {
		read_set_add(&self->isolation, p_act_vhdr, NULL, obj);
		ts_assert(obj);
		return obj;
	}

	/* Case 2: it has a volatile header. */
	p_copy = p_act_vhdr->p_copy;
	p_latest = (void *)p_copy;
	if (unlikely(p_copy)) {
		last_ckpt_clk = self->clks.__last_ckpt;
		local_clk = self->local_clk;

		do {
			chs = vobj_to_chs(p_copy, TYPE_COPY);
			wrt_clk = get_wrt_clk(chs, local_clk);
			_dbg_assert_chs_copy(__func__, __LINE__, chs);

			if (lt_clock(wrt_clk, local_clk)) { /* TIME: < */
				read_set_add(&self->isolation, p_act_vhdr,
					     p_latest, (void *)p_copy);
				return (void *)p_copy;
			}

			/* All copies in tvlog that are older than
			 * last checkpoint timestamp (last_ckpt) are
			 * guaranteed to be checkpointed meaning
			 * there are no longer exist in tvlog.
			 * Therefore we should stop version chain
			 * traversal here and fall back
			 * to the np_cur_master. */
			if (unlikely(lt_clock(chs->cpy_hdr.wrt_clk_next,
					      last_ckpt_clk))) /* TIME: < */
				break;
			p_copy = chs->cpy_hdr.p_copy;
		} while (p_copy);
	}

	/* Returns the current master object */
	obj = (void *)p_act_vhdr->np_cur_act;
	read_set_add(&self->isolation, p_act_vhdr, p_latest, obj);
	ts_assert(obj);
	return obj;
}
EXPORT_SYMBOL(ts_deref);

static inline int try_lock_obj(ts_act_vhdr_t *p_act_vhdr,
			       volatile void *p_old_copy,
			       volatile void *p_new_copy)
{
	int ret;

	if (p_act_vhdr->p_lock != NULL || p_act_vhdr->p_copy != p_old_copy)
		return 0;

	smp_wmb_tso();
	ret = smp_cas(&p_act_vhdr->p_lock, NULL, p_new_copy);
	if (!ret)
		return 0; /* smp_cas() failed */

	if (unlikely(p_act_vhdr->p_copy != p_old_copy)) {
		ts_assert(p_act_vhdr->p_lock == p_new_copy);

		/* If it is ABA, unlock and return false */
		smp_wmb();
		p_act_vhdr->p_lock = NULL;
		return 0;
	}

	/* Finally succeeded. Updating p_copy of p_new_copy
	 * will be done upon commit. */
	return 1;
}

static int try_alloc_act_vhdr(void *obj)
{
	ts_act_nvhdr_t *p_act_nvhdr;
	ts_act_nvhdr_t old_act_nvhdr, new_act_nvhdr;
	ts_act_vhdr_t *p_act_vhdr;

	p_act_nvhdr = get_act_nvhdr(obj);
	old_act_nvhdr = *p_act_nvhdr;

	/* If another thread already allocates,
	 * then yield to that thread. */
	if (old_act_nvhdr.p_act_vhdr != NULL) {
		return 0;
	}

	/* The following check is necessary because
	 * the act_vhdr can be allocated in between. */
#if 0 /* TODO: temporarily disable */
	if (likely(old_act_nvhdr.gen_id != get_gen_id())) {
		return 0;
	}
#endif

	// new_act_nvhdr.p_act_vhdr = alloc_act_vhdr(obj);
	p_act_vhdr = alloc_act_vhdr(obj);
	smp_atomic_store(&new_act_nvhdr.p_act_vhdr, p_act_vhdr);
	smp_mb();
	new_act_nvhdr.gen_id = get_gen_id();

	if (unlikely(new_act_nvhdr.p_act_vhdr == NULL)) {
		ts_trace(TS_ERROR, "Fail to allocate p_act_vhdr\n");
		return 0;
	}

	/* update volatile header and a generation id */
	if (p_act_nvhdr->p_act_vhdr ||
	    !smp_cas16b(p_act_nvhdr, NULL, old_act_nvhdr.gen_id,
			new_act_nvhdr.p_act_vhdr, new_act_nvhdr.gen_id)) {
		/* Another thread already allocates and updates. */
		free_act_vhdr((void *)new_act_nvhdr.p_act_vhdr);
		return 0;
	}

	return 1;
}

int _ts_try_lock(ts_thread_struct_t *self, void **pp_obj, size_t size)
{
	volatile void *p_lock, *p_old_copy, *p_new_copy, *p_act;
	ts_cpy_hdr_struct_t *chs;
	ts_act_vhdr_t *p_act_vhdr;
	unsigned long local_clk;
	void *obj;
	int bogus_allocated;

	/* Check if stale read already occured */
	if (stale_read_occurred(&self->isolation)) {
		return 0;
	}

	obj = *pp_obj;
	ts_warning(obj != NULL);
	p_act_vhdr = get_act_vhdr(obj);

	/* If act_vhdr is not yet allocated, try to allocate it. */
	if (unlikely(!p_act_vhdr)) {
		if (!try_alloc_act_vhdr(obj)) {
			return 0;
		}
		p_act_vhdr = (ts_act_vhdr_t *)get_act_nvhdr(obj)->p_act_vhdr;
	}
	ts_assert(p_act_vhdr != NULL);
	ts_assert(p_act_vhdr ==
		  obj_to_ahs(get_org_act_obj(obj))->act_nvhdr.p_act_vhdr);

	/* If an object is already locked, it cannot lock again
	 * except when a lock is locked again by the same thread. */
	p_act = p_act_vhdr->np_cur_act;
	p_lock = p_act_vhdr->p_lock;
	if (unlikely(p_lock)) {
#ifdef TS_NESTED_LOCKING
		ts_wrt_set_t *ws;

		/* Free is never unlocked. */
		if (vobj_to_obj_hdr(p_lock)->type == TYPE_FREE) {
			return 0;
		}

		/* If the same thread tries to acquire the same lock,
		 * the previous transaction should be committed
		 * before starting the second transaction and it makes
		 * get_raw_wrt_clk(chs) non-MAX_VERSION. */
		ws = vchs_obj_to_ws(p_lock);
		ts_assert(ws);
		if (self != ws->thread ||
		    self->run_cnt != ws->thread->run_cnt) {
			return 0;
		}

		/* If the lock is acquired by the same thread,
		 * allow to lock again according to the original
		 * RLU semantics.
		 *
		 * WARNING: We do not promote immutable try_lock_const()
		 * to mutable try_lock_const().
		 */
		*pp_obj = (void *)p_lock;
		ts_assert(vobj_to_obj_hdr(p_lock)->type == TYPE_COPY);
		return 1;
#else
		return 0;
#endif /* TS_NESTED_LOCKING */
	}

	/* To maintain a linear version history, we should allow
	 * lock acquisition only when the local version of a thread
	 * is greater or equal to the writer version of an object.
	 * Otherwise it allows inconsistent, mixed, views
	 * of the local version and the writer version.
	 * That is because acquiring a lock fundamentally means
	 * advancing the version. */
	p_old_copy = p_act_vhdr->p_copy;
	if (p_old_copy) {
		chs = vobj_to_chs(p_old_copy, TYPE_COPY);
		/* It guarantees that clock gap between two versions of
		 * an object is greater than 2x ORDO_BOUNDARY. */
		local_clk = self->local_clk;
		if (gte_clock(get_wrt_clk(chs, local_clk),
			      local_clk)) /* TIME: >= */
			return 0;
	}

	/* Secure log space and initialize a header */
	chs = tvlog_append_begin(&self->tvlog, p_act_vhdr, size,
				 &bogus_allocated);
	p_new_copy = (volatile void *)chs->obj_hdr.obj;

	/* Try lock */
	if (!try_lock_obj(p_act_vhdr, p_old_copy, p_new_copy)) {
		tvlog_append_abort(&self->tvlog, chs);
		return 0;
	}

	/* Duplicate the copy */
	if (!p_old_copy) {
		p_act = p_act_vhdr->np_cur_act;
		memcpy((void *)p_new_copy, (void *)p_act, size);
	} else
		memcpy((void *)p_new_copy, (void *)p_old_copy, size);
	tvlog_append_end(&self->tvlog, chs, bogus_allocated);

	/* Succeed in locking */
	if (self->is_write_detected == 0)
		self->is_write_detected = 1;
	*pp_obj = (void *)p_new_copy;

	/* Add this to the write set. */
	write_set_add(&self->isolation, (void *)p_new_copy);

	ts_assert(p_act_vhdr->p_lock);
	ts_assert(p_act_vhdr ==
		  obj_to_ahs(get_org_act_obj(obj))->act_nvhdr.p_act_vhdr);
	ts_assert(obj_to_ahs(get_org_act_obj(obj))
			  ->act_nvhdr.p_act_vhdr->p_lock != NULL);
	return 1;
}
EXPORT_SYMBOL(_ts_try_lock);

int _ts_try_lock_const(ts_thread_struct_t *self, void *obj, size_t size)
{
	/* Try_lock_const is nothing but a try lock with size zero
 	 * so we can omit copy from/to p_act.
	 *
	 * NOTE: obj is not updated after the call (not void ** but void *) */
	return _ts_try_lock(self, &obj, 0);
}
EXPORT_SYMBOL(_ts_try_lock_const);

int ts_cmp_ptrs(void *obj1, void *obj2)
{
	if (likely(obj1 != NULL))
		obj1 = get_org_act_obj(obj1);
	if (likely(obj2 != NULL))
		obj2 = get_org_act_obj(obj2);
	return obj1 == obj2;
}
EXPORT_SYMBOL(ts_cmp_ptrs);

void _ts_assign_pointer(void **p_ptr, void *obj)
{
	if (likely(obj != NULL))
		obj = get_org_act_obj(obj);
	*p_ptr = obj;
}
EXPORT_SYMBOL(_ts_assign_pointer);

void ts_flush_log(ts_thread_struct_t *self)
{
	tvlog_flush(&self->tvlog, &self->ckptlog);
	ckptlog_flush(&self->ckptlog);
}
EXPORT_SYMBOL(ts_flush_log);

void ts_set_op(ts_thread_struct_t *self, unsigned long op_type)
{
	self->op_info.curr = 0;
	self->op_info.op_entry.op_type = op_type;
}
EXPORT_SYMBOL(ts_set_op);

void *ts_alloc_operand(ts_thread_struct_t *self, int size)
{
	void *tgt;

	tgt = self->op_info.op_entry.opd + self->op_info.curr;
	self->op_info.curr += size;
	ts_assert(self->op_info.curr <= TS_MAX_OPERAND_SIZE);
	return tgt;
}
EXPORT_SYMBOL(ts_alloc_operand);

void ts_memcpy_operand(ts_thread_struct_t *self, void *opd, int size)
{
	void *tgt;

	tgt = ts_alloc_operand(self, size);
	memcpy(tgt, opd, size);
}
EXPORT_SYMBOL(ts_memcpy_operand);

int ts_isolation_supported(int isolation)
{
	switch (isolation) {
	case TS_SNAPSHOT:
		return 1;
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	case TS_SERIALIZABILITY:
		return 1;
	case TS_LINEARIZABILITY:
		return 1;
#endif
	}
	return 0;
}
EXPORT_SYMBOL(ts_memcpy_operand);
