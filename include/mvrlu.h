// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifndef _MVRLU_H
#define _MVRLU_H

#include <timestone.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef ts_thread_struct_t rlu_thread_data_t;

#define RLU_INIT(conf) ts_init(conf);
#define RLU_FINISH() ts_finish()
#define RLU_PRINT_STATS() ts_print_stats()
#define RLU_DESTROY_NVM() ts_destroy_nvm()

#define RLU_THREAD_ALLOC() ts_thread_alloc()
#define RLU_THREAD_FREE(self) ts_thread_free(self)

#define RLU_THREAD_INIT(self) ts_thread_init(self)
#define RLU_THREAD_FINISH(self) ts_thread_finish(self)

#define RLU_READER_LOCK(self) ts_begin(self, TS_SNAPSHOT)
#define RLU_READER_UNLOCK(self) ts_end(self)

#define RLU_ALLOC(size) ts_alloc(size)
#define RLU_FREE(self, p_obj) ts_free(self, p_obj)

#define RLU_TRY_LOCK(self, p_p_obj) ts_try_lock(self, p_p_obj)
#define RLU_TRY_LOCK_CONST(self, obj) ts_try_lock_const(self, obj)
#define RLU_ABORT(self) ts_abort(self)

#define RLU_IS_SAME_PTRS(p_obj_1, p_obj_2) ts_cmp_ptrs(p_obj_1, p_obj_2)
#define RLU_ASSIGN_PTR(self, p_ptr, p_obj) ts_assign_ptr(self, p_ptr, p_obj)

#define RLU_DEREF(self, p_obj) ts_deref(self, p_obj)

#ifdef __cplusplus
}
#endif

#endif /* _MVRLU_H */
