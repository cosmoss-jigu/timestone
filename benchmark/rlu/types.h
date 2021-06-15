#ifndef _TYPES_H_
#define _TYPES_H_
#include <limits.h>
/////////////////////////////////////////////////////////
// DEFINES
/////////////////////////////////////////////////////////
#define LIST_VAL_MIN (INT_MIN)
#define LIST_VAL_MAX (INT_MAX)
/////////////////////////////////////////////////////////
// TYPES
/////////////////////////////////////////////////////////
#define NODE_PADDING (16)
#define MAX_BUCKETS (1000000)
typedef int val_t;

typedef struct node {
	val_t val;
	struct node *p_next;
#ifdef IS_VERSION
        struct vlist_slot *slots;
#endif
} node_t;

typedef struct list {
	node_t *p_head;
} list_t;

typedef struct hash_list {
	int n_buckets;
	list_t *buckets[MAX_BUCKETS];  
} hash_list_t;

#define VLIST_ENTRIES_PER_TASK 2


typedef struct vlist_record {
	unsigned long epoch;
	struct vlist_record *rec_next;
	int count;
	struct node *nodes[VLIST_ENTRIES_PER_TASK];
	struct vlist_slot *slots[VLIST_ENTRIES_PER_TASK];
} vlist_record_t;

typedef struct vlist_slot {
	unsigned long epoch;
	struct node *next;
	struct vlist_slot *slot_next;
	struct vlist_record *rec;
} vlist_slot_t;
#endif
