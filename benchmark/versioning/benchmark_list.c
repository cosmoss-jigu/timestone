#define _GNU_SOURCE
#include <getopt.h>
#include <sys/time.h>

#include "benchmark_list.h"
#include "numa-config.h"
#include "zipf/zipf.h"
int getCPUid(int index, int reset);
static void barrier_init(barrier_t *b, int n)
{
	pthread_cond_init(&b->complete, NULL);
	pthread_mutex_init(&b->mutex, NULL);
	b->count = n;
	b->crossing = 0;
}

static void barrier_cross(barrier_t *b)
{
	pthread_mutex_lock(&b->mutex);
	b->crossing++;
	if (b->crossing < b->count)
		pthread_cond_wait(&b->complete, &b->mutex);
	else {
		pthread_cond_broadcast(&b->complete);
		b->crossing = 0;
	}
	pthread_mutex_unlock(&b->mutex);
}

#define DEFAULT_DURATION 1000
#define DEFAULT_NTHREADS 1
#define DEFAULT_ISIZE    256
#define DEFAULT_VRANGE   512
#define DEFAULT_URATIO   500
#define DEFAULT_ZIPF_DIST_VAL   0.0

void print_args(void)
{
	printf("list benchmark:\n");
	printf("options:\n");
	printf("  -h: print help message\n");
	printf("  -d: test running time in milliseconds (default %d)\n", DEFAULT_DURATION);
	printf("  -n: number of threads (default %d)\n", DEFAULT_NTHREADS);
	printf("  -i: initial size of the list (default %d)\n", DEFAULT_ISIZE);
	printf("  -r: range of value (default %d)\n", DEFAULT_VRANGE);
	printf("  -u: update ratio (0~1000, default %d/1000)\n", DEFAULT_URATIO);
	printf("  -z: zipf-dist-val (greater than or equal 0, default %lf)\n", DEFAULT_ZIPF_DIST_VAL);
}


//////////////////////////////////////
// GLOBALS
//////////////////////////////////////
static volatile int stop;
static cpu_set_t cpu_set[450];

static void *bench_thread(void *data)
{
	unsigned long op;
	int key;
	pthread_data_t *d = (pthread_data_t *)data;
	struct zipf_state zs;

	zipf_init(&zs, d->range, d->zipf_dist_val, rand_r(&d->seed));

	// thread_init

#ifdef THREAD_PINNING
	sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set[d->id]);
#else
	sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set[0]);
#endif
	barrier_cross(d->barrier);
	while (stop == 0) {
		// do somthing;
		op = rand_r(&d->seed) % 1000;
		if (d->zipf)
			key = zipf_next(&zs);
		else
			key = rand_r(&d->seed) % d->range;
		
		if (op < d->update_ratio) {
			if (op < d->update_ratio / 2) {
				list_ins(key, d);
				d->nr_ins++;
			} else {
				list_del(key, d);
				d->nr_del++;
			}
		} else {
			list_find(key, d);
			d->nr_find++;
		}
	}

	// thread cleanup

	return NULL;
}

int getCPUid(int index, int reset)
{

        static int cur_socket = 0;
        static int cur_physical_cpu = 0;
        static int cur_smt = 0;

        if(reset){
                cur_socket = 0;
                cur_physical_cpu = 0;
                cur_smt = 0;
                return 1;
        }

        int ret_val = OS_CPU_ID[cur_socket][cur_physical_cpu][cur_smt];
        cur_physical_cpu++;

        if(cur_physical_cpu == NUM_PHYSICAL_CPU_PER_SOCKET){
                cur_physical_cpu = 0;
                cur_socket++;
                if(cur_socket == NUM_SOCKET){
                        cur_socket = 0;
                        cur_smt++;
                        if(cur_smt == SMT_LEVEL)
                                cur_smt = 0;
                }
        }

        return ret_val;

}


int main(int argc, char *argv[]) {
	struct option bench_options[] = {
		{"help",           no_argument,       NULL, 'h'},
		{"duration",       required_argument, NULL, 'd'},
		{"num-of-threads", required_argument, NULL, 'n'},
		{"initial-size",   required_argument, NULL, 'i'},
		{"range",          required_argument, NULL, 'r'},
		{"update-rate",    required_argument, NULL, 'u'},
		{0,                0,                 0,    0  }
	};

	int i, c;
	int duration = DEFAULT_DURATION;
	int nr_threads = DEFAULT_NTHREADS;
	int init_size = DEFAULT_ISIZE;
	int value_range = DEFAULT_VRANGE;
	int update_ratio = DEFAULT_URATIO;
	barrier_t barrier;
	pthread_attr_t attr;
	pthread_t *threads;
	pthread_data_t **data;
	struct timeval start, end;
	struct timespec timeout;
	unsigned long nr_read, nr_write, nr_txn, nr_abort;
	int zipf = 0;
	double zipf_dist_val = DEFAULT_ZIPF_DIST_VAL;
	void *list;

	stop = 0;

	while (1) {
		c = getopt_long(argc, argv, "hd:n:i:r:u:z:", bench_options, &i);

		if (c == -1)
			break;

		if (c == 0 && bench_options[i].flag == 0)
			c = bench_options[i].val;

		switch(c) {
		case 'h':
			print_args();
			goto out;
		case 'd':
			duration = atoi(optarg);
			break;
		case 'n':
			nr_threads = atoi(optarg);
			break;
		case 'i':
			init_size = atoi(optarg);
			break;
		case 'r':
			value_range = atoi(optarg);
			break;
		case 'u':
			update_ratio = atoi(optarg);
			break;
		case 'z':
			zipf_dist_val = atof(optarg);
			break;
		default:
			printf("Error while processing options.\n");
			goto out;
		}
	}

	if (duration <= 0) {
		printf("invalid test time\n");
		goto out;
	}
	if (nr_threads <= 0) {
		printf("invalid thread number\n");
		goto out;
	}
	if (init_size > value_range) {
		printf("list initial size should not be larger than value range\n");
		goto out;
	}
	if (update_ratio < 0 || update_ratio > 1000) {
		printf("update ratio should be between 0 and 1000\n");
		goto out;
	}
	if (zipf_dist_val < 0.0) {
		printf("zipf dist val should be greater than or equal 0\n");
		goto out;
	}

	if (zipf_dist_val > 0.0)
		zipf = 1;

	printf("List benchmark\n");
	printf("Test time:     %d\n", duration);
	printf("Thread number: %d\n", nr_threads);
	printf("Initial size:  %d\n", init_size);
	printf("Value range:   %d\n", value_range);
	printf("Update Ratio:  %d/1000\n", update_ratio);
	printf("Zipf dist:     %d\n", zipf);
	printf("Zipf dist val: %lf\n", zipf_dist_val);

	timeout.tv_sec = duration / 1000;
	timeout.tv_nsec = (duration % 1000) * 1000000;

	if ((threads = (pthread_t *)malloc(nr_threads * sizeof(pthread_t))) == NULL) {
		printf("failed to malloc pthread_t\n");
		goto out;
	}
	if ((data = (pthread_data_t **)malloc(nr_threads * sizeof(pthread_data_t *))) == NULL) {
		printf("failed to malloc pthread_data_t\n");
		goto out;
	}
	for (i = 0; i < nr_threads; i++) {
		if ((data[i] = alloc_pthread_data()) == NULL) {
			printf("failed to malloc pthread_data_t %d\n", i);
			goto out;
		}
	}

	srand(time(0));
	// global init
	if ((list = list_global_init(init_size, value_range)) == NULL) {
		printf("failed to do list_global_init\n");
		goto out;
	}
#ifndef THREAD_PINNING
	int j = 0;
	CPU_ZERO(&cpu_set[0]);
	for(j = 0; j < nr_threads;j++){
		int cpuid = getCPUid(j,0);
		CPU_SET(cpuid, &cpu_set[0]);
	}
	getCPUid(0,1);
#endif

	barrier_init(&barrier, nr_threads + 1);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	for (i = 0; i < nr_threads; i++) {
#ifdef THREAD_PINNING
		int cpuid = getCPUid(i, 0);
		CPU_ZERO(&cpu_set[i]);
		CPU_SET(cpuid, &cpu_set[i]);
#endif
		data[i]->id = i;
		data[i]->nr_ins = 0;
		data[i]->nr_del = 0;
		data[i]->nr_find = 0;
		data[i]->nr_txn = 0;
		data[i]->range = value_range;
		data[i]->update_ratio = update_ratio;
		data[i]->seed = rand();
		data[i]->zipf = zipf;
		data[i]->zipf_dist_val = zipf_dist_val;
		data[i]->barrier = &barrier;
		data[i]->list = list;
		if (list_thread_init(data[i], data, nr_threads)) {
			printf("failed to do list_thread_init\n");
			goto out;
		}
		if (pthread_create(&threads[i], &attr, bench_thread, (void *)(data[i])) != 0) {
			printf("failed to create thread %d\n", i);
			goto out;
		}
	}
	pthread_attr_destroy(&attr);

	printf("STARTING THREADS...\n");
	barrier_cross(&barrier);

	gettimeofday(&start, NULL);
	nanosleep(&timeout, NULL);
	stop = 1;
	gettimeofday(&end, NULL);
	printf("STOPPING THREADS...\n");

	for (i = 0; i < nr_threads; i++) {
		if (pthread_join(threads[i], NULL) != 0) {
			printf("failed to join child thread %d\n", i);
			goto out;
		}
	}

#ifdef TIMESTAMP
	for (i = 0; i < nr_threads; i++) {
		thread_finish(data[i]);	
	}
	global_finish();
#endif

	duration = (end.tv_sec * 1000 + end.tv_usec / 1000) -
	           (start.tv_sec * 1000 + start.tv_usec / 1000);
	nr_read = 0;
	nr_write = 0;
	nr_txn = 0;
        nr_abort = 0;
	for (i = 0;  i < nr_threads; i++) {
		printf("Thread %d: ins %lu, del %lu, find %lu\n", i,
		       data[i]->nr_ins, data[i]->nr_del, data[i]->nr_find);
		nr_read += (data[i]->nr_find);
		nr_write += (data[i]->nr_ins + data[i]->nr_del);
		nr_txn += (data[i]->nr_txn);
		nr_abort += (data[i]->nr_abort);
	}

	printf("List benchmark ends:\n");
	printf("  duration: %d ms\n", duration);
	printf("  ops:      %lu (%f/s)\n", nr_read + nr_write, (nr_read + nr_write) * 1000.0 / duration);
	printf("  txn:      %lu (%f/s)\n", nr_txn, (nr_txn) * 1000.0 / duration);
	printf("  abort_ratio:      %f \n", 1.0*(nr_abort)/(nr_read+nr_write+nr_abort));
	printf("  abort:      %lu \n", (nr_abort));

	for (i = 0; i < nr_threads; i++) {
#ifndef TIMESTAMP
		free_pthread_data(data[i]);
#else
		free(data[i]);
#endif
	}

	list_global_exit(list);
	free(data);
	free(threads);

#ifdef TIMESTAMP
	destroy_nvm();
#endif

out:
	return 0;
}
