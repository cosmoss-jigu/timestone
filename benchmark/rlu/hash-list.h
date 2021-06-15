#ifndef _HASH_LIST_H_
#define _HASH_LIST_H_

/////////////////////////////////////////////////////////
// INCLUDES
/////////////////////////////////////////////////////////
#include "hazard_ptrs.h"
#include "new-urcu.h"
#ifdef IS_MVRLU
#include "mvrlu.h"
#else
#include "rlu.h"
#endif
#include "types.h"
#ifdef IS_VERSION
#include "list_vlist.h"
#endif




/////////////////////////////////////////////////////////
// INTERFACE
/////////////////////////////////////////////////////////
hash_list_t *pure_new_hash_list(int n_buckets);
hash_list_t *harris_new_hash_list(int n_buckets);
hash_list_t *hp_harris_new_hash_list(int n_buckets);
hash_list_t *rcu_new_hash_list(int n_buckets);
hash_list_t *rlu_new_hash_list(rlu_thread_data_t *self, int n_buckets);
hash_list_t *version_new_hash_list(int n_buckets);

int hash_list_size(hash_list_t *p_hash_list);
int rlu_hash_list_size(hash_list_t *p_hash_list);
void hash_list_print(hash_list_t *p_hash_list);

int pure_hash_list_contains(hash_list_t *p_hash_list, val_t val);
int pure_hash_list_add(hash_list_t *p_hash_list, val_t val);
int pure_hash_list_remove(hash_list_t *p_hash_list, val_t val);

int harris_hash_list_contains(hash_list_t *p_hash_list, val_t val);
int harris_hash_list_add(hash_list_t *p_hash_list, val_t val);
int harris_hash_list_remove(hash_list_t *p_hash_list, val_t val);

int hp_harris_hash_list_contains(hp_thread_t *p_hp_td, hash_list_t *p_hash_list, val_t val);
int hp_harris_hash_list_add(hp_thread_t *p_hp_td, hash_list_t *p_hash_list, val_t val);
int hp_harris_hash_list_remove(hp_thread_t *p_hp_td, hash_list_t *p_hash_list, val_t val);

int rcu_hash_list_contains(hash_list_t *p_hash_list, val_t val);
int rcu_hash_list_add(hash_list_t *p_hash_list, val_t val);
int rcu_hash_list_remove(hash_list_t *p_hash_list, val_t val);

int rlu_hash_list_contains(rlu_thread_data_t *self, volatile hash_list_t *p_hash_list, val_t val);
int rlu_hash_list_add(rlu_thread_data_t *self, hash_list_t *p_hash_list, val_t val);
int rlu_hash_list_remove(rlu_thread_data_t *self, volatile hash_list_t *p_hash_list, val_t val);

#ifdef IS_VERSION
int version_hash_list_contains(vlist_pthread_data_t *self, hash_list_t *p_hash_list, val_t val);
int version_hash_list_add(vlist_pthread_data_t *self, hash_list_t *p_hash_list, val_t val);
void vlist_free_node_later(node_t *node, vlist_pthread_data_t *vlist_data);
int version_hash_list_remove(vlist_pthread_data_t *self, hash_list_t *p_hash_list, val_t val);
node_t *vlist_get_next(node_t *node, vlist_pthread_data_t *vlist_data);
void vlist_free_later(void *ptr, vlist_pthread_data_t *vlist_data);
void set_committed_rec(vlist_record_t *rec);
#endif

#endif // _HASH_LIST_H_
