// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "timestone_i.h"
#include "port.h"
#include "util.h"
#include "nvlog.h"
#include "nvm.h"
#include "recovery.h"

/*
 * common
 */

static ts_nvlog_t *reconstruct_volatile_nvlog(ts_nvlog_store_t *nvlog_store)
{
	ts_nvlog_t *nvlog;

	nvlog = (ts_nvlog_t *)port_alloc(sizeof(ts_nvlog_t));
	if (unlikely(!nvlog)) {
		perror("volataile log creation failed\n");
		return NULL;
	}

	nvlog_load(nvlog_store, nvlog);
	return nvlog;
}

/*
 * ckptlog recovery
 */

static int reconstruct_volatile_header(ts_ckpt_entry_t *ckpt_entry,
				       unsigned long gen_id)
{
	ts_act_hdr_struct_t *ahs;
	ts_act_nvhdr_t act_nvhdr;
	ts_act_vhdr_t *p_act_vhdr;
	ts_ckpt_entry_t *cur_act_ckpt_entry;

	ahs = obj_to_ahs((void *)ckpt_entry->ckptlog_hdr.np_org_act);
	act_nvhdr = ahs->act_nvhdr;

	if (act_nvhdr.gen_id != gen_id || !act_nvhdr.p_act_vhdr) {
		p_act_vhdr = (ts_act_vhdr_t *)alloc_act_vhdr(
			ckpt_entry->obj_hdr.obj);
		if (unlikely(!p_act_vhdr)) {
			return ENOMEM;
		}
		ahs->act_nvhdr.p_act_vhdr = p_act_vhdr;
		ahs->act_nvhdr.gen_id = gen_id;
		/* TODO: is this crash-consistent? */
	} else {
		p_act_vhdr = (ts_act_vhdr_t *)act_nvhdr.p_act_vhdr;
		cur_act_ckpt_entry = (ts_ckpt_entry_t *)obj_to_ckpt_ent(
			(void *)p_act_vhdr->np_cur_act);
		if (cur_act_ckpt_entry->nvlog_hdr.wrt_clk <
		    ckpt_entry->nvlog_hdr.wrt_clk) {
			p_act_vhdr->np_cur_act = ckpt_entry->obj_hdr.obj;
		}
	}
	return 0;
}

int ckptlog_recovery(ts_recovery_t *recovery)
{
	ts_nvm_root_obj_t *root = recovery->root;
	ts_nvlog_store_t *current = root->next;
	ts_nvlog_t *nvlog;
	ts_ckpt_entry_t *ckpt_entry;
	unsigned long last_ckpt_clk;
	unsigned long gen_id;
	int ret;

	/* Load the last ckpt clk and gen_id */
	last_ckpt_clk = nvlog_get_last_ckpt_clk();
	gen_id = nvm_get_gen_id();

	/* For each ckptlog ... */
	for (; current; current = current->next) {
		if (current->type != TYPE_CKPTLOG ||
		    current->status != STATUS_NVLOG_NORMAL) {
			continue;
		}

		/* For each checkpoint entry in a ckptlog ... */
		nvlog = reconstruct_volatile_nvlog(current);
		if (unlikely(!nvlog)) {
			perror("failed to construct volatile nvlog for ckptlog\n");
			return ENOMEM;
		}
		while (nvlog->head_cnt != nvlog->tail_cnt) {
			/* Reconstruct its volatile header
			 * until the last ckpt clk. */
			ckpt_entry = (ts_ckpt_entry_t *)nvlog_peek_head(nvlog);
			if (ckpt_entry->ckptlog_hdr.ckpt_s_clk >
			    last_ckpt_clk) {
				break;
			}
			ret = reconstruct_volatile_header(ckpt_entry, gen_id);
			if (unlikely(ret)) {
				perror("failed to construct volatile nvlog header for ckptlog\n");
				return ENOMEM;
			}
			nvlog_deq(nvlog);
		}

		/* Truncate tail since they are not necessary for recovery. */
		nvlog_truncate_tail(nvlog, nvlog->head_cnt);

		/* Free nvlog here */
		port_free(nvlog);
	}
	return 0;
}

/*
 * oplog recovery
 */

static inline int is_op_entry_valid(ts_op_entry_t *op_entry,
				    unsigned long last_ckpt_clk)
{
	return op_entry->oplog_hdr.local_clk > last_ckpt_clk;
}

static int alloc_replay_array(ts_nvm_root_obj_t *root,
			      ts_replay_entry_t **p_replay,
			      ts_op_context_t **p_context)
{
	ts_nvlog_store_t *current = root->next;
	ts_nvlog_t *nvlog;
	ts_op_entry_t *op_entry;
	unsigned long last_ckpt_clk, head_cnt;
	int ops_cnt, replay_size, context_size;

	/* Load the last ckpt clk and gen_id */
	last_ckpt_clk = nvlog_get_last_ckpt_clk();

	/* For each oplog ... */
	for (ops_cnt = 0; current; current = current->next) {
		if (current->type != TYPE_OPLOG ||
		    current->status != STATUS_NVLOG_NORMAL) {
			continue;
		}

		/* For each oplog entry in an oplog ... */
		nvlog = reconstruct_volatile_nvlog(current);
		if (unlikely(!nvlog)) {
			perror("failed to construct volatile nvlog for oplog\n");
			return ENOMEM;
		}
		head_cnt = nvlog->head_cnt;
		while (nvlog->head_cnt != nvlog->tail_cnt) {
			/* Reconstruct its volatile header
			 * until the last ckpt clk. */
			op_entry = (ts_op_entry_t *)nvlog_peek_head(nvlog);
			if (is_op_entry_valid(op_entry, last_ckpt_clk)) {
				if (ops_cnt == 0) {
					head_cnt = nvlog->head_cnt;
				}
				ops_cnt++;
			}
			nvlog_deq(nvlog);
		}

		/* Truncate head since they are not necessary for recovery. */
		nvlog_truncate_head(nvlog, head_cnt);

		/* Free nvlog here */
		port_free(nvlog);
	}

	/* Alloc and init replay array */
	replay_size = (sizeof(ts_replay_entry_t) * 2 * ops_cnt);
	*p_replay = (ts_replay_entry_t *)port_alloc(replay_size);
	if (unlikely(!*p_replay)) {
		perror("failed to allocate memory for the array\n");
	}
	memset(*p_replay, 0, replay_size);

	context_size = (sizeof(ts_op_context_t) * ops_cnt);
	*p_context = (ts_op_context_t *)port_alloc(context_size);
	if (unlikely(!*p_context)) {
		perror("failed to allocate memory for the array\n");
	}
	memset(*p_context, 0, context_size);

	return 0;
}

static void add_op_replay(ts_replay_entry_t *replay, ts_op_context_t *context,
			  ts_op_entry_t *op_entry)
{
	context->thread = NULL;
	context->op_entry = op_entry;

	replay[0].time_stamp = op_entry->oplog_hdr.local_clk;
	replay[0].type = OPLOG_REPLAY_TYPE_EXEC;
	replay[0].context = context;

	replay[1].time_stamp = op_entry->nvlog_hdr.wrt_clk;
	replay[1].type = OPLOG_REPLAY_TYPE_COMMIT;
	replay[1].context = context;
}

static int collect_replay_entries(ts_nvm_root_obj_t *root,
				  ts_replay_entry_t *replay_array,
				  ts_op_context_t *context_array, int *ops_cnt)
{
	ts_nvlog_store_t *current = root->next;
	ts_nvlog_t *nvlog;
	ts_op_entry_t *op_entry;
	ts_replay_entry_t *replay;
	ts_op_context_t *context;
	unsigned long last_ckpt_clk, head_cnt;
	int i;

	/* Load the last ckpt clk and gen_id */
	last_ckpt_clk = nvlog_get_last_ckpt_clk();

	/* For each oplog ... */
	for (i = 0; current; current = current->next) {
		if (current->type != TYPE_OPLOG ||
		    current->status != STATUS_NVLOG_NORMAL) {
			continue;
		}

		/* For each oplog entry in an oplog ... */
		/* TODO: need to check why we need this, we already created in
		 * alloc_entries function*/
		nvlog = reconstruct_volatile_nvlog(current);
		if (unlikely(!nvlog)) {
			perror("failed to construct volatile nvlog for oplog in collect entries \n");
			return ENOMEM;
		}
		head_cnt = nvlog->head_cnt;
		while (nvlog->head_cnt != nvlog->tail_cnt) {
			/* Reconstruct its volatile header
			 * until the last ckpt clk. */
			op_entry = (ts_op_entry_t *)nvlog_peek_head(nvlog);
			if (is_op_entry_valid(op_entry, last_ckpt_clk)) {
				replay = &replay_array[i];
				context = &context_array[i / 2];
				add_op_replay(replay, context, op_entry);
				i += 2;
			}
			nvlog_deq(nvlog);
		}

		/* Truncate head since they are not necessary for recovery. */
		nvlog_truncate_head(nvlog, head_cnt);

		/* Free nvlog here */
		port_free(nvlog);
	}

	*ops_cnt = i / 2;
	return 0;
}

static int comp_replay_entries(const void *op1, const void *op2)
{
	ts_replay_entry_t *rop1, *rop2;
	int d;

	rop1 = ((ts_replay_entry_t *)op1);
	rop2 = ((ts_replay_entry_t *)op2);
	d = rop1->time_stamp - rop2->time_stamp;
	if (d == 0) {
		d = rop1->type - rop2->type;
	}
	return d;
}

static void sort_replay_array(ts_replay_entry_t *replay_array, int ops_cnt)
{
	qsort(replay_array, 2 * ops_cnt, sizeof(ts_replay_entry_t),
	      comp_replay_entries);
}

static int acquire_thread(ts_ptr_set_t *threads, ts_replay_entry_t *re_ent)
{
	ts_thread_struct_t *thread;

	/* If a thread is not assigned to the context ... */
	thread = re_ent->context->thread;
	if (!thread) {
		/* First, try to reuse on in the pool ... */
		thread = ptrset_pop(threads);
		if (!thread) {
			/* If the pool is empty,
			 * then allocate and init one. */
			thread = ts_thread_alloc();
			if (unlikely(!thread)) {
				return ENOMEM;
			}
			/* Initialize a new thread for recovery mode. */
			ts_thread_init_x(thread, STATUS_NVLOG_RECOVERY);
		}
		ts_assert(thread);
		/* Since we allocate a new thread,
		 * assigned it to the context. */
		re_ent->context->thread = thread;
	}

	return 0;
}

static void release_thread(ts_ptr_set_t *threads, ts_replay_entry_t *re_ent)
{
	ts_thread_struct_t *thread;

	/* Do nothing for execution */
	if (re_ent->type == OPLOG_REPLAY_TYPE_EXEC) {
		return;
	}

	/* Return the thread to the pool for reuse */
	thread = re_ent->context->thread;
	re_ent->context->thread = NULL;
	ptrset_push(threads, thread);
}

static void init_threads(ts_ptr_set_t *threads)
{
	ptrset_init(threads);
}

static void deinit_threads(ts_ptr_set_t *threads, ts_thread_struct_t *thread)
{
	/* Forcefully return a thread to the pool. */
	if (thread) {
		ptrset_push(threads, thread);
	}

	/* Free all threads in the pool. */
	while ((thread = ptrset_pop(threads))) {
		/* TODO: not sure if following code is
		 * compatible with qp thread operation. */
		ts_thread_finish(thread);
		ts_thread_free(thread);
	}
	ptrset_deinit(threads);
}

static int execute_op(ts_op_exec_fn_t op_exec, ts_replay_entry_t *re_ent)
{
	ts_op_context_t *context;

	context = re_ent->context;
	switch (re_ent->type) {
	case OPLOG_REPLAY_TYPE_EXEC:
		/* Execute an operation in a recovery mode */
		context->thread->in_recovery_mode = 1;
		/* TODO: need to implment the function pointer 
		 * at the user side*/
		op_exec(context->thread, re_ent->type,
			context->op_entry->oplog_hdr.opd);
		break;
	case OPLOG_REPLAY_TYPE_COMMIT:
		/* Commit the operation in a non-recovery mode */
		context->thread->in_recovery_mode = 0;
		ts_end(context->thread);
		break;
	default:
		ts_assert(0 && "Never be there");
		break;
	}
	context->thread->in_recovery_mode = 1;

	return 0;
}

static int replay_ops(ts_recovery_t *recovery, ts_replay_entry_t *replay_array,
		      int ops_cnt)
{
	ts_ptr_set_t threads;
	ts_thread_struct_t *thread = NULL;
	ts_replay_entry_t *re_ent;
	int i, ret = 0;

	/* Init thread set */
	memset(&threads, 0, sizeof(threads));
	init_threads(&threads);

	/* Execute operations */
	for (i = 0; i < (2 * ops_cnt); ++i) {
		/* To execute an operation, assigned a thread */
		re_ent = &replay_array[i];
		ret = acquire_thread(&threads, re_ent);
		if (unlikely(ret)) {
			goto out;
		}

		/* Then execute the operation */
		ret = execute_op(recovery->op_exec, re_ent);
		if (unlikely(ret)) {
			goto out;
		}

		/* Finally, release the thread. */
		release_thread(&threads, re_ent);
	}
	thread = NULL;

out:
	/* Clean up threads */
	deinit_threads(&threads, thread);
	return ret;
}

int oplog_recovery(ts_recovery_t *recovery)
{
	ts_nvm_root_obj_t *root = recovery->root;
	ts_replay_entry_t *replay_array = NULL;
	ts_op_context_t *context_array = NULL;
	int ops_cnt, ret;

	/* Alloc replay array */
	ret = alloc_replay_array(root, &replay_array, &context_array);
	if (unlikely(ret)) {
		goto out;
	}
	printf("oplog: replay array allocated\n");

	/* Collect replay array */
	ret = collect_replay_entries(root, replay_array, context_array,
				     &ops_cnt);
	if (unlikely(ret)) {
		perror("failed to collect op entries\n");
		goto out;
	}
	printf("oplog: replay entries collected\n");

	/* Sort in time stamp order */
	sort_replay_array(replay_array, ops_cnt);
	printf("oplog: replay entries sorted\n");

	/* Replay operations */
	//	replay_ops(recovery, replay_array, ops_cnt);
	printf("oplog: entries replied \n");

out:
	/* Clean up */
	port_free(replay_array);
	port_free(context_array);
	return ret;
}

int perform_recovery(ts_recovery_t *recovery)
{
#ifndef TS_NVM_IS_DRAM
	/* NOTE: This function should be called
	 * after initialize all sub-systems. */

	ts_nvm_root_obj_t *root = recovery->root;
	int ret;

	/* Get nvlog root */
	ts_trace(TS_INFO, "Recovery starts...\n");
	if (unlikely(!root)) {
		return -ENOMEM;
	}

	if (!root->next) {
		/* if root->next is NULL it signifies a safe termination*/
		printf("No Recovery needed, safe termination in the last run.. \n");
		return 0;
	}
	/* Perform checkpoint recovery to get
	 * to the consistent checkpointed status. */
	ret = ckptlog_recovery(recovery);
	if (unlikely(ret)) {
		return ret;
	}
	printf("ckptlog revcovery done\n");

	/* Perform oplog recovery to get to the
	 * recover all completed operations after
	 * being executed the checkpointed status. */
	ret = oplog_recovery(recovery);
	if (unlikely(ret)) {
		return ret;
	}
	/* TODO: destroy the nvlogs after recovery the logs keep adding */
	printf("oplog revcovery done\n");
	ts_trace(TS_INFO, "Recovery succeed...\n");
#endif
	return 0;
}
