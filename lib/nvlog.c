#ifndef __KERNEL__
#include "timestone.h"
#else
#include <linux/timestone.h>
#endif

#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include "util.h"
#include "debug.h"
#include "port.h"
#include "nvlog.h"
#include "nvm.h"
#include "clock.h"
#include "qp.h"

/* Global mutex declaration for the nvlog functions */
static pthread_mutex_t nvlog_lock = PTHREAD_MUTEX_INITIALIZER;
static ts_nvm_root_obj_t *nvm_root_obj;

int nvlog_init(ts_nvm_root_obj_t *root_obj)
{
	nvm_root_obj = root_obj;
	return 0;
}

/*
 * nvlog operations
 */
void nvlog_set_last_ckpt_clk(unsigned long last_ckpt_clk)
{
#ifndef TS_NVM_IS_DRAM
	/* Update last ckpt clk first. */
	if (nvm_root_obj->last_ckpt_clk != last_ckpt_clk) {
		memcpy_to_nvm(&nvm_root_obj->last_ckpt_clk, &last_ckpt_clk,
			      sizeof(last_ckpt_clk));
		wmb();
	}
#endif
}

unsigned long nvlog_get_last_ckpt_clk(void)
{
#ifndef TS_NVM_IS_DRAM
	return nvm_root_obj->last_ckpt_clk;
#else
	return MIN_VERSION;
#endif
}

static inline unsigned int nvlog_index(ts_nvlog_t *nvlog, unsigned long cnt)
{
	return cnt & ~nvlog->mask;
}

static inline void *nvlog_at(ts_nvlog_t *nvlog, unsigned long cnt)
{
	return (void *)&nvlog->buffer[nvlog_index(nvlog, cnt)];
}

static inline unsigned long is_power_of_2(unsigned int x)
{
	return x && (!(x & (x - 1)));
}

static void _nvlog_store_destroy(ts_nvlog_store_t *nvlog_store)
{
#ifndef TS_NVM_IS_DRAM
	ts_nvlog_store_t *current = NULL;
	ts_nvlog_store_t *temp = NULL;
#endif

	if (!nvlog_store) {
		ts_trace(TS_ERROR, "invalid pointer\n");
		return;
	}

	pthread_mutex_lock(&nvlog_lock);

#ifndef TS_NVM_IS_DRAM
	/* TODO: check if following code is crash-consistent. */

	/* Get the root object*/
	current = nvm_root_obj->next;

	/* the nvm_root_obj->next points to the first log */
	if (unlikely(current == NULL)) {
		printf("there are no logs to delete\n");
		goto unlock_out;
	}
	/* first log is a match*/
	if (current == nvlog_store) {
		/* if it is the only log exsisting*/
		if (!current->next) {
			nvm_root_obj->next = NULL;
		} else {
			nvm_root_obj->next = current->next;
		}
	}
	/* search for the required log*/
	else {
		while (current != nvlog_store && current != NULL) {
			temp = current;
			current = current->next;
		}

		if (!current) {
			ts_trace(TS_ERROR, "nvlog does not exsists\n");
			goto unlock_out;
		}
		/* handle the last log*/
		if (!current->next) {
			temp->next = NULL;
		} else {
			temp->next = current->next;
		}
	}

#ifndef TS_NVM_IS_MAKALU
	if (current) {
		nvm_free((void *)current->_buffer);
		nvm_free(current);
	}
#else
	/* NOTE: This looks like a makalu bug. */
	ts_trace(TS_INFO,
		 "[%s:%d]Do not free objects at ckptlog_destroy "
		 "when you use makalu allocator\n",
		 __func__, __LINE__);
#endif

unlock_out:
#else
	port_free((void *)nvlog_store->_buffer);
	port_free(nvlog_store);
#endif /* TS_NVM_IS_DRAM */

	pthread_mutex_unlock(&nvlog_lock);
}

static ts_nvlog_store_t *_nvlog_store_create(unsigned long size,
					     unsigned short type,
					     unsigned short status)
{
	ts_nvlog_store_t *nvlog_store = NULL;

	/* size should be power of 2 */
	if (unlikely(!is_power_of_2(size))) {
		ts_trace(TS_ERROR,
			 "The requested log size is not power of 2\n");
		goto err_out;
	}
	pthread_mutex_lock(&nvlog_lock);

#ifndef TS_NVM_IS_DRAM
	/* TODO: check if following code is crash-consistent. */
	nvlog_store = (ts_nvlog_store_t *)nvm_alloc(sizeof(*nvlog_store));
	if (!nvlog_store) {
		ts_trace(TS_ERROR, "failed to allocate nvlog_store\n");
		goto err_out;
	}
	flush_to_nvm(nvlog_store, sizeof(*nvlog_store));
	wmb();

	if (nvm_root_obj->next == NULL) {
		/* TODO: do not directly use pointers,
		 * which is not re-loadable due to ASLR. */
		nvm_root_obj->next = nvlog_store;
		nvlog_store->next = NULL;
	} else {
		nvlog_store->next = nvm_root_obj->next;
		nvm_root_obj->next = nvlog_store;
	}

	/********************************************************************************
	 * alloc (size + CACHE_LINE_SIZE), after the allocation the nvlog               *
	 * in nvm pool looks like							*
	 *										*
	 *										*
	 *+----------+									*
	 *| root_obj | ------------> +-------+	    +-------+       	  +--------+	*
	 *+----------+		     |nvlog1 |----->|nvlog2 |-----> ......|nvlog N |	*
	 *			     +-------+	    +-------+		  +---------	*
	 *				'               '                     '		*
	 *				'               '                     '         *
	 *				'		'		      '		*
	 *			     +-----+        +-----+	          +-----+	*
	 *			     |buff1|	    |buff2|	          |buffN|	*
	 *			     +-----+	    +-----+	          +-----+	*
	 ********************************************************************************/
	nvlog_store->_buffer =
		(unsigned char *)nvm_alloc(size + TS_CACHE_LINE_SIZE);
	if (!nvlog_store->_buffer) {
		ts_trace(TS_ERROR, "failed to allocate nvlog_store->_buffer\n");
		goto err_out;
	}
#else
	nvlog_store = (ts_nvlog_store_t *)port_alloc(sizeof(*nvlog_store));
	if (unlikely(!nvlog_store)) {
		goto err_out;
	}
	nvlog_store->_buffer =
		(unsigned char *)malloc(size + TS_CACHE_LINE_SIZE);
#endif /* TS_NVM_IS_DRAM */

	if (unlikely(!nvlog_store->_buffer)) {
		goto err_out;
	}
	nvlog_store->buffer =
		align_ptr_to_cacheline((void *)nvlog_store->_buffer);
	/* NOTE:
	 * pmemobj_alloc() does not guarantee return cacheline-aligned
	 * address in contrast to the API documentation.
	 * http://pmem.io/pmdk/manpages/linux/v1.4/libpmemobj/pmemobj_alloc.3
	 */
	ts_assert(is_ptr_cacheline_aligned((void *)nvlog_store->buffer));

	nvlog_store->head_cnt = 0;
	nvlog_store->tail_cnt = 0;
	nvlog_store->type = type;
	nvlog_store->status = status;
	nvlog_store->log_size = size;
	nvlog_store->mask = (~(size - 1));
	flush_to_nvm(nvlog_store, sizeof(*nvlog_store));
	wmb();
	pthread_mutex_unlock(&nvlog_lock);
	return nvlog_store;
err_out:
	/* clean up */
	_nvlog_store_destroy(nvlog_store);
	return NULL;
}

void nvlog_load(ts_nvlog_store_t *nvlog_store, ts_nvlog_t *nvlog)
{
	memset(nvlog, 0, sizeof(*nvlog));
	nvlog->nvlog_store = nvlog_store;
	nvlog->buffer = nvlog_store->buffer;
	nvlog->head_cnt = nvlog_store->head_cnt;
	nvlog->tail_cnt = nvlog_store->tail_cnt;
	nvlog->prev_head_cnt = nvlog->head_cnt;
	nvlog->type = nvlog_store->type;
	nvlog->status = nvlog_store->status;
	nvlog->log_size = nvlog_store->log_size;
	nvlog->mask = nvlog_store->mask;
}

int nvlog_create(ts_thread_struct_t *self, ts_nvlog_t *nvlog,
		 unsigned long size, unsigned short type, unsigned short status)
{
	ts_nvlog_store_t *nvlog_store;

	/* Create a nvlog on NVM */
	nvlog_store = _nvlog_store_create(size, type, status);
	if (unlikely(!nvlog_store)) {
		return ENOMEM;
	}

	/* Load its control structure in DRAM */
	nvlog_load(nvlog_store, nvlog);

	/* Initialize the clock */
	nvlog->thread = self;
	nvlog->clks = &self->clks;
	return 0;
}

void nvlog_destroy(ts_nvlog_t *nvlog)
{
	if (nvlog && nvlog->nvlog_store) {
		_nvlog_store_destroy(nvlog->nvlog_store);
		nvlog->nvlog_store = NULL;
	}
}

ts_nvlog_entry_hdr_t *nvlog_enq(ts_nvlog_t *nvlog, unsigned int obj_size)
{
	/* allocate a buffer for a given size
         * only update nvlog->tail_cnt (do not update nvlog_store->tail_cnt */

	ts_nvlog_entry_hdr_t *nvl_entry_hdr;
	unsigned int entry_size;

	/* Make an entry size  */
	entry_size = obj_size + sizeof(ts_nvlog_entry_hdr_t);
	entry_size = align_uint_to_cacheline(entry_size);
	if (entry_size > nvlog->log_size)
		goto overflow_out;

	/* If an allocation wraps around the end of a log,
         * insert a bogus object to prevent such case in real
         * object access.
         *
         *   +-----------------------------------+
         *   |                             |bogus|
         *   +-----------------------------------+
         *      \                           \
         *       \                           +- 1) nvlog->tail_cnt
         *        +- 2) nvlog->tail_cnt + entry_size
         */

	if (unlikely(nvlog_index(nvlog, nvlog->tail_cnt + entry_size) <
		     nvlog_index(nvlog, nvlog->tail_cnt))) {
		unsigned int bogus_size;

		nvl_entry_hdr = nvlog_at(nvlog, nvlog->tail_cnt);
		memset(nvl_entry_hdr, 0, sizeof(*nvl_entry_hdr));
		bogus_size =
			nvlog->log_size - nvlog_index(nvlog, nvlog->tail_cnt);
		nvl_entry_hdr->size = bogus_size;
		nvl_entry_hdr->type = TYPE_BOGUS;

		nvlog->tail_cnt += bogus_size;
		ts_assert(nvlog_index(nvlog, nvlog->tail_cnt) == 0);
	}

	if ((nvlog->tail_cnt + entry_size) - nvlog->head_cnt > nvlog->log_size)
		goto overflow_out;

	/*
         *   +-- ts_nvlog_entry_t --+
         *  /                           \
         * +-----------------------------------------+----
         * |  wrt_clk   size    type     |   entry   | ...
         * +-----------------------------------------+----
         */

	nvl_entry_hdr = nvlog_at(nvlog, nvlog->tail_cnt);
	memset(nvl_entry_hdr, 0, sizeof(*nvl_entry_hdr));
	nvl_entry_hdr->wrt_clk = MAX_VERSION;
	nvl_entry_hdr->size = entry_size;

	/* Update nvlog's tail_cnt */
	nvlog->tail_cnt += nvl_entry_hdr->size;

	ts_assert(nvl_entry_hdr->type != TYPE_BOGUS);
	return nvl_entry_hdr;
overflow_out:
#ifndef TS_GTEST
	/* We assume that the log must be properly reclaimed
	 * by the upper layer so this should not happen. */
	ts_assert(0 && "fail to allocate nvlog");
#endif
	return NULL;
}

void nvlog_enq_persist(ts_nvlog_t *nvlog)
{
	ts_nvlog_store_t *nvlog_store;
	void *buffer;
	unsigned int v_tail_cnt, nv_tail_cnt, i;

	/* sanity check */
	if (unlikely(!nvlog || !nvlog->nvlog_store)) {
		return;
	}

	/* flush nvlog area */
	nvlog_store = nvlog->nvlog_store;
	buffer = (void *)nvlog->buffer;
	v_tail_cnt = nvlog_index(nvlog, nvlog->tail_cnt);
	nv_tail_cnt = nvlog_index(nvlog, nvlog_store->tail_cnt);
	ts_assert(is_ptr_cacheline_aligned(buffer));

	/* - case 0: everything is already flushed out */
	if (nv_tail_cnt == v_tail_cnt) {
		return;
	}

	/* - case 1: no wrap around
	 *
	 *               nv_tail_cnt       v_tail_cnt
	 *              /                 /
	 *  +-----------------------------------------+
	 *  |          |~~to be flushed~~|            |
	 *  +-----------------------------------------+
	 */
	if (nv_tail_cnt < v_tail_cnt) {
		for (i = nv_tail_cnt; i < v_tail_cnt; i += TS_CACHE_LINE_SIZE) {
			clwb(buffer + i);
		}
	}
	/* - case 2: wrap around
	 *
	 *               v_tail_cnt        nv_tail_cnt
	 *              /                 /
	 *  +-----------------------------------------+
	 *  |~~-shed~~~|                 |~~to be flu~|
	 *  +-----------------------------------------+
	 */
	else {
		for (i = nv_tail_cnt; i < nvlog->log_size;
		     i += TS_CACHE_LINE_SIZE) {
			clwb(buffer + i);
		}
		for (i = 0; i < v_tail_cnt; i += TS_CACHE_LINE_SIZE) {
			clwb(buffer + i);
		}
	}

	/* wait until all previous store operations
	 * reach to nvm. */
	wmb();

	/* persist tail_cnt */
	nvlog->nvlog_store->tail_cnt = nvlog->tail_cnt;
	clwb(&nvlog->nvlog_store->tail_cnt);
}

ts_nvlog_entry_hdr_t *nvlog_peek_head(ts_nvlog_t *nvlog)
{
	ts_nvlog_entry_hdr_t *nvl_entry_hdr;

	/* check if nvlog is empty */
	if (unlikely(nvlog->head_cnt == nvlog->tail_cnt))
		return NULL;

	/* update head_cnt */
	nvl_entry_hdr = nvlog_at(nvlog, nvlog->head_cnt);

	/* skip bogus */
	if (unlikely(nvl_entry_hdr->type == TYPE_BOGUS)) {
		nvlog->head_cnt += nvl_entry_hdr->size;
		if (unlikely(nvlog->head_cnt == nvlog->tail_cnt))
			return NULL;
		nvl_entry_hdr = nvlog_at(nvlog, nvlog->head_cnt);
	}

	ts_assert(nvl_entry_hdr->type != TYPE_BOGUS);
	return nvl_entry_hdr;
}

ts_nvlog_entry_hdr_t *nvlog_deq(ts_nvlog_t *nvlog)
{
	ts_nvlog_entry_hdr_t *nvl_entry_hdr;

	/* check if nvlog is empty */
	if (unlikely(nvlog->head_cnt == nvlog->tail_cnt))
		return NULL;

	/* update head_cnt */
	nvl_entry_hdr = nvlog_at(nvlog, nvlog->head_cnt);
	nvlog->head_cnt += nvl_entry_hdr->size;

	/* skip bogus */
	if (unlikely(nvl_entry_hdr->type == TYPE_BOGUS)) {
		if (unlikely(nvlog->head_cnt == nvlog->tail_cnt))
			return NULL;
		nvl_entry_hdr = nvlog_at(nvlog, nvlog->head_cnt);
		nvlog->head_cnt += nvl_entry_hdr->size;
	}

	ts_assert(nvl_entry_hdr->type != TYPE_BOGUS);
	return nvl_entry_hdr;
}

void nvlog_deq_persist(ts_nvlog_t *nvlog)
{
	/* persist head_cnt */
	if (nvlog->nvlog_store->head_cnt != nvlog->head_cnt) {
		nvlog->nvlog_store->head_cnt = nvlog->head_cnt;
		clwb(&nvlog->nvlog_store->head_cnt);
	}
}

void nvlog_truncate_tail(ts_nvlog_t *nvlog, unsigned long tail_cnt)
{
	ts_nvlog_store_t *nvlog_store = nvlog->nvlog_store;

	nvlog->tail_cnt = tail_cnt;
	if (nvlog_store->tail_cnt != tail_cnt) {
		nvlog_store->tail_cnt = tail_cnt;
		clwb(&nvlog_store->tail_cnt);
		wmb();
	}
}

void nvlog_truncate_head(ts_nvlog_t *nvlog, unsigned long head_cnt)
{
	ts_nvlog_store_t *nvlog_store = nvlog->nvlog_store;

	nvlog->head_cnt = head_cnt;
	if (nvlog_store->head_cnt != head_cnt) {
		nvlog_store->head_cnt = head_cnt;
		clwb(&nvlog_store->head_cnt);
		wmb();
	}
}
