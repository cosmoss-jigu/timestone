/*
* Create threads, mmap non-overlapping address range, reference it, and unmap it.
*/

#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "bench.h"

#define ____cacheline_aligned __attribute__((aligned(2 * CACHE_BYTES)))


#define PHASE_INIT		        1
#define PHASE_SEND_CORE1	    2
#define PHASE_RECV_CORE1	    3
#define PHASE_RESET		        4
#define PHASE_END		        5

struct rdtsc_pair {
	uint64_t t_1;
	uint64_t t_2;
} ____cacheline_aligned;

struct timestamp_tuple {
	uint64_t ta;
    uint64_t td;
	uint64_t tb ____cacheline_aligned;
    uint64_t tc;
} ____cacheline_aligned;

static struct {
   volatile int phase;
   union {
	   struct rdtsc_pair p;
	   char __padl[CACHE_BYTES];
   } remote_pair ____cacheline_aligned;
   union {
	   struct rdtsc_pair p;
	   char __padr[CACHE_BYTES];
   } local_pair ____cacheline_aligned;
} *sync_state;

int niter;
struct timestamp_tuple *tuples;

static inline void init_rpair(struct rdtsc_pair *p)
{
	barrier();
	p->t_1 = 0;
	p->t_2 = 0;
	smp_wmb();
}

static void remote_worker(int cpu)
{
	struct sched_param param;
	struct rdtsc_pair *p = &sync_state->remote_pair.p;

	setaffinity(cpu);
	memset(&param, 0, sizeof(param));
	param.sched_priority = 1;
	if (sched_setscheduler(getpid(), SCHED_FIFO, &param))
		edie("cannot change policy");

	for (;;) {
		init_rpair(p);

		sync_state->phase = PHASE_INIT;

		while(sync_state->phase != PHASE_SEND_CORE1)
          smp_rmb();

		p->t_1 = read_tscp();
        __sync_fetch_and_add(&p->t_1, 0);
		p->t_2 = read_tscp();
        __sync_fetch_and_add(&p->t_2, 0);

		sync_state->phase = PHASE_RECV_CORE1;

		barrier();
		while(sync_state->phase == PHASE_RECV_CORE1);

		if (sync_state->phase != PHASE_RESET)
			break;
	}
}

static void collect_stats(int index)
{
	struct rdtsc_pair *rp, *lp;

	lp = &sync_state->local_pair.p;
	rp = &sync_state->remote_pair.p;

	tuples[index].ta = lp->t_1;
    tuples[index].tb = rp->t_1;
    tuples[index].tc = rp->t_2;
    tuples[index].td = lp->t_2;
    smp_mb();
}

static void local_worker(int cpu)
{
	int iter = 0;
	struct rdtsc_pair *p = &sync_state->local_pair.p;
	setaffinity(cpu);

	for (; iter < niter; ++iter) {
		init_rpair(p);

		while(sync_state->phase != PHASE_INIT ||
		      sync_state->phase == PHASE_RESET)
			smp_rmb();

		if (sync_state->phase == PHASE_RESET)
			smp_rmb();

		p->t_1 = read_tscp();
        __sync_fetch_and_add(&p->t_1, 0);
		sync_state->phase = PHASE_SEND_CORE1;
		while(sync_state->phase != PHASE_RECV_CORE1)
			smp_rmb();
        __sync_fetch_and_add(&p->t_1, 0);
		p->t_2 = read_tsc();
        __sync_fetch_and_add(&p->t_2, 0);
		collect_stats(iter);
		if (iter != niter - 1)
			sync_state->phase = PHASE_RESET;
	}
	sync_state->phase = PHASE_END;
}

static void get_stats(int cpu0, int cpu1)
{
	int32_t index;

	for (index = 0; index < niter; ++index) {
      struct timestamp_tuple *t = &tuples[index];
      printf("t(a): %Lu t(b): %Lu t(c): %Lu t(d): %Lu\n",
             t->ta, t->tb, t->tc, t->td);
	}

}

int main(int ac, char **av)
{
	int cpu0;
	int cpu1;
	pid_t p;
	struct sched_param param;

	if (ac < 3)
		die("usage: %s cpu0 cpu1 niter\n", av[0]);

	cpu0 = atoi(av[1]);
	cpu1 = atoi(av[2]);
	niter = atoi(av[3]);

	if (niter <= 0)
		niter = 1;

	if (cpu0 == cpu1)
		return 1;

	sync_state = (void *) mmap(0, sizeof(*sync_state),
				   PROT_READ | PROT_WRITE,
				   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (sync_state == MAP_FAILED)
		edie("mmap sync_state failed");

	tuples = (void *) mmap(0, sizeof(struct timestamp_tuple) * niter,
			      PROT_READ | PROT_WRITE,
			      MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (tuples == MAP_FAILED)
		edie("mmap tuples failed\n");

	memset(sync_state, 0, sizeof(*sync_state));
	memset(tuples, 0, sizeof(struct timestamp_tuple) * niter);

	memset(&param, 0, sizeof(param));
	param.sched_priority = 1;
	if (sched_setscheduler(getpid(), SCHED_FIFO, &param))
		edie("cannot change policy");

	p = fork();
	if (p < 0)
		edie("fork");
	if (!p) {
		remote_worker(cpu1);
		return 0;
	}

	local_worker(cpu0);
	get_stats(cpu0, cpu1);
	return 0;
}
