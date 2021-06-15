#ifndef _LIST_VLIST_H_
#define _LIST_VLIST_H_
#include <assert.h>

#include "qsbr.h"
#include "util.h"
#include <stdlib.h>
#include "types.h"

#define CACHE_ALIGN (192) 
#define CACHE_ALIGN_SIZE(size) ((((size - 1) / CACHE_ALIGN) + 1) * CACHE_ALIGN)



#if 0
typedef struct node {
	int value;
	vlist_slot_t *slots;
} node_t;
#endif
#if 0
typedef struct vlist_list {
	node_t *head;
} vlist_list_t;
#endif

typedef struct vlist_pthread_data {
	vlist_record_t *rec;
	unsigned long epoch;
	vlist_record_t *new_rec;
	unsigned long count;
	qsbr_pthread_data_t *qsbr_data;
        qsbr_pthread_data_t qsbr_data_inst;
} vlist_pthread_data_t;
#define INDIRECT_EPOCH 0
#define INACTIVE_EPOCH 1
#define STARTING_EPOCH 2


#define QSBR_PERIOD 100
void vlist_maybe_quiescent(vlist_pthread_data_t *vlist_data);
node_t *vlist_new_node();
void add_slot(node_t *node, node_t *next, vlist_pthread_data_t *vlist_data);
void vlist_free_node_later(node_t *node, vlist_pthread_data_t *vlist_data);
void vlist_set_read_epoch(vlist_pthread_data_t *vlist_data);
void vlist_read_cs_enter(vlist_pthread_data_t *vlist_data);
void vlist_read_cs_exit(vlist_pthread_data_t *vlist_data);
void vlist_write_cs_enter(vlist_pthread_data_t *vlist_data);
int vlist_write_cs_exit(vlist_pthread_data_t *vlist_data);
int list_thread_init(vlist_pthread_data_t *data, vlist_pthread_data_t **sync_data, int nr_threads);
#endif
