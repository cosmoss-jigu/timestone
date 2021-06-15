#include <sys/mman.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include "bench.h"

#define ____cacheline_aligned __attribute__((aligned(2 * CACHE_BYTES)))

struct ttuple {
	uint64_t lat;
} ____cacheline_aligned;

static struct {
	volatile int phase;
} *sync_state;

int niter;
struct ttuple *tuples;
static pthread_barrier_t thbarrier;
static volatile int value ____cacheline_aligned;

static void *remote_worker(void *d)
{
	int cpu = (uintptr_t)d;
	struct sched_param param;

	setaffinity(cpu);
	memset(&param, 0, sizeof(param));
	param.sched_priority = 1;
	if (sched_setscheduler(getpid(), SCHED_FIFO, &param))
		edie("cannot change policy");

	for (;;) {
		pthread_barrier_wait(&thbarrier);
		__sync_val_compare_and_swap(&value, 0, 1);
		pthread_barrier_wait(&thbarrier);
	}
}

static void local_worker(int cpu)
{
	int iter = 0;
	setaffinity(cpu);

	for (; iter < niter; ++iter) {
		pthread_barrier_wait(&thbarrier);
		pthread_barrier_wait(&thbarrier);
		uint64_t t = read_tsc();
		__sync_val_compare_and_swap(&value, 1, 0);
		tuples[iter].lat = read_tsc() - t;
	}

}

static void get_stats(void)
{
	int32_t index;
	uint64_t min = 100000, max = 0;

	for (index = 0; index < niter; ++index) {
		uint64_t v = tuples[index].lat;
		if (max < v)
			max = v;
		if (min > v)
			min = v;
		printf("%Lu\n", tuples[index].lat);
	}
}

int main(int ac, char **av)
{
	int cpu0;
	int cpu1;
	struct sched_param param;
	pthread_t th;

	if (ac < 3)
		die("usage: %s cpu0 cpu1 niter\n", av[0]);

	value = 0;
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

	tuples = (void *) mmap(0, sizeof(struct ttuple) * niter,
			       PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (tuples == MAP_FAILED)
		edie("mmap tuples failed\n");

	memset(sync_state, 0, sizeof(*sync_state));
	memset(tuples, 0, sizeof(struct ttuple) * niter);

	memset(&param, 0, sizeof(param));
	param.sched_priority = 1;
	if (sched_setscheduler(getpid(), SCHED_FIFO, &param))
		edie("cannot change policy");

	pthread_barrier_init(&thbarrier, NULL, 2);
	pthread_create(&th, NULL, &remote_worker, (void *)(uintptr_t)cpu1);
	local_worker(cpu0);
	get_stats();
	return 0;
}
