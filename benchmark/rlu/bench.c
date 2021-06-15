#define _GNU_SOURCE
/////////////////////////////////////////////////////////
// INCLUDES
/////////////////////////////////////////////////////////
#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <sys/time.h>
#include <time.h>

#include "hash-list.h"
#include "numa-config.h"
#include "zipf/zipf.h"

int getCPUid(int index, bool reset);
/////////////////////////////////////////////////////////
// DEFINES
/////////////////////////////////////////////////////////

#define RO                              1
#define RW                              0

#ifdef DEBUG
# define IO_FLUSH                       fflush(NULL)
/* Note: stdio is thread-safe */
#endif

#define DEFAULT_BUCKETS                 1
#define DEFAULT_DURATION                10000
#define DEFAULT_INITIAL                 256
#define DEFAULT_NB_THREADS              1
#define DEFAULT_RANGE                   (DEFAULT_INITIAL * 2)
#define DEFAULT_SEED                    0
#define DEFAULT_UPDATE                  200
#define DEFAULT_ZIPF_DIST_VAL           0
#define DEFAULT_OP_CNT                  1
#define DEFAULT_NVHEAP_POOL_PATH        NVHEAP_POOL_PATH
#define DEFAULT_NVHEAP_POOL_SIZE        NVHEAP_POOL_SIZE

#define XSTR(s)                         STR(s)
#define STR(s)                          #s

#define BENCH_ASSERT(cond)						\
	if (__builtin_expect ((unsigned long)(!(cond)), 0)) {		\
		extern void rlu_assert_fail(void);			\
		printf ("\n-----------------------------------------------\n"); \
		printf ("\nAssertion failure: %s:%d '%s'\n", __FILE__, __LINE__, #cond); \
		rlu_assert_fail();					\
	}

/////////////////////////////////////////////////////////
// TYPES
/////////////////////////////////////////////////////////
typedef struct thread_data {
	long uniq_id;
	hash_list_t *p_hash_list;
	struct barrier *barrier;
	unsigned long cmt_add;
	unsigned long cmt_remove;
	unsigned long nb_add;
	unsigned long nb_remove;
	unsigned long nb_contains;
	unsigned long nb_found;
	unsigned short seed[3];
	int initial;
	int diff;
	int range;
	int update;
	int alternate;	
	int zipf;
	int op_cnt;
	double zipf_dist_val;
	rlu_thread_data_t *p_rlu_td;
#ifndef IS_MVRLU
        rlu_thread_data_t rlu_td;
#endif
	hp_thread_t *p_hp_td;
	hp_thread_t hp_td;
#ifdef IS_VERSION
        vlist_pthread_data_t *p_version_td;
        vlist_pthread_data_t version_td;
#endif
	char padding[64];
} thread_data_t;

typedef struct barrier {
  pthread_cond_t complete;
  pthread_mutex_t mutex;
  int count;
  int crossing;
} barrier_t;


/////////////////////////////////////////////////////////
// GLOBALS
/////////////////////////////////////////////////////////
static volatile long padding[50];

static volatile int stop;
static unsigned short main_seed[3];
static cpu_set_t cpu_set[450];

/////////////////////////////////////////////////////////
// HELPER FUNCTIONS
/////////////////////////////////////////////////////////
static inline int MarsagliaXORV (int x) { 
  if (x == 0) x = 1 ; 
  x ^= x << 6;
  x ^= ((unsigned)x) >> 21;
  x ^= x << 7 ; 
  return x ;        // use either x or x & 0x7FFFFFFF
}

static inline int MarsagliaXOR (int * seed) {
  int x = MarsagliaXORV(*seed);
  *seed = x ; 
  return x & 0x7FFFFFFF;
}

static inline void rand_init(unsigned short *seed)
{
  seed[0] = (unsigned short)rand();
  seed[1] = (unsigned short)rand();
  seed[2] = (unsigned short)rand();
}

static inline int rand_range(int n, unsigned short *seed)
{
  /* Return a random number in range [0;n) */
  
  /*int v = (int)(erand48(seed) * n);
  assert (v >= 0 && v < n);*/
  
  int v = MarsagliaXOR((int *)seed) % n;
  return v;
}

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
  /* One more thread through */
  b->crossing++;
  /* If not all here, wait */
  if (b->crossing < b->count) {
    pthread_cond_wait(&b->complete, &b->mutex);
  } else {
    pthread_cond_broadcast(&b->complete);
    /* Reset for next time */
    b->crossing = 0;
  }
  pthread_mutex_unlock(&b->mutex);
}

/////////////////////////////////////////////////////////
// FUNCTIONS
/////////////////////////////////////////////////////////
#ifndef IS_MVRLU
static void global_init(int n_threads) {
	RLU_INIT();
#else
static void global_init(int n_threads, ts_conf_t *conf) {

	RLU_INIT(conf);
#endif
	RCU_INIT(n_threads);
}

static void global_finish() {
#ifndef IS_MVRLU
	RLU_FINISH();
#else
	extern void __ts_finish(void);
	__ts_finish();
#endif
}

static void thread_init(thread_data_t *d) {
	RLU_THREAD_INIT(d->p_rlu_td);
	RCU_THREAD_INIT(d->uniq_id);
#ifdef IS_VERSION
        list_thread_init(d->p_version_td, NULL, 0);
#endif
}

static void thread_finish(thread_data_t *d) {
	RLU_THREAD_FINISH(d->p_rlu_td);
	RCU_THREAD_FINISH();
}

static void destroy_nvm() {
#ifdef IS_MVRLU
	extern void __ts_unload_nvm(void);
	__ts_unload_nvm();
#endif
}

static void print_stats() {
	RLU_PRINT_STATS();
	RCU_PRINT_STATS();
}

static void hash_list_init(hash_list_t **pp, int n_buckets) {
#ifdef IS_RLU
	*pp = rlu_new_hash_list(NULL, n_buckets);
#else
#ifdef IS_RCU
	*pp = rcu_new_hash_list(n_buckets);
#else
#ifdef IS_HARRIS
	*pp = harris_new_hash_list(n_buckets);
#else
#ifdef IS_HAZARD_PTRS_HARRIS
	*pp = hp_harris_new_hash_list(n_buckets);
#else
#ifdef IS_VERSION
        *pp = version_new_hash_list(n_buckets);
#else
	printf("ERROR: benchmark not defined!\n");
	abort();
#endif
#endif
#endif
#endif
#endif
}

static int hash_list_contains(thread_data_t *d, int key) {
#ifdef IS_RLU
	return rlu_hash_list_contains(d->p_rlu_td, d->p_hash_list, key);
#else
#ifdef IS_RCU
	return rcu_hash_list_contains(d->p_hash_list, key);
#else
#ifdef IS_HARRIS
	return harris_hash_list_contains(d->p_hash_list, key);
#else
#ifdef IS_HAZARD_PTRS_HARRIS
	return hp_harris_hash_list_contains(d->p_hp_td, d->p_hash_list, key);
#else
#ifdef IS_VERSION
        return version_hash_list_contains(d->p_version_td, d->p_hash_list, key);
#else
	printf("ERROR: benchmark not defined!\n");
	abort();
#endif
#endif
#endif
#endif
#endif
}

static int hash_list_add(thread_data_t *d, int key) {
#ifdef IS_RLU
	return rlu_hash_list_add(d->p_rlu_td, d->p_hash_list, key);
#else
#ifdef IS_RCU
	return rcu_hash_list_add(d->p_hash_list, key);
#else
#ifdef IS_HARRIS
	return harris_hash_list_add(d->p_hash_list, key);
#else
#ifdef IS_HAZARD_PTRS_HARRIS
	return hp_harris_hash_list_add(d->p_hp_td, d->p_hash_list, key);
#else
#ifdef IS_VERSION
        return version_hash_list_add(d->p_version_td, d->p_hash_list, key);
#else
	printf("ERROR: benchmark not defined!\n");
	abort();
#endif
#endif
#endif
#endif
#endif
}

static int hash_list_remove(thread_data_t *d, int key) {
#ifdef IS_RLU
	return rlu_hash_list_remove(d->p_rlu_td, d->p_hash_list, key);
#else
#ifdef IS_RCU
	return rcu_hash_list_remove(d->p_hash_list, key);
#else
#ifdef IS_HARRIS
	return harris_hash_list_remove(d->p_hash_list, key);
#else
#ifdef IS_HAZARD_PTRS_HARRIS
	return hp_harris_hash_list_remove(d->p_hp_td, d->p_hash_list, key);
#else
#ifdef IS_VERSION
        return version_hash_list_remove(d->p_version_td, d->p_hash_list, key);
#else
	printf("ERROR: benchmark not defined!\n");
	abort();
#endif
#endif
#endif
#endif
#endif
}

static void *test(void *data)
{
	int op, last = -1;
	int key, rc;
	thread_data_t *d = (thread_data_t *)data;
	struct zipf_state zs;
	int _op_cnt;

	zipf_init(&zs, d->range, d->zipf_dist_val, rand_range(d->range, d->seed)+1);

	thread_init(d);
#ifdef THREAD_PINNING
        sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set[d->uniq_id]);
#else
        sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set[0]);
#endif
	if (d->uniq_id == 0) {
		/* Populate set */
		printf("[%ld] Initializing\n", d->uniq_id);
		printf("[%ld] Adding %d entries to set\n", d->uniq_id, d->initial);
		int i = 0;
		while (i < d->initial) {
			key = rand_range(d->range, d->seed) + 1;
			if (hash_list_add(d, key)) {
				i++;
			}
		}
		printf("[%ld] Adding done\n", d->uniq_id);
	}

	/* Wait on barrier */
#ifdef IS_MVRLU
	ts_flush_log(d->p_rlu_td);
        if (d->uniq_id == 0) {
		printf("Reset stats in test()!!\n");
		ts_reset_stats();
        }
#endif
	barrier_cross(d->barrier);

#if 1	// time based run
	while (stop == 0) {
#else	// op_cnt based run
	_op_cnt = 0;
	while (_op_cnt < d->op_cnt) {
#endif
		op = rand_range(1000, d->seed);
		if (op < d->update) {
			if (d->alternate) {
				/* Alternate insertions and removals */
				if (last < 0) {
					/* Add random value */
					if (d->zipf)
						key = zipf_next(&zs) + 1;
					else
						key = rand_range(d->range, d->seed) + 1;
					
					if (hash_list_add(d, key)) {
						d->diff++;
						last = key;
						d->cmt_add++;
					}
					d->nb_add++;
				} else {
					/* Remove last value */
					if (hash_list_remove(d, last)) {
						d->diff--;
						d->cmt_remove++;
					}
					d->nb_remove++;
					last = -1;
				}
			} else {
				/* Randomly perform insertions and removals */
				if (d->zipf)
					key = zipf_next(&zs) + 1;
				else
					key = rand_range(d->range, d->seed) + 1;

				if ((op & 0x01) == 0) {
					/* Add random value */
					if (hash_list_add(d, key)) {
						d->diff++;
						d->cmt_add++;
					}
					d->nb_add++;
				} else {
					/* Remove random value */
					if (hash_list_remove(d, key)) {
						d->diff--;
						d->cmt_remove++;
					}
					d->nb_remove++;
				}
			}
		} else {
			/* Look for random value */
			if (d->zipf)
				key = zipf_next(&zs) + 1;
			else
				key = rand_range(d->range, d->seed) + 1;

			rc = hash_list_contains(d, key);
			if (rc) {
				d->nb_found++;
			}
			d->nb_contains++;
		}

		_op_cnt ++;
	}

	thread_finish(d);

	return NULL;
}

int getCPUid(int index, bool reset)
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
int main(int argc, char **argv)
{
	struct option long_options[] = {
	// These options don't set a flag
			{"help",                      no_argument,       NULL, 'h'},
			{"do-not-alternate",          no_argument,       NULL, 'a'},
			{"buckets",                   required_argument, NULL, 'b'},
			{"duration",                  required_argument, NULL, 'd'},
			{"initial-size",              required_argument, NULL, 'i'},
			{"num-threads",               required_argument, NULL, 'n'},
			{"range",                     required_argument, NULL, 'r'},
			{"seed",                      required_argument, NULL, 's'},
			{"zipf-dist-val",             required_argument, NULL, 'z'},
			{"op-cnt",                    required_argument, NULL, 'o'},
			{"nvheap-path",               required_argument, NULL, 'j'},
			{"nvheap-size",               required_argument, NULL, 'k'},
			{"nvlog-path",                required_argument, NULL, 'l'},
			{"nvlog-size",                required_argument, NULL, 'm'},
			{"rlu-max-ws",                required_argument, NULL, 'w'},
			{"update-rate",               required_argument, NULL, 'u'},
			{NULL, 0, NULL, 0}
	};

	hash_list_t *p_hash_list;
	int i, c, size, size2;
	unsigned long reads, updates, cmt_updates;
	thread_data_t *data;
	pthread_t *threads;
	pthread_attr_t attr;
	barrier_t barrier;
	struct timeval start, end;
	struct timespec timeout;
	int n_buckets = DEFAULT_BUCKETS;
	int duration = DEFAULT_DURATION;
	int initial = DEFAULT_INITIAL;
	int nb_threads = DEFAULT_NB_THREADS;
	int range = DEFAULT_RANGE;
	int seed = DEFAULT_SEED;
	int update = DEFAULT_UPDATE;
	int op_cnt = DEFAULT_OP_CNT;
	double zipf_dist_val = DEFAULT_ZIPF_DIST_VAL;
	int zipf = 0;
	int alternate = 1;
	sigset_t block_set;

#ifdef IS_MVRLU
	ts_conf_t *conf;

	if ((conf = (ts_conf_t *)malloc(sizeof(ts_conf_t))) == NULL) {
		perror("malloc");
		exit(1);
	}

	memcpy(conf->nvheap_path, DEFAULT_NVHEAP_POOL_PATH, strlen(DEFAULT_NVHEAP_POOL_PATH)); 
	conf->nvheap_size = DEFAULT_NVHEAP_POOL_SIZE;
#endif

	while(1) {
		i = 0;
		c = getopt_long(argc, argv, "hab:d:i:n:r:s:w:u:o:z:j:k:l:m:", long_options, &i);

		if(c == -1)
			break;

		if(c == 0 && long_options[i].flag == 0)
			c = long_options[i].val;

		switch(c) {
			case 0:
	/* Flag is automatically set */
			break;
			case 'h':
			printf("intset "
				"(linked list)\n"
				"\n"
				"Usage:\n"
				"  intset [options...]\n"
				"\n"
				"Options:\n"
				"  -h, --help\n"
				"        Print this message\n"
				"  -a, --do-not-alternate\n"
				"        Do not alternate insertions and removals\n"
				"  -b, --buckets <int>\n"
				"        Number of buckets (default=" XSTR(DEFAULT_BUCKETS) ")\n"
				"  -d, --duration <int>\n"
				"        Test duration in milliseconds (0=infinite, default=" XSTR(DEFAULT_DURATION) ")\n"
				"  -i, --initial-size <int>\n"
				"        Number of elements to insert before test (default=" XSTR(DEFAULT_INITIAL) ")\n"
				"  -n, --num-threads <int>\n"
				"        Number of threads (default=" XSTR(DEFAULT_NB_THREADS) ")\n"
				"  -r, --range <int>\n"
				"        Range of integer values inserted in set (default=" XSTR(DEFAULT_RANGE) ")\n"
				"  -s, --seed <int>\n"
				"        RNG seed (0=time-based, default=" XSTR(DEFAULT_SEED) ")\n"
				"  -u, --update-rate <int>\n"
				"        Percentage of update transactions (1000 = 100 percent) (default=" XSTR(DEFAULT_UPDATE) ")\n"
				"  -z, --zipf-dist-value <double>\n"
				"        Zipf distribution value (greater than or equal 0.1 (if no specified, uniform random dist.)) (default=" XSTR(DEFAULT_ZIPF_DIST_VAL) ")\n"
				"  -o, --op-cnt <int>\n"
				"        operation count for benchmark (greater than 0, default=" XSTR(DEFAULT_OP_CNT) ")\n"
				"  -j, --nvheap-path <string>\n"
				"        File name of nvheap path (default=" XSTR(DEFAULT_NVHEAP_POOL_PATH) ")\n"
				"  -k, --nvheap-size in GB <unsigned long>\n"
				"        File name of nvheap path (default=" XSTR(DEFAULT_NVHEAP_POOL_SIZE) ")\n"
				"  -l, --nvlog-path <string>\n"
				"        File name of nvheap path (default=" XSTR(DEFAULT_NVLOG_POOL_PATH) ")\n"
				"  -m, --nvlog-size in GB <unsigned long>\n"
				"        File name of nvheap path (default=" XSTR(DEFAULT_NVLOG_POOL_SIZE) ")\n"
				);
			exit(0);
			case 'a':
			alternate = 0;
			break;
			case 'b':
			n_buckets = atoi(optarg);
			break;
			case 'd':
			duration = atoi(optarg);
			break;
			case 'i':
			initial = atoi(optarg);
			break;
			case 'n':
			nb_threads = atoi(optarg);
			break;
			case 'r':
			range = atoi(optarg);
			break;
			case 's':
			seed = atoi(optarg);
			break;
			case 'u':
			update = atoi(optarg);
			break;
			case 'o':
			op_cnt = atoi(optarg);
			break;
			case 'z':
			zipf_dist_val = atof(optarg);
#ifdef IS_MVRLU
			case 'j':
			memset(conf->nvheap_path, 0, 100);
			memcpy(conf->nvheap_path, optarg, strlen(optarg));
			break;
			case 'k':
			conf->nvheap_size = atoi(optarg);
			conf->nvheap_size = conf->nvheap_size * 1024ul * 1024ul * 1024ul;
			break;
			case 'l':
			case 'm':
				/* ignore */
			break;
#endif
			case '?':
			printf("Use -h or --help for help\n");
			exit(0);
			default:
			exit(1);
		}
	}

	assert(n_buckets >= 1);
	assert(duration >= 0);
	assert(initial >= 0);
	assert(nb_threads > 0);
	assert(range > 0 && range >= initial);
	assert(update >= 0 && update <= 1000);
	assert(op_cnt >= 0);
	assert(zipf_dist_val >= 0.0);

	/* If zipf dist. value is 0, uniform random dist. is choosen */
	if (zipf_dist_val > 0)
		zipf = 1;

	printf("Set type     	   : hash-list\n");
	printf("Buckets      	   : %d\n", n_buckets);
	printf("Duration           : %d\n", duration);
	printf("Initial size 	   : %d\n", initial);
	printf("Nb threads   	   : %d\n", nb_threads);
	printf("Value range  	   : %d\n", range);
	printf("Seed         	   : %d\n", seed);
	printf("Update rate  	   : %d\n", update);
	printf("Operation count	   : %d\n", op_cnt);
	printf("Zipf dist    	   : %d\n", zipf);
	printf("Zipf dist val	   : %lf\n", zipf_dist_val);
#if IS_MVRLU
	printf("NVheap_pool_path   : %s\n", conf->nvheap_path);
	printf("NVheap_pool_size   : %lu\n", conf->nvheap_size);
#endif
	printf("Alternate          : %d\n", alternate);
	printf("Node size    	   : %lu\n", sizeof(node_t));
	printf("Type sizes   	   : int=%d/long=%d/ptr=%d/word=%d\n",
		(int)sizeof(int),
		(int)sizeof(long),
		(int)sizeof(void *),
		(int)sizeof(size_t));

	timeout.tv_sec = duration / 1000;
	timeout.tv_nsec = (duration % 1000) * 1000000;

	if ((data = (thread_data_t *)malloc(nb_threads * sizeof(thread_data_t))) == NULL) {
		perror("malloc");
		exit(1);
	}

	memset(data, 0, nb_threads * sizeof(thread_data_t));

	if ((threads = (pthread_t *)malloc(nb_threads * sizeof(pthread_t))) == NULL) {
		perror("malloc");
		exit(1);
	}

	if (seed == 0)
		srand((int)time(NULL));
	else
		srand(seed);

#ifdef IS_MVRLU
	global_init(nb_threads, conf);
#else
	global_init(nb_threads);
#endif
	hash_list_init(&p_hash_list, n_buckets);

	size = initial;
	
	stop = 0;

	/* Thread-local seed for main thread */
	rand_init(main_seed);

	if (alternate == 0 && range != initial * 2) {
		printf("ERROR: range is not twice the initial set size\n");
		exit(1);
	}

	/* Access set from all threads */
	barrier_init(&barrier, nb_threads + 1);
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

#ifndef THREAD_PINNING
        //Highly inefficient but works
        int j = 0;
        CPU_ZERO(&cpu_set[0]);
        for(j = 0; j < nb_threads; j++){
                int cpuid = getCPUid(j,0);
                CPU_SET(cpuid, &cpu_set[0]);
        }
        getCPUid(0, 1);
#endif

	for (i = 0; i < nb_threads; i++) {
		printf("Creating thread %d\n", i);
                //Creating CPU set for each thread and storing
                //Will be applied in test function
#ifdef THREAD_PINNING
                int cpuid = getCPUid(i,0);
                CPU_ZERO(&cpu_set[i]);
                CPU_SET(cpuid, &cpu_set[i]);
#endif

		data[i].uniq_id = i;
		data[i].range = range;
		data[i].update = update;
		data[i].op_cnt = op_cnt;
		data[i].zipf = zipf;
		data[i].zipf_dist_val = zipf_dist_val;
		data[i].alternate = alternate;
		data[i].cmt_add = 0;
		data[i].cmt_remove = 0;
		data[i].nb_add = 0;
		data[i].nb_remove = 0;
		data[i].nb_contains = 0;
		data[i].nb_found = 0;
		data[i].initial = initial;
		data[i].diff = 0;
		rand_init(data[i].seed);
		data[i].p_hash_list = p_hash_list;
		data[i].barrier = &barrier;
#ifdef IS_MVRLU
		data[i].p_rlu_td = RLU_THREAD_ALLOC();
#else
                data[i].p_rlu_td = &(data[i].rlu_td);
#endif
		data[i].p_hp_td = &(data[i].hp_td);
#ifdef IS_VERSION
                data[i].p_version_td = &(data[i].version_td);
                (data[i].p_version_td)->qsbr_data = &((data[i].p_version_td)->qsbr_data_inst);
#endif
		if (pthread_create(&threads[i], &attr, test, (void *)(&data[i])) != 0) {
			fprintf(stderr, "Error creating thread\n");
			exit(1);
		}
	}
	pthread_attr_destroy(&attr);

	/* Start threads */
	barrier_cross(&barrier);
	
	printf("STARTING THREADS...\n");
	gettimeofday(&start, NULL);
	if (duration > 0) {
		nanosleep(&timeout, NULL);
	} else {
		sigemptyset(&block_set);
		sigsuspend(&block_set);
	}
	stop = 1;
	gettimeofday(&end, NULL);
	printf("STOPPING THREADS...\n");

	/* Wait for thread completion */
	for (i = 0; i < nb_threads; i++) {
		printf("[wait for thread comp i:%d \n", i);
		if (pthread_join(threads[i], NULL) != 0) {
			fprintf(stderr, "Error waiting for thread completion\n");
			exit(1);
		}
	}

	/* Cleanup */
#ifdef IS_MVRLU
	for (i = 0; i < nb_threads; i++) {
		RLU_THREAD_FREE(data[i].p_rlu_td);
	}
#endif
	global_finish();

	duration = (end.tv_sec * 1000 + end.tv_usec / 1000) - (start.tv_sec * 1000 + start.tv_usec / 1000);
	reads = 0;
	updates = 0;
	cmt_updates = 0;
	for (i = 0; i < nb_threads; i++) {
		printf("Thread %d\n", i);
		printf("  #add        : %lu\n", data[i].nb_add);
		printf("  #remove     : %lu\n", data[i].nb_remove);
		printf("  #cmt_add    : %lu\n", data[i].cmt_add);
		printf("  #cmt_remove : %lu\n", data[i].cmt_remove);
		printf("  #contains   : %lu\n", data[i].nb_contains);
		printf("  #found      : %lu\n", data[i].nb_found);
		reads += data[i].nb_contains;
		updates += (data[i].nb_add + data[i].nb_remove);
		cmt_updates += (data[i].cmt_add + data[i].cmt_remove);
		size += data[i].diff;
	}
	size2 = hash_list_size(p_hash_list);
	printf("Set size      : %d (expected: %d)\n", size2, size);
#ifdef IS_RLU
	printf("Set size(RLU) : %d (expected: %d)\n", rlu_hash_list_size(p_hash_list), size);
#endif
	printf("Duration      : %d (ms)\n", duration);
	printf("#ops          : %lu (%f / s)\n", reads + updates, (reads + updates) * 1000.0 / duration);
	printf("#read ops     : %lu (%f / s)\n", reads, reads * 1000.0 / duration);
	printf("#update ops   : %lu (%f / s)\n", updates, updates * 1000.0 / duration);
	printf("#cmt_update ops : %lu (%f / s)\n", cmt_updates, cmt_updates * 1000.0 / duration);

#ifdef IS_MVRLU
	free(conf);
#endif
	free(threads);
	free(data);

	/* Minimal sanity check */
	print_stats();
	fflush(stdout);
#ifdef SIZE_CHECK
	if (size != size2) {
		printf("\n<<<<<<<<<<<<<< ASSERT FAILURE <<<<<<<<<<<<<<<<<<<\n");
		hash_list_print(p_hash_list);
		printf("\n>>>>>>>>>>>>>> ASSERT FAILURE >>>>>>>>>>>>>>>>>>>\n");
		fflush(stdout);
	}
#endif

	destroy_nvm();

#ifdef SIZE_CHECK
	/* Minimal sanity check */
	if (size != size2) {
		printf("ERROR: something wrong: list size does not match (%d vs %d)\n", size, size2);
		exit(1);
	}
#endif
	return 0;
}
