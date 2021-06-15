#include "benchmark_list.h"
#ifdef MVRLU
#include "mvrlu.h"
#else
#include "rlu.h"
#endif

#include <stdio.h>

#define TEST_RLU_MAX_WS 1
//#define TRACE

typedef struct node {
	int value;
	struct node *child[2];
} node_t;

typedef struct rlu_tree {
	node_t *root;
} rlu_tree_t;

static node_t *rlu_new_node(int key)
{
	node_t *node = RLU_ALLOC(sizeof(node_t));

	node->value = key;
	node->child[0] = NULL;
	node->child[1] = NULL;

	return node;
}

pthread_data_t *alloc_pthread_data(void)
{
	pthread_data_t *d;
	size_t pthread_size, rlu_size;

#ifndef MVRLU
	pthread_size = sizeof(pthread_data_t);
	pthread_size = CACHE_ALIGN_SIZE(pthread_size);
	rlu_size = sizeof(rlu_thread_data_t);
	rlu_size = CACHE_ALIGN_SIZE(rlu_size);

	d = (pthread_data_t *)malloc(pthread_size + rlu_size);
	if (d != NULL)
		d->ds_data = ((void *)d) + pthread_size;
#else
	pthread_size = sizeof(pthread_data_t);
	pthread_size = CACHE_ALIGN_SIZE(pthread_size);

	d = (pthread_data_t *)malloc(pthread_size);
	if (d != NULL)
		d->ds_data = RLU_THREAD_ALLOC();
#endif



	return d;
}

void free_pthread_data(pthread_data_t *d)
{
	rlu_thread_data_t *rlu_data = (rlu_thread_data_t *)d->ds_data;

	RLU_THREAD_FINISH(rlu_data);

	free(d);
}

void traverse_pre_order(pthread_data_t *data, node_t *node)
{
        node_t *cur;
        rlu_thread_data_t *rlu_data = (rlu_thread_data_t *)data->ds_data;

        if ( RLU_DEREF(rlu_data, node) == NULL) {
                printf("------------------\n");
                return;
        }

        cur = (node_t *)RLU_DEREF(rlu_data, node);
        printf("[%p] %d\n", cur, cur->value);
        traverse_pre_order(data, RLU_DEREF(rlu_data, (node->child[0])));
        traverse_pre_order(data, RLU_DEREF(rlu_data, (node->child[1])));
}

void print_pre_order(node_t *node)
{
        if (node == NULL) {
                printf("------------------\n");
                return;
        }

        printf("[%p] %d \n", node, node->value);
        print_pre_order(node->child[0]);
        print_pre_order(node->child[1]);
}


void *list_global_init(int init_size, int value_range)
{
	rlu_tree_t *tree;
	node_t *prev, *cur, *new_node;
	int i, key, val, direction;
#ifdef MVRLU
        ts_conf_t conf;

        memcpy(conf.nvheap_path, NVHEAP_POOL_PATH, strlen(NVHEAP_POOL_PATH));
        conf.nvheap_size = NVHEAP_POOL_SIZE;
#endif

	tree = (rlu_tree_t *)malloc(sizeof(rlu_tree_t));
	if (tree == NULL)
		return NULL;
	tree->root = rlu_new_node(INT_MAX);

	i = 0;
	while (i < init_size) {
		key = rand() % value_range;

		prev = tree->root;
		cur = prev->child[0];
		direction = 0;
		while (cur != NULL) {
			prev = cur;
			val = cur->value;
			if (val > key) {
				direction = 0;
				cur = cur->child[0];
			} else if (val < key) {
				direction = 1;
				cur = cur->child[1];
			} else
				break;
		}
		if (cur != NULL)
			continue;
		new_node = rlu_new_node(key);
		if (new_node == NULL)
			return NULL;
		prev->child[direction] = new_node;
		i++;
	}

#ifdef TRACE
	printf("========== print pre-order ==========\n");
	print_pre_order(tree->root);
#endif

#ifdef MVRLU
	RLU_INIT(&conf);
#else
	RLU_INIT();
#endif

	return tree;
}

int list_thread_init(pthread_data_t *data, pthread_data_t **sync_data, int nr_threads)
{
	rlu_thread_data_t *rlu_data = (rlu_thread_data_t *)data->ds_data;

	RLU_THREAD_INIT(rlu_data);

	return 0;
}

void list_global_exit(void *list)
{
	//free l->head;
}

int list_ins(int key, pthread_data_t *data)
{
	rlu_tree_t *tree = (rlu_tree_t *)data->list;
	rlu_thread_data_t *rlu_data = (rlu_thread_data_t *)data->ds_data;
	node_t *prev, *cur, *new_node;
	int direction, ret, val;

restart:
	RLU_READER_LOCK(rlu_data);

	prev = (node_t *)RLU_DEREF(rlu_data, (tree->root));
	cur = (node_t *)RLU_DEREF(rlu_data, (prev->child[0]));
	direction = 0;
	while (cur != NULL) {
		val = cur->value;
		if (val > key) {
			direction = 0;
			prev = cur;
			cur = (node_t *)RLU_DEREF(rlu_data, (cur->child[0]));
		} else if (val < key) {
			direction = 1;
			prev = cur;
			cur = (node_t *)RLU_DEREF(rlu_data, (cur->child[1]));
		} else
			break;
	}
	ret = (cur == NULL);
	if (ret) {
		if (!RLU_TRY_LOCK(rlu_data, &prev)) {
                        data->nr_abort++;
			RLU_ABORT(rlu_data);
			goto restart;
		}
		new_node = rlu_new_node(key);
		RLU_ASSIGN_PTR(rlu_data, &(prev->child[direction]), new_node);
	}

	RLU_READER_UNLOCK(rlu_data);

#ifdef TRACE
	printf("[list_ins] TRAVERSE PRE_ORDER root:[%p] cur:[%p] key:%d\n", 
			RLU_DEREF(rlu_data, (tree->root)), cur, key);
	traverse_pre_order(data, RLU_DEREF(rlu_data, (tree->root)));
#endif
	return ret;
}

int list_del(int key, pthread_data_t *data)
{
	rlu_tree_t *tree = (rlu_tree_t *)data->list;
	rlu_thread_data_t *rlu_data = (rlu_thread_data_t *)data->ds_data;
	node_t *prev, *cur, *prev_succ, *succ, *next;
	node_t *cur_child_l, *cur_child_r;
	int direction, ret, val;
#ifdef CITRUS
	node_t *new_node;
	succ = NULL;
#endif

restart:
	RLU_READER_LOCK(rlu_data);

	prev = (node_t *)RLU_DEREF(rlu_data, (tree->root));
	cur = (node_t *)RLU_DEREF(rlu_data, (prev->child[0]));
 	
	direction = 0;
	while (cur != NULL) {
		val = cur->value;
		if (val > key) {
			direction = 0;
			prev = cur;
			cur = (node_t *)RLU_DEREF(rlu_data, (cur->child[0]));
		} else if (val < key) {
			direction = 1;
			prev = cur;
			cur = (node_t *)RLU_DEREF(rlu_data, (cur->child[1]));
		} else
			break;
	}

	ret = (cur != NULL);
	if (!ret)
		goto out;

	cur_child_l = (node_t *)RLU_DEREF(rlu_data, (cur->child[0]));
	cur_child_r = (node_t *)RLU_DEREF(rlu_data, (cur->child[1]));

	if (cur_child_l == NULL) {
		if (!RLU_TRY_LOCK(rlu_data, &prev) ||
		    !RLU_TRY_LOCK(rlu_data, &cur)) {
                        data->nr_abort++;
			RLU_ABORT(rlu_data);
			goto restart;
		}
		RLU_ASSIGN_PTR(rlu_data, &(prev->child[direction]), cur_child_r);
		goto out;
	}
	if (cur_child_r == NULL) {
		if (!RLU_TRY_LOCK(rlu_data, &prev) ||
		    !RLU_TRY_LOCK(rlu_data, &cur)) {
                        data->nr_abort++;
			RLU_ABORT(rlu_data);
			goto restart;
		}
		RLU_ASSIGN_PTR(rlu_data, &(prev->child[direction]), cur_child_l);
		goto out;
	}
	prev_succ = cur;
	succ = cur_child_r;
	next = (node_t *)RLU_DEREF(rlu_data, (succ->child[0]));
	while (next != NULL) {
		prev_succ = succ;
		succ = next;
		next = (node_t *)RLU_DEREF(rlu_data, next->child[0]);
	}

#ifdef CITRUS
        /* Need to prcu_wait_for_readers()?
         * => instead of prcu_wait_for_readers(),
	 * it's OK because new node doesn't appear
         * to other threads til after RLU_UNLOCK, which commit the new node
         */
        if (prev_succ == cur) {
                if (!RLU_TRY_LOCK(rlu_data, &prev) ||
                    !RLU_TRY_LOCK(rlu_data, &cur) ||
                    !RLU_TRY_LOCK(rlu_data, &succ)) {
                        data->nr_abort++;
                        RLU_ABORT(rlu_data);
                        goto restart;
		}
		/* Make a copy node for replacing to be deleted node */
		new_node = rlu_new_node(succ->value);
		RLU_ASSIGN_PTR(rlu_data, &(new_node->child[0]), cur_child_l);
		RLU_ASSIGN_PTR(rlu_data, &(new_node->child[1]), cur_child_r);
		RLU_ASSIGN_PTR(rlu_data, &(prev->child[direction]), new_node);
		RLU_ASSIGN_PTR(rlu_data, &(new_node->child[1]), succ->child[1]);
	} else {
		if (!RLU_TRY_LOCK(rlu_data, &prev) ||
		    !RLU_TRY_LOCK(rlu_data, &cur) ||
		    !RLU_TRY_LOCK(rlu_data, &prev_succ) ||
		    !RLU_TRY_LOCK(rlu_data, &succ)) {
                        data->nr_abort++;
			RLU_ABORT(rlu_data);
			goto restart;
		}
		new_node = rlu_new_node(succ->value);
		RLU_ASSIGN_PTR(rlu_data, &(new_node->child[0]), cur_child_l);
		RLU_ASSIGN_PTR(rlu_data, &(new_node->child[1]), cur_child_r);
		RLU_ASSIGN_PTR(rlu_data, &(prev->child[direction]), new_node);
                RLU_ASSIGN_PTR(rlu_data, &(prev_succ->child[0]), succ->child[1]);
	}
#else
	
	if (prev_succ == cur) {
		if (!RLU_TRY_LOCK(rlu_data, &prev) ||
		    !RLU_TRY_LOCK(rlu_data, &cur) ||
		    !RLU_TRY_LOCK(rlu_data, &succ)) {
                        data->nr_abort++;
			RLU_ABORT(rlu_data);
			goto restart;
		}
		RLU_ASSIGN_PTR(rlu_data, &(prev->child[direction]), succ);
		RLU_ASSIGN_PTR(rlu_data, &(succ->child[0]), cur_child_l);
	} else {
		if (!RLU_TRY_LOCK(rlu_data, &prev) ||
		    !RLU_TRY_LOCK(rlu_data, &cur) ||
		    !RLU_TRY_LOCK(rlu_data, &prev_succ) ||
		    !RLU_TRY_LOCK(rlu_data, &succ)) {
                        data->nr_abort++;
			RLU_ABORT(rlu_data);
			goto restart;
		}
		RLU_ASSIGN_PTR(rlu_data, &(prev->child[direction]), succ);
		RLU_ASSIGN_PTR(rlu_data, &(prev_succ->child[0]),
		               RLU_DEREF(rlu_data, (succ->child[1])));
		RLU_ASSIGN_PTR(rlu_data, &(succ->child[0]), cur_child_l);
		RLU_ASSIGN_PTR(rlu_data, &(succ->child[1]), cur_child_r);
	}
#endif

out:
#ifdef CITRUS
	if (ret)
		RLU_FREE(rlu_data, cur);
	if (succ != NULL)
		RLU_FREE(rlu_data, succ);

#else
	if (ret)
		RLU_FREE(rlu_data, cur);
#endif
	RLU_READER_UNLOCK(rlu_data);

#ifdef TRACE
	printf("[list_del] TRAVERSE PRE_ORDER root:[%p] cur:[%p] key:%d\n", 
			RLU_DEREF(rlu_data, (tree->root)), cur, key);
	traverse_pre_order(data, RLU_DEREF(rlu_data, (tree->root)));
#endif

	return ret;
}

int list_find(int key, pthread_data_t *data)
{
	rlu_tree_t *tree = (rlu_tree_t *)data->list;
	rlu_thread_data_t *rlu_data = (rlu_thread_data_t *)data->ds_data;
	node_t *cur;
	int ret, val;

	RLU_READER_LOCK(rlu_data);

	cur = (node_t *)RLU_DEREF(rlu_data, (tree->root));
	cur = (node_t *)RLU_DEREF(rlu_data, (cur->child[0]));
	while (cur != NULL) {
		val = cur->value;
		if (val > key) {
			cur = (node_t *)RLU_DEREF(rlu_data, (cur->child[0]));
		} else if (val < key) {
			cur = (node_t *)RLU_DEREF(rlu_data, (cur->child[1]));
		} else
			break;
	}
	ret = (cur != NULL);

	RLU_READER_UNLOCK(rlu_data);

	return ret;
}
