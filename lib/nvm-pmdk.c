// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifdef TS_NVM_IS_PMDK
#include <libpmemobj.h>
#include "util.h"
#include "debug.h"
#include "port.h"
#include "nvm.h"

static PMEMobjpool *__g_pop;

#define POOL_PATH "/mnt/pmem/ts"
#define POOL_SIZE 25ul * 1024ul * 1024ul * 1024ul

ts_nvm_root_obj_t *nvm_load_heap(const char *path, size_t sz, int *is_created)
{
	PMEMoid root;
	ts_nvm_root_obj_t *root_obj;

	/* You should not init twice. */
	ts_assert(__g_pop == NULL);

	/* Open a nvm heap */
	if (access(POOL_PATH, F_OK) != 0) {
		__g_pop = pmemobj_create(POOL_PATH, POBJ_LAYOUT_NAME(nvlog),
					 POOL_SIZE, 0666);
		if (unlikely(!__g_pop)) {
			ts_trace(TS_ERROR, "failed to create pool\n");
			return NULL;
		}
		*is_created = 1;
	} else {
		if ((__g_pop = pmemobj_open(POOL_PATH,
					    POBJ_LAYOUT_NAME(nvlog))) == NULL) {
			ts_trace(TS_ERROR,
				 "failed to open the exsisting pool\n");
			return NULL;
		}
		*is_created = 0;
	}

	/* Allocate a root in the nvmem pool, here on root_obj
	 * will be the entry point to nv pool for all log allocations*/
	root = pmemobj_root(__g_pop, sizeof(ts_nvm_root_obj_t));
	root_obj = pmemobj_direct(root);
	if (!root_obj) {
		return NULL;
	}

	if (*is_created) {
		memset(root_obj, 0, sizeof(*root_obj));
	}

	return root_obj;
}

void nvm_heap_destroy(void)
{
	PMEMoid root;
	ts_nvm_root_obj_t *root_obj;

	/* set the root_obj->next to null to signify safe termination*/
	root = pmemobj_root(__g_pop, sizeof(ts_nvm_root_obj_t));
	root_obj = pmemobj_direct(root);
	root_obj->next = NULL;
	flush_to_nvm(root_obj, sizeof(*root_obj));
	smp_wmb();
	pmemobj_close(__g_pop);
	__g_pop = NULL;
}

void *nvm_alloc(size_t size)
{
	int ret;
	PMEMoid master_obj;

	ret = pmemobj_alloc(__g_pop, &master_obj, size, 0, NULL, NULL);
	/* TODO: need to link each object for recovery */
	if (ret) {
		ts_trace(TS_ERROR, "master_obj allocation failed\n");
		return NULL;
	}
	return pmemobj_direct(master_obj);
}

void *nvm_aligned_alloc(size_t alignment, size_t size)
{
	/* TODO: need to implement aligned alloc */
	return nvm_alloc(size);
}

void nvm_free(void *ptr)
{
	PMEMoid _ptr;

	_ptr = pmemobj_oid(ptr);
	pmemobj_free(&_ptr);
}

#endif /* TS_NVM_IS_PMDK */
