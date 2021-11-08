// SPDX-FileCopyrightText: Copyright (c) 2019-2021 Virginia Tech
// SPDX-License-Identifier: Apache-2.0

#ifndef __KERNEL__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#endif /* __KERNEL__ */

#include "timestone_i.h"
#include "util.h"
#include "debug.h"
#include "port.h"
#include "clock.h"
#include "qp.h"

#define TS_COLOR_RED "\x1b[31m"
#define TS_COLOR_GREEN "\x1b[32m"
#define TS_COLOR_YELLOW "\x1b[33m"
#define TS_COLOR_BLUE "\x1b[34m"
#define TS_COLOR_MAGENTA "\x1b[35m"
#define TS_COLOR_CYAN "\x1b[36m"
#define TS_COLOR_RESET "\x1b[0m"

#ifdef TS_ENABLE_STATS
ts_stat_t g_stat ____cacheline_aligned2;
#endif

void ts_dump_stack(void)
{
#ifndef __KERNEL__
	/*
	 * quick and dirty backtrace implementation
	 * - http://stackoverflow.com/questions/4636456/how-to-get-a-stack-trace-for-c-using-gcc-with-line-number-information
	 */
	char pid_buf[30];
	char name_buf[512];
	int child_pid;

	sprintf(pid_buf, "%d", getpid());
	name_buf[readlink("/proc/self/exe", name_buf, 511)] = 0;
	child_pid = fork();

	if (!child_pid) {
		dup2(2, 1); /* redirect output to stderr */
		fprintf(stdout, "stack trace for %s pid=%s\n", name_buf,
			pid_buf);
		execlp("gdb", "gdb", "--batch", "-n", "-ex", "thread", "-ex",
		       "bt", name_buf, pid_buf, NULL);
		fprintf(stdout, "gdb is not installed. ");
		fprintf(stdout, "Please, install gdb to see stack trace.");
		abort(); /* If gdb failed to start */
	} else
		waitpid(child_pid, NULL, 0);
#else /* __KERNEL__ */
	dump_stack();
#endif /* __KERNEL__ */
}

void ts_attach_gdb(void)
{
#ifndef __KERNEL__
	char pid_buf[30];
	char name_buf[512];
	int child_pid;

	sprintf(pid_buf, "%d", getpid());
	name_buf[readlink("/proc/self/exe", name_buf, 511)] = 0;
	child_pid = fork();

	if (!child_pid) {
		dup2(2, 1); /* redirect output to stderr */
		fprintf(stdout, "stack trace for %s pid=%s\n", name_buf,
			pid_buf);
		execlp("gdb", "gdb", name_buf, pid_buf, NULL);
		fprintf(stdout, "gdb is not installed. ");
		fprintf(stdout, "Please, install gdb to see stack trace.");
		abort(); /* If gdb failed to start */
	} else
		waitpid(child_pid, NULL, 0);
#endif /* __KERNEL__ */
}

void ts_assert_fail(void)
{
#ifdef __KERNEL__
	BUG();
	/* if that doesn't kill us, halt */
	panic("Oops failed to kill thread");
#else
	fflush(stdout);
	fflush(stderr);
	ts_dump_stack();
#if TS_ATTACH_GDB_ASSERT_FAIL
	rlu_attach_gdb();
#endif /* TS_ATTACH_GDB_ASSERT_FAIL */
	abort();
#endif /* __KERNEL__ */
}

static const char *stat_get_name(int s)
{
	/*
	 * Check out following implementation tricks:
	 * - C preprocessor applications
	 *   https://bit.ly/2H1sC5G
	 * - Stringification
	 *   https://gcc.gnu.org/onlinedocs/gcc-4.1.2/cpp/Stringification.html
	 * - Designated Initializers in C
	 *   https://www.geeksforgeeks.org/designated-initializers-c/
	 */
#undef S
#define S(x) [stat_##x] = #x,
	static const char *stat_string[stat_max__ + 1] = { STAT_NAMES };

	ts_assert(s >= 0 && s < stat_max__);
	return stat_string[s];
}

static void stat_print_cnt(ts_stat_t *stat)
{
	int i;
	for (i = 0; i < stat_max__; ++i) {
		printf("  %30s = %lu\n", stat_get_name(i), stat->cnt[i]);
	}
}

static void print_config(void)
{
	printf(TS_COLOR_GREEN
#ifdef TS_ORDO_TIMESTAMPING
	       "  TS_ORDO_TIMESTAMPING = 1\n"
#else
	       "  TS_ORDO_TIMESTAMPING = 0\n"
#endif
	       TS_COLOR_RESET);
	printf(TS_COLOR_GREEN "  TS_TVLOG_SIZE = %ld\n" TS_COLOR_RESET,
	       TS_TVLOG_SIZE);
	printf(TS_COLOR_GREEN "  TS_TVLOG_LOW_MARK = %ld\n" TS_COLOR_RESET,
	       TS_TVLOG_LOW_MARK);
	printf(TS_COLOR_GREEN "  TS_TVLOG_HIGH_MARK = %ld\n" TS_COLOR_RESET,
	       TS_TVLOG_HIGH_MARK);
	printf(TS_COLOR_GREEN "  TS_OPLOG_SIZE = %ld\n" TS_COLOR_RESET,
	       TS_OPLOG_SIZE);
	printf(TS_COLOR_GREEN "  TS_CKPTLOG_SIZE = %ld\n" TS_COLOR_RESET,
	       TS_CKPTLOG_SIZE);
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	printf(TS_COLOR_RED "  TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY is on."
			    "DO NOT USE FOR BENCHMARK!\n" TS_COLOR_RESET);
#endif
#ifdef TS_NESTED_LOCKING
	printf(TS_COLOR_RED "  TS_NESTED_LOCKING is on.         "
			    "DO NOT USE FOR BENCHMARK!\n" TS_COLOR_RESET);
#endif
#ifdef TS_ENABLE_ASSERT
	printf(TS_COLOR_RED "  TS_ENABLE_ASSERT is on.          "
			    "DO NOT USE FOR BENCHMARK!\n" TS_COLOR_RESET);
#endif
#ifdef TS_NVM_IS_DRAM
	printf(TS_COLOR_RED "  TS_NVM_IS_DRAM is on.            "
			    "DO NOT USE FOR BENCHMARK!\n" TS_COLOR_RESET);
#endif
#ifdef TS_ENABLE_FREE_POISIONING
	printf(TS_COLOR_RED "  TS_ENABLE_FREE_POISIONING is on. "
			    "DO NOT USE FOR BENCHMARK!\n" TS_COLOR_RESET);
#endif
#ifdef TS_ENABLE_STATS
	printf(TS_COLOR_RED "  TS_ENABLE_STATS is on.           "
			    "DO NOT USE FOR BENCHMARK!\n" TS_COLOR_RESET);
#endif
#ifdef TS_TIME_MEASUREMENT
	printf(TS_COLOR_RED "  TS_TIME_MEASUREMENT is on.       "
			    "DO NOT USE FOR BENCHMARK!\n" TS_COLOR_RESET);
#endif
#if defined(TS_ENABLE_TRACE_0) || defined(TS_ENABLE_TRACE_1) ||                \
	defined(TS_ENABLE_TRACE_2) || defined(TS_ENABLE_TRACE_3)
	printf(TS_COLOR_MAGENTA
	       "  TS_ENABLE_TRACE_*  is on.        "
	       "IT MAY AFFECT BENCHMARK RESULTS!\n" TS_COLOR_RESET);
#endif
}

void ts_print_stats(void)
{
	printf("=================================================\n");
	printf("TimeStone configuration:\n");
	printf("-------------------------------------------------\n");
	print_config();
	printf("-------------------------------------------------\n");

	/* It should be called after ts_finish(). */
#ifdef TS_ENABLE_STATS
	printf("TimeStone statistics:\n");
	printf("-------------------------------------------------\n");
	stat_print_cnt(&g_stat);
	printf("-------------------------------------------------\n");
#endif
}
EXPORT_SYMBOL(ts_print_stats);

void ts_reset_stats(void)
{
#ifdef TS_ENABLE_STATS
	reset_all_stats();
	memset(&g_stat, 0, sizeof(g_stat));
#endif
}
EXPORT_SYMBOL(ts_reset_stats);

#if TS_TRACE_LEVEL >= TS_DUMP

void ts_dbg_dump_act_vhdr(const char *f, const int l, ts_act_vhdr_t *p_act_vhdr)
{
	if (!p_act_vhdr)
		return;

	ts_trace(TS_DUMP, "[%s:%d] ts_act_vhdr_t: %p\n", f, l, p_act_vhdr);
	ts_trace(TS_DUMP, "          p_copy:         %p\n", p_act_vhdr->p_copy);
	ts_trace(TS_DUMP, "          p_lock:         %p\n", p_act_vhdr->p_lock);
	ts_trace(TS_DUMP, "          np_org_act:     %p\n",
		 p_act_vhdr->np_org_act);
	ts_trace(TS_DUMP, "          np_cur_act:     %p\n",
		 p_act_vhdr->np_cur_act);
	ts_trace(TS_DUMP, "          tombstone_clk:  %lu\n",
		 p_act_vhdr->tombstone_clk);
}

void ts_dbg_dump_obj_hdr(const char *f, const int l, ts_obj_hdr_t *oh)
{
	if (!oh)
		return;

	ts_trace(TS_DUMP, "[%s:%d] ts_obj_hdr_t: %p\n", f, l, oh);
	ts_trace(TS_DUMP, "          obj_size:      %d\n", oh->obj_size);
	ts_trace(TS_DUMP, "          padding_size:  %d\n", oh->padding_size);
	ts_trace(TS_DUMP, "          type:          %d\n", oh->type);
	ts_trace(TS_DUMP, "          status:        %d\n", oh->status);
	ts_trace(TS_DUMP, "          obj:           %p\n", oh->obj);
}

void ts_dbg_dump_cpy_hdr(const char *f, const int l, ts_cpy_hdr_t *ch)
{
	if (!ch)
		return;

	ts_trace(TS_DUMP, "[%s:%d] ts_cpy_hdr_t: %p\n", f, l, ch);
	ts_trace(TS_DUMP, "          p_copy:        %p\n", ch->p_copy);
	ts_trace(TS_DUMP, "          p_act_vhdr:    %p\n", ch->p_act_vhdr);
	ts_trace(TS_DUMP, "          p_ws:          %p\n", ch->p_ws);
	ts_trace(TS_DUMP, "          wrt_clk_prev:  %lu\n", ch->wrt_clk_prev);
	ts_trace(TS_DUMP, "          wrt_clk:       %lu\n", ch->__wrt_clk);
	ts_trace(TS_DUMP, "          wrt_clk_next:  %lu\n", ch->wrt_clk_next);
}

void ts_dbg_dump_cpy_hdr_struct(const char *f, const int l,
				ts_cpy_hdr_struct_t *chs)
{
	ts_trace(TS_DUMP, "[%s:%d] ts_cpy_hdr_struct_t: %p\n", f, l, chs);
	if (!chs)
		return;

	ts_dbg_dump_cpy_hdr(f, l, &chs->cpy_hdr);
	ts_dbg_dump_obj_hdr(f, l, &chs->obj_hdr);
}

void ts_dbg_dump_version_chain(const char *f, const int l,
			       ts_cpy_hdr_struct_t *chs,
			       unsigned long last_ckpt_clk)
{
	volatile void *p_copy;
	ts_act_vhdr_t *p_act_vhdr;

	ts_trace(TS_DUMP, "[%s:%d] <<<<<< [%s:%d] (chs = %p) \n", f, l,
		 __func__, __LINE__, chs);
	if (!chs)
		return;

	p_act_vhdr = (ts_act_vhdr_t *)chs->cpy_hdr.p_act_vhdr;
	ts_dbg_dump_act_vhdr(__func__, __LINE__, p_act_vhdr);

	p_copy = p_act_vhdr->p_copy;
	if (likely(p_copy)) {
		do {
			chs = vobj_to_chs(p_copy, TYPE_COPY);
			ts_dbg_dump_cpy_hdr_struct(__func__, __LINE__, chs);

			if (unlikely(lt_clock(chs->cpy_hdr.wrt_clk_next,
					      last_ckpt_clk))) /* TIME: < */ {
				break;
			}
			p_copy = chs->cpy_hdr.p_copy;
		} while (p_copy);
	}
}

void ts_dbg_dump_all_version_chain_act_hdr(const char *f, const int l,
					   ts_act_vhdr_t *p_act_vhdr)
{
	ts_cpy_hdr_struct_t *chs;
	volatile void *p_copy;

	ts_trace(TS_DUMP, "[%s:%d] <<<<<< [%s:%d] (p_act_vhdr = %p) \n", f, l,
		 __func__, __LINE__, p_act_vhdr);
	if (!p_act_vhdr)
		return;

	ts_dbg_dump_act_vhdr(__func__, __LINE__, p_act_vhdr);
	ts_trace(TS_DUMP, "------------------------- \n");

	p_copy = p_act_vhdr->p_copy;
	if (likely(p_copy)) {
		do {
			ts_trace(TS_DUMP, "p_copy:%p ---------- \n", p_copy);
			chs = vobj_to_chs(p_copy, TYPE_COPY);
			ts_dbg_dump_cpy_hdr_struct(__func__, __LINE__, chs);

			p_copy = chs->cpy_hdr.p_copy;
		} while (p_copy);
	}
}

void ts_dbg_dump_all_version_chain_chs(const char *f, const int l,
				       ts_cpy_hdr_struct_t *chs)
{
	ts_act_vhdr_t *p_act_vhdr;

	ts_trace(TS_DUMP, "[%s:%d] <<<<<< [%s:%d] (chs = %p) \n", f, l,
		 __func__, __LINE__, chs);
	if (!chs)
		return;

	p_act_vhdr = (ts_act_vhdr_t *)chs->cpy_hdr.p_act_vhdr;
	ts_dbg_dump_all_version_chain_act_hdr(f, l, p_act_vhdr);
}

#endif /* TS_TRACE_LEVEL >= TS_DUMP */
