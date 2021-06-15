#ifdef TS_NVM_IS_MAKALU
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "makalu.h"
#include "util.h"
#include "debug.h"
#include "port.h"
#include "nvm.h"

static size_t FILESIZE;
static char *base_addr;
static char *curr_addr;

static void map_persistent_region(const char *path, size_t sz, int *is_created)
{
	void *addr;
	int fd, result;

	/* TODO: implement file existence and size check */
	FILESIZE = sz;
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT,
		  S_IRUSR | S_IWUSR);
	*is_created = 1;
	result = ftruncate(fd, sz);
	if (unlikely(result)) {
		perror("Fail to truncate a file\n");
		exit(1); /* TODO: ugly :( */
	}

	addr = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
		    fd, 0);
	assert(addr != MAP_FAILED);
	close(fd);

	*((intptr_t *)addr) = (intptr_t)addr;
	clwb(addr);
	base_addr = (char *)addr;

	curr_addr = (char *)((size_t)addr + 3 * sizeof(intptr_t));
	*(((intptr_t *)base_addr) + 1) = (intptr_t)curr_addr;
	clwb((((intptr_t *)base_addr) + 1));
	wmb();
}

static int nvm_region_allocator(void **memptr, size_t alignment, size_t size)
{
	char *next;
	char *res;
	size_t aln_adj;

	if (((alignment & (~alignment + 1)) !=
	     alignment) || //should be multiple of 2
	    (alignment < sizeof(void *)))
		return 1; //should be atleast the size of void*
	aln_adj = (size_t)curr_addr & (alignment - 1);

	if (aln_adj != 0)
		curr_addr += (alignment - aln_adj);

	res = curr_addr;
	next = curr_addr + size;
	if (next > base_addr + FILESIZE) {
		printf("\n----Region Manager: out of space in mmaped file-----\n");
		return 1;
	}
	curr_addr = next;
	*(((intptr_t *)base_addr) + 1) = (intptr_t)curr_addr;
	clwb((((intptr_t *)base_addr) + 1));
	wmb();
	*memptr = res;
	return 0;
}

ts_nvm_root_obj_t *nvm_load_heap(const char *path, size_t sz, int *is_created)
{
	void *nvm_heap;
	ts_nvm_root_obj_t *root_obj;
	void **r_addr;

	/* Map persistent memory region and initialize nvm heap. */
	map_persistent_region(path, sz, is_created);
	nvm_heap = MAK_start(&nvm_region_allocator);
	if (unlikely(!nvm_heap)) {
		return NULL;
	}

	/* Get a root pointer */
	r_addr = MAK_persistent_root_addr(0);
	assert(r_addr);
	if (!*r_addr) {
		*is_created = 1;
		root_obj = MAK_malloc(sizeof(*root_obj));
		if (unlikely(!root_obj)) {
			return NULL;
		}
		memset(root_obj, 0, sizeof(*root_obj));
		flush_to_nvm(root_obj, sizeof(*root_obj));
		wmb();
		*r_addr = root_obj;
		clwb(r_addr);
		wmb();
	} else {
		root_obj = *r_addr;
	}

	if (*is_created) {
		memset(root_obj, 0, sizeof(*root_obj));
	}

	return root_obj;
}

void nvm_heap_destroy(void)
{
	MAK_close();
}

void *nvm_alloc(size_t size)
{
	return MAK_malloc(size);
}

void *nvm_aligned_alloc(size_t alignment, size_t size)
{
	/* TODO: need to implement aligned alloc */
	return nvm_alloc(size);
}

void nvm_free(void *ptr)
{
	MAK_free(ptr);
}

#endif /* TS_NVM_IS_MAKALU */
