#ifndef _TIMESTONE2_H
#define _TIMESTONE2_H

#ifdef __cplusplus
extern "C" {
#endif

#define TS_AHS_SIZE (40)

int _ts_try_lock(ts_thread_struct_t *self, void **p_p_obj, size_t size);
int _ts_try_lock_const(ts_thread_struct_t *self, void *obj, size_t size);
void _ts_assign_pointer(void **p_ptr, void *p_obj);

#ifdef __cplusplus
}
#endif
#endif /* _TIMESTONE2_H */
