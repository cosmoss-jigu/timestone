// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifndef _RECYCLE_H
#define _RECYCLE_H

#include "timestone_i.h"

#ifdef __cplusplus
extern "C" {
#endif

int nvm_recycle_init(ts_nvm_recycle_t *recycle);
void nvm_recycle_deinit(ts_nvm_recycle_t *recycle);
void *nvm_recycle_alloc(ts_nvm_recycle_t *recycle, size_t size);
void nvm_recycle_free(ts_nvm_recycle_t *recycle, void *ptr, size_t size);

#ifdef __cplusplus
}
#endif
#endif
