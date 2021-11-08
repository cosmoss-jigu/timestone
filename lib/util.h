// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifndef _UTIL_H
#define _UTIL_H

#include "timestone_i.h"
#include "debug.h"
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Alignment functions
 */
static inline void *align_ptr_to_cacheline(void *p)
{
	return (void *)(((unsigned long)p + ~TS_CACHE_LINE_MASK) &
			TS_CACHE_LINE_MASK);
}

static inline unsigned int align_uint_to_cacheline(unsigned int unum)
{
	return (unum + ~TS_CACHE_LINE_MASK) & TS_CACHE_LINE_MASK;
}

static inline int is_ptr_cacheline_aligned(void *p)
{
	return ((unsigned long)p & ~TS_CACHE_LINE_MASK) == 0;
}

static inline size_t align_size_t_to_pmem_page(size_t sz)
{
	return (sz + ~TS_PMEM_PAGE_MASK) & TS_PMEM_PAGE_MASK;
}

/*
 * Locking functions
 */
static inline int try_lock(volatile unsigned int *lock)
{
	if (*lock == 0 && smp_cas(lock, 0, 1))
		return 1;
	return 0;
}

static inline void unlock(volatile unsigned int *lock)
{
	*lock = 0;
}

/*
 * Object access functions
 */

static inline ts_obj_hdr_t *obj_to_obj_hdr(void *obj)
{
	ts_obj_hdr_t *ohdr = (ts_obj_hdr_t *)obj;
	return &ohdr[-1];
}

static inline ts_obj_hdr_t *vobj_to_obj_hdr(volatile void *vobj)
{
	return obj_to_obj_hdr((void *)vobj);
}

static inline void ts_assert_obj_type(void *obj, int type)
{
	ts_assert(obj_to_obj_hdr(obj)->type == type);
}

static inline ts_act_hdr_struct_t *obj_to_ahs(void *obj)
{
	ts_act_hdr_struct_t *ahs;

	ahs = (ts_act_hdr_struct_t *)obj;
	ts_assert_obj_type(obj, TYPE_ACTUAL);
	return &ahs[-1];
}

static inline ts_act_hdr_struct_t *vobj_to_ahs(volatile void *vobj)
{
	return obj_to_ahs((void *)vobj);
}

static inline ts_cpy_hdr_struct_t *_obj_to_chs_unsafe(void *obj)
{
	ts_cpy_hdr_struct_t *chs;

	chs = (ts_cpy_hdr_struct_t *)obj;
	return &chs[-1];
}

static inline ts_cpy_hdr_struct_t *obj_to_chs(void *obj, int type)
{
	ts_cpy_hdr_struct_t *chs = _obj_to_chs_unsafe(obj);
	ts_assert_obj_type(obj, type);
	return chs;
}

static inline ts_cpy_hdr_struct_t *vobj_to_chs(volatile void *vobj, int type)
{
	return obj_to_chs((void *)vobj, type);
}

static inline ts_ckpt_entry_t *obj_to_ckpt_ent(void *obj)
{
	ts_ckpt_entry_t *ckpt_ent;

	ckpt_ent = (ts_ckpt_entry_t *)obj;
	ts_assert_obj_type(obj, TYPE_NVLOG_ENTRY);
	return &ckpt_ent[-1];
}

static inline ts_ckpt_entry_t *vobj_to_ckpt_ent(volatile void *vobj)
{
	return obj_to_ckpt_ent((void *)vobj);
}

static inline ts_wrt_set_t *chs_obj_to_ws(void *obj)
{
	ts_cpy_hdr_struct_t *chs = _obj_to_chs_unsafe(obj);
	ts_assert(obj_to_obj_hdr(obj)->type == TYPE_COPY ||
		  obj_to_obj_hdr(obj)->type == TYPE_FREE ||
		  obj_to_obj_hdr(obj)->type == TYPE_WRT_SET);
	return (ts_wrt_set_t *)chs->cpy_hdr.p_ws;
}

static inline ts_wrt_set_t *vchs_obj_to_ws(volatile void *obj)
{
	return chs_obj_to_ws((void *)obj);
}

static inline int is_obj_actual(ts_obj_hdr_t *obj_hdr)
{
#ifdef ts_disable_addr_actual_type_checking
	/* Test object type based on its type information
	 * in the header. It may cause one cache miss. */

	return obj_hdr->type == TYPE_ACTUAL;
#else
	/* Test if an object is in the log region or not.
	 * If not, it is an actual object. We avoid one
	 * memory reference so we may avoid one cache miss. */
	int ret = !port_addr_in_tvlog_region(obj_hdr);
	ts_assert(ret == (obj_hdr->type == TYPE_ACTUAL));
	return ret;
#endif /* TS_DISABLE_ADDR_ACTUAL_TYPE_CHECKING */
}

static inline ts_act_nvhdr_t *get_act_nvhdr(volatile void *obj)
{
	ts_obj_hdr_t *obj_hdr = vobj_to_obj_hdr(obj);

	if (unlikely(!is_obj_actual(obj_hdr))) {
		ts_assert(obj_hdr->type == TYPE_COPY);
		/* If this is the copy, get the actual object. 
		 * We should start in np_org_act, which is the 
		 * only way to access act_nvhdr_t */
		obj = vobj_to_chs(obj, TYPE_COPY)
			      ->cpy_hdr.p_act_vhdr->np_org_act;
	}

	ts_assert(obj_hdr->type != TYPE_NVLOG_ENTRY);
	return &vobj_to_ahs(obj)->act_nvhdr;
}

static inline ts_act_vhdr_t *get_act_vhdr(void *obj)
{
	ts_obj_hdr_t *obj_hdr = obj_to_obj_hdr(obj);
	ts_act_vhdr_t *p_act_vhdr;
	ts_act_nvhdr_t *p_act_nvhdr;
	ts_ckpt_entry_t *ckpt_entry;

	switch (obj_hdr->type) {
	case TYPE_ACTUAL:
		p_act_nvhdr = &obj_to_ahs(obj)->act_nvhdr;
#if 0 /* TODO: temporarily disable */
		if (p_act_nvhdr->gen_id != get_gen_id())
			return NULL;
#endif
		p_act_vhdr = (ts_act_vhdr_t *)p_act_nvhdr->p_act_vhdr;
		break;
	case TYPE_NVLOG_ENTRY:
		ckpt_entry = obj_to_ckpt_ent(obj);
		ts_assert(ckpt_entry);
		p_act_nvhdr = &vobj_to_ahs(ckpt_entry->ckptlog_hdr.np_org_act)
				       ->act_nvhdr;
		p_act_vhdr = (ts_act_vhdr_t *)p_act_nvhdr->p_act_vhdr;
		break;
	case TYPE_COPY:
		p_act_vhdr = (ts_act_vhdr_t *)obj_to_chs(obj, TYPE_COPY)
				     ->cpy_hdr.p_act_vhdr;
		break;
	default:
		ts_assert(0 && "Never be here");
		p_act_vhdr = NULL;
		break;
	}

	return p_act_vhdr;
}

static inline ts_act_vhdr_t *get_vact_vhdr(volatile void *obj)
{
	return get_act_vhdr((void *)obj);
}

static inline void *
	get_org_act_obj_from_act_nvhdr(volatile ts_act_nvhdr_t *p_act_nvhdr)
{
	ts_act_hdr_struct_t *ahs;

	ahs = (ts_act_hdr_struct_t *)p_act_nvhdr;
	return ahs->obj_hdr.obj;
}

static inline void *get_cur_act_obj(void *obj)
{
	ts_act_vhdr_t *p_act_vhdr = get_act_vhdr(obj);
	if (unlikely(p_act_vhdr))
		return (void *)p_act_vhdr->np_cur_act;
	return obj;
}

static inline void *get_org_act_obj(void *obj)
{
	ts_act_vhdr_t *p_act_vhdr = get_act_vhdr(obj);
	if (unlikely(p_act_vhdr))
		return (void *)p_act_vhdr->np_org_act;
	return obj;
}

static inline ts_act_hdr_struct_t *
	get_org_ahs(volatile ts_act_vhdr_t *p_act_vhdr)
{
	return vobj_to_ahs(p_act_vhdr->np_org_act);
}

static inline unsigned int get_entry_size(const ts_cpy_hdr_struct_t *chs)
{
	return chs->obj_hdr.obj_size + chs->obj_hdr.padding_size;
}

static inline unsigned int sizeof_chs(const ts_cpy_hdr_struct_t *chs)
{
	return sizeof(*chs) + get_entry_size(chs);
}

static inline void assert_chs_type(const ts_cpy_hdr_struct_t *chs)
{
	ts_assert(chs->obj_hdr.type == TYPE_WRT_SET ||
		  chs->obj_hdr.type == TYPE_COPY ||
		  chs->obj_hdr.type == TYPE_FREE ||
		  chs->obj_hdr.type == TYPE_BOGUS);
}

static inline ts_act_vhdr_t *alloc_act_vhdr(void *np_org_act)
{
	ts_act_vhdr_t *p_act_vhdr;

	ts_assert(np_org_act);

	/* p_act_vhdr should be aligned to
	 * a cacheline size to reduce false sharing. */
	p_act_vhdr = (ts_act_vhdr_t *)port_aligned_alloc(TS_CACHE_LINE_SIZE,
							 sizeof(*p_act_vhdr));
	if (unlikely(p_act_vhdr == NULL))
		return NULL;

	p_act_vhdr->p_copy = NULL;
	p_act_vhdr->p_lock = NULL;
	/* TODO: check if we need atomic update for
	 * np_org_act and sfence*/
	p_act_vhdr->np_org_act = np_org_act;
	p_act_vhdr->np_cur_act = np_org_act;
	p_act_vhdr->tombstone_clk = MAX_VERSION;
	smp_wmb();
	return p_act_vhdr;
}

static inline void free_act_vhdr(ts_act_vhdr_t *p_act_vhdr)
{
	/* TODO: kernel port? jemalloc? */
	return free(p_act_vhdr);
}

/*
 * Pointer set
 */

static inline int ptrset_init(ts_ptr_set_t *ptrset)
{
	ts_assert(ptrset->ptrs == NULL);

	ptrset->num_ptrs = 0;
	ptrset->num_max_ptrs = TS_INIT_PTR_SET_SIZE;
	ptrset->ptrs =
		(void **)port_alloc(ptrset->num_max_ptrs * sizeof(void *));
	if (unlikely(ptrset->ptrs == 0)) {
		return ENOMEM;
	}

	return 0;
}

static inline void ptrset_deinit(ts_ptr_set_t *ptrset)
{
	if (ptrset->ptrs) {
		port_free(ptrset->ptrs);
		ptrset->ptrs = NULL;
	}
}

static inline int ptrset_expand(ts_ptr_set_t *ptrset)
{
	unsigned int new_num;
	void **ptrs;

	new_num = ptrset->num_max_ptrs + TS_INIT_PTR_SET_SIZE;
	ptrs = (void **)port_realloc(ptrset->ptrs, new_num * sizeof(void *));
	if (unlikely(ptrs == NULL)) {
		return ENOMEM;
	}

	ts_trace(TS_FP, "[%s:%d] expand free ptr array: %d(%p)-> %d(%p)\n",
		 __func__, __LINE__, ptrset->num_max_ptrs, ptrset->ptrs,
		 new_num, ptrs);

	ptrset->num_max_ptrs = new_num;
	ptrset->ptrs = ptrs;
	return 0;
}

static inline int ptrset_is_member(ts_ptr_set_t *ptrset, void *p_act)
{
	unsigned int i;

	for (i = 0; i < ptrset->num_ptrs; ++i) {
		if (ptrset->ptrs[i] == p_act)
			return 1;
	}
	return 0;
}

static inline void ptrset_reset(ts_ptr_set_t *ptrset)
{
	ptrset->num_ptrs = 0;
}

static inline int ptrset_push(ts_ptr_set_t *ptrset, void *p)
{
	if (unlikely(ptrset->num_ptrs >= ptrset->num_max_ptrs)) {
		int ret = ptrset_expand(ptrset);
		if (ret) {
			ts_assert(0 && "Fail to expand a ptr_set");
			return ret;
		}
	}

	ptrset->ptrs[ptrset->num_ptrs++] = p;
	return 0;
}

static inline void *ptrset_pop(ts_ptr_set_t *ptrset)
{
	if (ptrset->num_ptrs > 0) {
		return ptrset->ptrs[ptrset->num_ptrs--];
	}
	return NULL;
}

/*
 * list manipulation
 */
static inline void init_ts_list(ts_list_t *list)
{
	list->next = list;
	list->prev = list;
}

static inline void ts_list_add(ts_list_t *_new, ts_list_t *head)
{
	head->next->prev = _new;
	_new->next = head->next;
	_new->prev = head;
	head->next = _new;
}

static inline void ts_list_del(ts_list_t *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

static inline int ts_list_empty(const ts_list_t *head)
{
	return head->next == head && head->prev == head;
}

static inline void ts_list_rotate_left(ts_list_t *head)
{
	/* Rotate a list in counterclockwise direction:
	 *
	 * Before rotation:
	 *  [T3]->{{H}}->[T0]->[T1]->[T2]
	 *  /|\                       |
	 *   +------------------------+
	 *
	 * After rotation:
	 *  [T3]->[T0]->{{H}}->[T1]->[T2]
	 *  /|\                       |
	 *   +------------------------+
	 */
	if (!ts_list_empty(head)) {
		ts_list_t *first;
		first = head->next;
		ts_list_del(first);
		ts_list_add(first, head->prev);
	}
}

/*
 * Misc. functions
 */

static inline const char *req2str(unsigned char req)
{
	switch (req) {
	case RECLAIM_TVLOG_BEST_EFFORT:
		return "RECLAIM_TVLOG_BEST_EFFORT";
	case RECLAIM_TVLOG_CKPT:
		return "RECLAIM_TVLOG_CKPT";
	case RECLAIM_CKPTLOG_BEST_EFFORT:
		return "RECLAIM_CKPTLOG_BEST_EFFORT";
	case RECLAIM_CKPTLOG_WRITEBACK:
		return "RECLAIM_CKPTLOG_WRITEBACK";
	case RECLAIM_CKPTLOG_WRITEBACK_ALL:
		return "RECLAIM_CKPTLOG_WRITEBACK_ALL";
	}
	return "";
}

#ifdef __cplusplus
}
#endif
#endif
