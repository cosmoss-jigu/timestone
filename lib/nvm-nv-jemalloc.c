// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifdef TS_NVM_IS_NV_JEMALLOC
#define _GNU_SOURCE
#include <stdlib.h>
#include <jemalloc/jemalloc.h>
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "util.h"
#include "debug.h"
#include "port.h"
#include "nvm.h"

ts_nvm_root_obj_t *nvm_load_heap(const char *path, size_t sz, int *is_created)
{
	ts_nvm_root_obj_t *root_obj;

	/* Get a root pointer */
	*is_created = 1;
	root_obj = nv_calloc(1, sizeof(*root_obj));
	if (unlikely(!root_obj)) {
		return NULL;
	}
	flush_to_nvm(root_obj, sizeof(*root_obj));
	wmb();

	return root_obj;
}

void nvm_heap_destroy(void)
{
	/* Do nothing! */
}

void *nvm_alloc(size_t size)
{
	return nv_malloc(size);
}

void *nvm_aligned_alloc(size_t alignment, size_t size)
{
	return nv_aligned_alloc(alignment, size);
}

void nvm_free(void *ptr)
{
	nv_free(ptr);
}

#endif /* TS_NVM_IS_NV_JEMALLOC */
