#ifndef _QP_H
#define _QP_H

#include "timestone_i.h"

#ifdef __cplusplus
extern "C" {
#endif

int init_qp(void);
void deinit_qp(void);
void register_thread(ts_thread_struct_t *);
void deregister_thread(ts_thread_struct_t *);
void zombinize_thread(ts_thread_struct_t *);
int request_tvlog_reclaim(unsigned char);
int request_ckptlog_reclaim(unsigned char);
void reset_all_stats(void);

#ifdef __cplusplus
}
#endif
#endif
