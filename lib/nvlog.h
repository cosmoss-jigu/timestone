// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifndef _NVLOG_H
#define _NVLOG_H

#include "timestone_i.h"

#ifdef __cplusplus
extern "C" {
#endif

int nvlog_init(ts_nvm_root_obj_t *);
int nvlog_create(ts_thread_struct_t *, ts_nvlog_t *, unsigned long,
		 unsigned short, unsigned short);
void nvlog_load(ts_nvlog_store_t *, ts_nvlog_t *);
void nvlog_destroy(ts_nvlog_t *);
ts_nvlog_entry_hdr_t *nvlog_enq(ts_nvlog_t *, unsigned int);
ts_nvlog_entry_hdr_t *nvlog_deq(ts_nvlog_t *);
void nvlog_enq_persist(ts_nvlog_t *);
void nvlog_deq_persist(ts_nvlog_t *);
ts_nvlog_entry_hdr_t *nvlog_peek_head(ts_nvlog_t *);
void nvlog_set_last_ckpt_clk(unsigned long);
unsigned long nvlog_get_last_ckpt_clk(void);
void nvlog_truncate_tail(ts_nvlog_t *, unsigned long);
void nvlog_truncate_head(ts_nvlog_t *, unsigned long);

static inline unsigned long nvlog_used(ts_nvlog_t *nvlog)
{
	return nvlog->tail_cnt - nvlog->head_cnt;
}

#ifdef __cplusplus
}
#endif
#endif
