// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#include "util.h"
#include "debug.h"
#include "port.h"
#include "nvm.h"

#ifndef TS_NVM_IS_DRAM
static ts_nvm_root_obj_t *__g_root_obj;

unsigned long nvm_get_gen_id(void)
{
	return __g_root_obj->gen_id;
}

ts_nvm_root_obj_t *nvm_init_heap(const char *path, size_t sz,
				 int *p_need_recovery)
{
	ts_nvm_root_obj_t *root_obj;
	int is_created, need_recovery;

	/* Load a nvm heap from a file */
	root_obj = nvm_load_heap(path, sz, &is_created);
	if (unlikely(!root_obj)) {
		return NULL;
	}

	/* Initialize the root object and gen id */
	if (root_obj->magic != NVPOOL_MAGIC) {
		/* TODO: check if following code is crash-consistent. */
		root_obj->next = NULL;
		root_obj->last_ckpt_clk = MIN_VERSION;
		root_obj->magic = 0ul;
		flush_to_nvm(root_obj, sizeof(*root_obj));
		wmb();

		root_obj->magic = NVPOOL_MAGIC;
		clwb(&root_obj->magic);
		wmb();

		/* Since the magic number is corrupted,
		 * we need a recovery. */
		need_recovery = 1;
	}

	/* Check if there are logs or not.
	 * If there are, we need a recovery. */
	if (!need_recovery && root_obj->next) {
		need_recovery = 1;
	}

	if (is_created) {
		need_recovery = 0;
	}

	smp_faa(&root_obj->gen_id, 1);
	clwb(&root_obj->gen_id);
	wmb();

	/* Set output */
	*p_need_recovery = need_recovery;
	__g_root_obj = root_obj;

	return root_obj;
}

#else /* TS_NVM_IS_DRAM */

unsigned long nvm_get_gen_id(void)
{
	/* do nothing */
	return 0;
}

ts_nvm_root_obj_t *nvm_init_heap(const char *path, size_t sz,
				 int *p_need_recovery)
{
	static ts_nvm_root_obj_t root_obj;
	*p_need_recovery = 0;
	return &root_obj;
}

void nvm_heap_destroy(void)
{
	/* do nothing */
}

void *nvm_alloc(size_t size)
{
	return port_alloc_x(size, PORT_DEFAULT_ALLOC_FLAG);
}

void *nvm_aligned_alloc(size_t alignment, size_t size)
{
	return port_aligned_alloc(alignment, size);
}

void nvm_free(void *ptr)
{
	port_free(ptr);
}
#endif /* TS_NVM_IS_DRAM */
