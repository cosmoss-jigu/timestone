#ifndef _TIMESTONE_H
#define _TIMESTONE_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __KERNEL__
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#else
#include <linux/compiler.h>
#include <linux/sched.h>
#endif /* __KERNEL__ */

/* FIXME [[ */
#define NVHEAP_POOL_PATH "/mnt/pmem"
#define NVHEAP_POOL_SIZE (48 * 1024ul * 1024ul * 1024ul)
/* FIXME ]] */

/*
 * Declaration
 */
enum { TS_INVALID = -1,
       TS_SNAPSHOT = 0,
       TS_SERIALIZABILITY,
       TS_LINEARIZABILITY,
};

typedef struct ts_thread_struct ts_thread_struct_t;
typedef int (*ts_op_exec_fn_t)(ts_thread_struct_t *self, unsigned long op_type,
			       unsigned char *opd);

typedef struct ts_conf {
	char nvheap_path[PATH_MAX];
	size_t nvheap_size;
	ts_op_exec_fn_t op_exec;
} ts_conf_t;

#include "timestone2.h"

/*
 * TimeStone API
 */
int ts_init(ts_conf_t *conf);
void ts_finish(void);
void ts_reset_stats(void);
void ts_print_stats(void);
int ts_isolation_supported(int isolation);

ts_thread_struct_t *ts_thread_alloc(void);
void ts_thread_free(ts_thread_struct_t *self);

void ts_thread_init(ts_thread_struct_t *self);
void ts_thread_init_x(ts_thread_struct_t *self, unsigned short flags);
void ts_thread_finish(ts_thread_struct_t *self);

void *ts_alloc(size_t size);
void ts_stat_alloc_act_obj(ts_thread_struct_t *self, size_t size);
void ts_free(ts_thread_struct_t *self, void *p_obj);

void ts_begin(ts_thread_struct_t *self, int isolation_level);
int ts_end(ts_thread_struct_t *self);
void ts_abort(ts_thread_struct_t *self);

void ts_set_op(ts_thread_struct_t *self, unsigned long op_type);
void *ts_alloc_operand(ts_thread_struct_t *self, int size);
void ts_memcpy_operand(ts_thread_struct_t *self, void *opd, int size);

#define ts_try_lock(self, p_p_obj)                                             \
	_ts_try_lock(self, (void **)p_p_obj, sizeof(**p_p_obj))
#define ts_try_lock_const(self, obj) _ts_try_lock_const(self, obj, sizeof(*obj))

void *ts_deref(ts_thread_struct_t *self, void *p_obj);
#define ts_assign_ptr(self, p_ptr, p_obj)                                      \
	_ts_assign_pointer((void **)p_ptr, p_obj)
int ts_cmp_ptrs(void *p_obj_1, void *p_obj_2);
int ts_get_index(void *p_obj);
void ts_update_index(void *p_obj, int index);
volatile void *ts_get_offset(void *p_obj, int index);
void ts_flush_log(ts_thread_struct_t *self);
void ts_dump_stack(void);
void ts_attach_gdb(void);
#ifdef __cplusplus
}
#endif

#endif /* _TIMESTONE_H */
