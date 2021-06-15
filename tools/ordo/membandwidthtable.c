/*
 * Create threads, mmap non-overlapping address range, reference it, and unmap it.
 */

#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include "bench.h"

#define ____cacheline_aligned __attribute__((aligned(2 * CACHE_BYTES)))

#define KB (uint64_t)1024
#define MB (KB * 1024)
#define GB (MB * 1024)

#define WRITE_CHUNK_SIZE (4 * KB)
#define READ_CHUNK_SIZE (16 * KB)

#define PHASE_INIT		        1
#define PHASE_SEND_CORE1	    2
#define PHASE_RECV_CORE1	    3
#define PHASE_RESET		        4
#define PHASE_END		        5

struct rdtsc_pair {
    uint64_t t_1;
    uint64_t t_2;
} ____cacheline_aligned;

struct timestamp_tuple_t {
    uint64_t ta;
    uint64_t td;
    uint64_t tb ____cacheline_aligned;
    uint64_t tc;
} ____cacheline_aligned;

struct sync_state_t {
    volatile  uint64_t phase;
    union {
        struct rdtsc_pair p;
        char __padl[CACHE_BYTES];
    } remote_pair ____cacheline_aligned;
    union {
        struct rdtsc_pair p;
        char __padr[CACHE_BYTES];
    } local_pair ____cacheline_aligned;
};

static uint64_t buffer_size = 2 * GB;
static char *buffer;

int niter;
struct timestamp_tuple_t *tuples;
struct sync_state_t *sync_state;

static inline void init_rpair(struct rdtsc_pair *p)
{
    barrier();
    p->t_1 = 0;
    p->t_2 = 0;
    smp_wmb();
}

static void collect_stats(int index)
{
    struct rdtsc_pair *rp;

    rp = &sync_state->remote_pair.p;
    tuples[index].ta = rp->t_1;
    tuples[index].tb = rp->t_2;

    smp_mb();
}

static void remote_worker(int cpu)
{
    struct sched_param param;
    struct rdtsc_pair *p = &sync_state->remote_pair.p;
    struct rdtsc_pair *lp = &sync_state->local_pair.p;
    int index = 0;
    char chunk[READ_CHUNK_SIZE] = {0, };
    const uint64_t chunk_size = READ_CHUNK_SIZE;
    const uint64_t chunks_per_iter = buffer_size / chunk_size;

    setaffinity(cpu);
    memset(&param, 0, sizeof(param));
    param.sched_priority = 1;
    if (sched_setscheduler(getpid(), SCHED_FIFO, &param))
        edie("cannot change policy");

    init_rpair(p);
    init_rpair(lp);

    while (1) {
        static unsigned char is_first_iter = 1;
        char *buff = buffer;
        uint64_t chunks = chunks_per_iter;

        sync_state->phase = PHASE_INIT;

        while(sync_state->phase == PHASE_INIT)
            smp_rmb();

        p->t_1 = usec();
        while (chunks--) {
            memcpy(chunk, buff, chunk_size);
            buff += chunk_size;
        }
        p->t_2 = usec();

        if (is_first_iter)
            is_first_iter = 0;
        else
            collect_stats(index++);

        init_rpair(p);
        init_rpair(lp);
        sync_state->phase = PHASE_RECV_CORE1;

        while (sync_state->phase == PHASE_RECV_CORE1);

        if (sync_state->phase != PHASE_RESET)
            break;
    }
}

static void local_worker(int cpu)
{
    const uint64_t chunk_size = WRITE_CHUNK_SIZE;
    const uint64_t chunks_per_iter = buffer_size / chunk_size;
    const uint64_t values_per_chunk = chunk_size / sizeof(int);
    unsigned iters = niter + 1; // Because we drop the first result.

    setaffinity(cpu);
    while (iters--) {
        uint64_t chunks = chunks_per_iter;
        int *value = (int *)buffer;

        while(sync_state->phase != PHASE_INIT || sync_state->phase == PHASE_RESET)
            smp_rmb();

        while (chunks--) {
            uint64_t values = values_per_chunk;
            srand(time(NULL));
            while (values--)
                *value++ = rand();
        }

        sync_state->phase = PHASE_SEND_CORE1;
        while (sync_state->phase != PHASE_RECV_CORE1)
            smp_rmb();

        if (iters)
            sync_state->phase = PHASE_RESET;
    }
    sync_state->phase = PHASE_END;
}

static void get_stats(int cpu0, int cpu1)
{
    int32_t index;
    int64_t time_delta;
    const int64_t microsecond = 1;

    for (index = 0; index < niter; ++index) {
        struct timestamp_tuple_t *t = &tuples[index];
        time_delta = t->tb - t->ta;
        if (time_delta < microsecond)
            time_delta = microsecond;
        long double MB_per_sec = (2 * 1024) / (long double)(time_delta) * 1000000 ;
        printf("%Lf\n", MB_per_sec);
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
    if (cpu0 == cpu1)
        return 1;

    niter = atoi(av[3]);
    if (niter <= 0)
        niter = 1;

    sync_state = (struct sync_state_t *)mmap(0, sizeof(*sync_state),
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (sync_state == MAP_FAILED)
        edie("mmap sync_state failed");

    tuples = (struct timestamp_tuple_t *)mmap(0, sizeof(struct timestamp_tuple_t) * niter,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (tuples == MAP_FAILED) {
        munmap(sync_state, sizeof(*sync_state));
        edie("mmap tuples failed\n");
    }

    buffer = (char *)mmap(0, buffer_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (buffer == MAP_FAILED) {
        munmap(tuples, sizeof(struct timestamp_tuple_t) * niter);
        munmap(sync_state, sizeof(*sync_state));
        edie("mmap buffer failed\n");
    }

    memset(sync_state, 0, sizeof(*sync_state));
    memset(tuples, 0, sizeof(struct timestamp_tuple_t) * niter);
    memset(buffer, 0, buffer_size);
    memset(&param, 0, sizeof(param));
    param.sched_priority = 1;

    if (sched_setscheduler(getpid(), SCHED_FIFO, &param)) {
        munmap(buffer, buffer_size);
        munmap(tuples, sizeof(struct timestamp_tuple_t) * niter);
        munmap(sync_state, sizeof(*sync_state));
        edie("cannot change policy");
    }

    p = fork();
    if (p < 0) {
        munmap(buffer, buffer_size);
        munmap(tuples, sizeof(struct timestamp_tuple_t) * niter);
        munmap(sync_state, sizeof(*sync_state));
        edie("fork");
    }

    if (!p) {
        remote_worker(cpu1);
        return 0;
    }

    local_worker(cpu0);
    get_stats(cpu0, cpu1);

    munmap(buffer, buffer_size);
    munmap(tuples, sizeof(struct timestamp_tuple_t) * niter);
    munmap(sync_state, sizeof(*sync_state));
    return 0;
}
