// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifndef _PORT_USER_H
#define _PORT_USER_H

#include <sys/mman.h>
#include <limits.h>
#include <stdlib.h>
#include <malloc.h>

#ifdef __cplusplus
extern "C" {
#endif

#define __init
#define EXPORT_SYMBOL(sym)
#define early_initcall(fn)

/*
 * Log region
 */

#define BITMAP_SIZE 1024
#define FULLY_ALLOCATED 0xFFFFFFFFFFFFFFFFul

typedef struct tvlog_region_allocator {
	/*
	 *               size x n_log
	 *   _____________________________________
	 *  /                                     \
	 * +---------------------------------------+
	 * | size | size | ...                     |
	 * +---------------------------------------+
	 *  \                                       \
	 *   + start_addr                            + end_addr
	 */
	unsigned long size;
	int num;
	volatile unsigned long bitmap[BITMAP_SIZE]; /* 2**16 */
} tvlog_region_allocator_t;

extern tvlog_region_allocator_t g_lr;
extern void *g_start_addr __read_mostly;
extern void *g_end_addr __read_mostly;

static inline int port_tvlog_region_init(unsigned long size, unsigned long num)
{
	unsigned long region_size;

	memset(&g_lr, 0, sizeof(g_lr));
	region_size = size * num;
	g_lr.size = size;
	g_lr.num = num;
	g_start_addr = mmap(NULL, region_size, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (unlikely(g_start_addr == MAP_FAILED))
		return errno;
	g_end_addr = (char *)g_start_addr + region_size;
	return 0;
}

static inline void port_tvlog_region_destroy(void)
{
	if (unlikely(g_start_addr == NULL))
		return;

	munmap(g_start_addr, g_lr.size * g_lr.num);
	g_start_addr = g_end_addr = NULL;
}

static inline void *port_alloc_tvlog_mem(void)
{
	unsigned long mask, bitmap;
	int pos, i, j;
	void *addr;

	/* Test i-th long in the bitmap */
	for (i = 0; i < BITMAP_SIZE; ++i) {
	retry:
		if (g_lr.bitmap[i] == FULLY_ALLOCATED)
			continue;
		/* Test j-th bit in the i-th long */
		for (j = 0; j < 64; ++j) {
			pos = (i * 64) + j;
			if (pos >= g_lr.num)
				return NULL;

			/* If the bit is turned off, turn it on. */
			mask = 0x1ul << j;
			bitmap = g_lr.bitmap[i];
			if ((bitmap & mask) == 0) {
				if (!smp_cas(&g_lr.bitmap[i], bitmap,
					     bitmap | mask))
					goto retry;
				/* Succeed */
				addr = (char *)g_start_addr + (pos * g_lr.size);
				return addr;
			}
		}
	}

	return NULL;
}

static inline void port_free_tvlog_mem(void *addr)
{
	unsigned long mask, bitmap;
	int pos, i, j, rc;

	/* Unmap the log space */
	madvise(addr, g_lr.size, MADV_DONTNEED);

	/* Calculate a position in the bitmap */
	pos = ((char *)addr - (char *)g_start_addr) / g_lr.size;
	i = pos / 64;
	j = pos - (i + 64);
	mask = 0x1ul << j;

	/* Turn off the bit */
	do {
		bitmap = g_lr.bitmap[i];
		rc = smp_cas(&g_lr.bitmap[i], bitmap, bitmap & ~mask);
	} while (!rc);
}

static inline int port_addr_in_tvlog_region(void *addr)
{
	return addr >= g_start_addr && addr < g_end_addr;
}

/*
 * Memory allocation
 */

#define PORT_DEFAULT_ALLOC_FLAG 0

static inline void *port_aligned_alloc(size_t alignment, size_t size)
{
	return aligned_alloc(alignment, size);
}

static inline void *port_alloc_x(size_t size, unsigned int __dummy)
{
	return malloc(size);
}

static inline void *port_realloc(void *ptr, size_t size)
{
	return realloc(ptr, size);
}

static inline void port_free(void *ptr)
{
	free(ptr);
}

/*
 * Synchronization
 */

#define port_cpu_relax_and_yield cpu_relax

static inline void port_spin_init(pthread_spinlock_t *lock)
{
	pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE);
}

static inline void port_spin_destroy(pthread_spinlock_t *lock)
{
	pthread_spin_destroy(lock);
}

static inline void port_spin_lock(pthread_spinlock_t *lock)
{
	pthread_spin_lock(lock);
}

static inline int port_spin_trylock(pthread_spinlock_t *lock)
{
	return pthread_spin_trylock(lock) == 0;
}

static inline void port_spin_unlock(pthread_spinlock_t *lock)
{
	pthread_spin_unlock(lock);
}

static inline int port_mutex_init(pthread_mutex_t *mutex)
{
	return pthread_mutex_init(mutex, NULL);
}

static inline int port_mutex_destroy(pthread_mutex_t *mutex)
{
	return pthread_mutex_destroy(mutex);
}

static inline int port_mutex_lock(pthread_mutex_t *mutex)
{
	return pthread_mutex_lock(mutex);
}

static inline int port_mutex_unlock(pthread_mutex_t *mutex)
{
	return pthread_mutex_unlock(mutex);
}

static inline void port_cond_init(pthread_cond_t *cond)
{
	pthread_cond_init(cond, NULL);
}

static inline void port_cond_destroy(pthread_cond_t *cond)
{
	pthread_cond_destroy(cond);
}

/*
 * Thread
 */

static int port_create_thread(const char *name, pthread_t *t,
			      void *(*fn)(void *), void *arg, void *x)
{
	return pthread_create(t, NULL, fn, arg);
}

static void port_finish_thread(void *x)
{
	/* do nothing */
}

static void port_wait_for_finish(pthread_t *t, void *x)
{
	pthread_join(*t, NULL);
}

static inline void port_initiate_wakeup(pthread_mutex_t *mutex,
					pthread_cond_t *cond)
{
	port_mutex_lock(mutex);
	{
		pthread_cond_broadcast(cond);
	}
	port_mutex_unlock(mutex);
}

static inline void port_initiate_nap(pthread_mutex_t *mutex,
				     pthread_cond_t *cond, unsigned long usecs)
{
	struct timespec ts;
	unsigned long nsecs;

	clock_gettime(CLOCK_REALTIME, &ts);
	nsecs = usecs * 1000;
	if (ts.tv_nsec + nsecs > 1000000000)
		ts.tv_sec += nsecs / 1000000000;
	ts.tv_nsec += nsecs % 1000000000;

	port_mutex_lock(mutex);
	{
		pthread_cond_timedwait(cond, mutex, &ts);
	}
	port_mutex_unlock(mutex);
}

#ifdef __cplusplus
}
#endif
#endif /* _PORT_USER_H */
