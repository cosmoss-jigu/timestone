/******************************************************************************/
#ifdef JEMALLOC_H_TYPES

typedef struct tcache_bin_info_s tcache_bin_info_t;
typedef struct tcache_bin_s tcache_bin_t;
typedef struct tcache_s tcache_t;
typedef struct tcaches_s tcaches_t;

/*
 * tcache pointers close to NULL are used to encode state information that is
 * used for two purposes: preventing thread caching on a per thread basis and
 * cleaning up during thread shutdown.
 */
#define	TCACHE_STATE_DISABLED		((tcache_t *)(uintptr_t)1)
#define	TCACHE_STATE_REINCARNATED	((tcache_t *)(uintptr_t)2)
#define	TCACHE_STATE_PURGATORY		((tcache_t *)(uintptr_t)3)
#define	TCACHE_STATE_MAX		TCACHE_STATE_PURGATORY

/*
 * Absolute minimum number of cache slots for each small bin.
 */
#define	TCACHE_NSLOTS_SMALL_MIN		20

/*
 * Absolute maximum number of cache slots for each small bin in the thread
 * cache.  This is an additional constraint beyond that imposed as: twice the
 * number of regions per slab for this size class.
 *
 * This constant must be an even number.
 */
#define	TCACHE_NSLOTS_SMALL_MAX		200

/* Number of cache slots for large size classes. */
#define	TCACHE_NSLOTS_LARGE		20

/* (1U << opt_lg_tcache_max) is used to compute tcache_maxclass. */
#define	LG_TCACHE_MAXCLASS_DEFAULT	15

/*
 * TCACHE_GC_SWEEP is the approximate number of allocation events between
 * full GC sweeps.  Integer rounding may cause the actual number to be
 * slightly higher, since GC is performed incrementally.
 */
#define	TCACHE_GC_SWEEP			8192

/* Number of tcache allocation/deallocation events between incremental GCs. */
#define	TCACHE_GC_INCR							\
    ((TCACHE_GC_SWEEP / NBINS) + ((TCACHE_GC_SWEEP / NBINS == 0) ? 0 : 1))

#endif /* JEMALLOC_H_TYPES */
/******************************************************************************/
#ifdef JEMALLOC_H_STRUCTS

typedef enum {
	tcache_enabled_false   = 0, /* Enable cast to/from bool. */
	tcache_enabled_true    = 1,
	tcache_enabled_default = 2
} tcache_enabled_t;

/*
 * Read-only information associated with each element of tcache_t's tbins array
 * is stored separately, mainly to reduce memory usage.
 */
struct tcache_bin_info_s {
	unsigned	ncached_max;	/* Upper limit on ncached. */
};

struct tcache_bin_s {
	tcache_bin_stats_t tstats;
	int		low_water;	/* Min # cached since last GC. */
	unsigned	lg_fill_div;	/* Fill (ncached_max >> lg_fill_div). */
	unsigned	ncached;	/* # of cached objects. */
	/*
	 * To make use of adjacent cacheline prefetch, the items in the avail
	 * stack goes to higher address for newer allocations.  avail points
	 * just above the available space, which means that
	 * avail[-ncached, ... -1] are available items and the lowest item will
	 * be allocated first.
	 */
	void		**avail;	/* Stack of available objects. */
};

struct tcache_s {
	ql_elm(tcache_t) link;		/* Used for aggregating stats. */
	uint64_t	prof_accumbytes;/* Cleared after arena_prof_accum(). */
	ticker_t	gc_ticker;	/* Drives incremental GC. */
	szind_t		next_gc_bin;	/* Next bin to GC. */
	tcache_bin_t	tbins[1];	/* Dynamically sized. */
	/*
	 * The pointer stacks associated with tbins follow as a contiguous
	 * array.  During tcache initialization, the avail pointer in each
	 * element of tbins is initialized to point to the proper offset within
	 * this array.
	 */
};

/* Linkage for list of available (previously used) explicit tcache IDs. */
struct tcaches_s {
	union {
		tcache_t	*tcache;
		tcaches_t	*next;
	};
};

#endif /* JEMALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef JEMALLOC_H_EXTERNS

extern bool	opt_tcache;
extern ssize_t	opt_lg_tcache_max;

extern tcache_bin_info_t	*tcache_bin_info;

/*
 * Number of tcache bins.  There are NBINS small-object bins, plus 0 or more
 * large-object bins.
 */
extern unsigned	nhbins;

/* Maximum cached size class. */
extern size_t	tcache_maxclass;

/*
 * Explicit tcaches, managed via the tcache.{create,flush,destroy} mallctls and
 * usable via the MALLOCX_TCACHE() flag.  The automatic per thread tcaches are
 * completely disjoint from this data structure.  tcaches starts off as a sparse
 * array, so it has no physical memory footprint until individual pages are
 * touched.  This allows the entire array to be allocated the first time an
 * explicit tcache is created without a disproportionate impact on memory usage.
 */
extern tcaches_t	*tcaches;

size_t	tcache_salloc(tsdn_t *tsdn, const void *ptr);
void	tcache_event_hard(tsd_t *tsd, tcache_t *tcache);
void	*tcache_alloc_small_hard(tsdn_t *tsdn, arena_t *arena, tcache_t *tcache,
    tcache_bin_t *tbin, szind_t binind, bool *tcache_success);

void	*tcache_next_small_hard(tsdn_t *tsdn, arena_t *arena, tcache_t *tcache,
    tcache_bin_t *tbin, szind_t binind, bool *tcache_success);
void	tcache_bin_flush_small(tsd_t *tsd, tcache_t *tcache, tcache_bin_t *tbin,
    szind_t binind, unsigned rem);
void	tcache_bin_flush_large(tsd_t *tsd, tcache_bin_t *tbin, szind_t binind,
    unsigned rem, tcache_t *tcache);
void	tcache_arena_reassociate(tsdn_t *tsdn, tcache_t *tcache,
    arena_t *oldarena, arena_t *newarena);
tcache_t *tcache_get_hard(tsd_t *tsd);
tcache_t *tcache_create(tsdn_t *tsdn, arena_t *arena);
void	tcache_cleanup(tsd_t *tsd);
void	tcache_stats_merge(tsdn_t *tsdn, tcache_t *tcache, arena_t *arena);
bool	tcaches_create(tsd_t *tsd, unsigned *r_ind);
void	tcaches_flush(tsd_t *tsd, unsigned ind);
void	tcaches_destroy(tsd_t *tsd, unsigned ind);
bool	tcache_boot(tsdn_t *tsdn);

#endif /* JEMALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef JEMALLOC_H_INLINES

#ifndef JEMALLOC_ENABLE_INLINE
void	tcache_event(tsd_t *tsd, tcache_t *tcache);
void	tcache_flush(void);
bool	tcache_enabled_get(void);
tcache_t *tcache_get(tsd_t *tsd, bool create);
void	tcache_enabled_set(bool enabled);
void	*tcache_alloc_easy(tcache_bin_t *tbin, bool *tcache_success);
void	*tcache_next_easy(tcache_bin_t *tbin, bool *tcache_success);
void	*tcache_alloc_small(tsd_t *tsd, arena_t *arena, tcache_t *tcache,
    size_t size, szind_t ind, bool zero, bool slow_path);

void	*tcache_next_small(tsd_t *tsd, arena_t *arena, tcache_t *tcache,
    size_t size, szind_t ind, bool zero, bool slow_path);

void	*tcache_alloc_large(tsd_t *tsd, arena_t *arena, tcache_t *tcache,
    size_t size, szind_t ind, bool zero, bool slow_path);
void	tcache_dalloc_small(tsd_t *tsd, tcache_t *tcache, void *ptr,
    szind_t binind, bool slow_path);
void	tcache_dalloc_large(tsd_t *tsd, tcache_t *tcache, void *ptr,
    size_t size, bool slow_path);
tcache_t	*tcaches_get(tsd_t *tsd, unsigned ind);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_TCACHE_C_))

/* #ifndef USE_NVM_DEVICE */
/* #define SIMULATE_LATENCIES 1 */
/* #endif */

#define WAIT_WRITES_DELAY 370
#define WRITE_DATA_WAIT_DELAY 370
#define WRITE_DATA_NOWAIT_DELAY 9


#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

#define CACHE_ALIGNED __attribute__ ((aligned(CACHE_LINE_SIZE)))


#ifndef EPOCH_CACHE_LINE_SIZE
#define EPOCH_CACHE_LINE_SIZE 64
#endif

#define EPOCH_CACHE_ALIGNED __attribute__ ((aligned(EPOCH_CACHE_LINE_SIZE)))

#if !defined(UNUSED)
#define UNUSED __attribute__ ((unused))
#endif

#ifdef __SSE__
#include <xmmintrin.h>
#endif

#include <stdint.h>

typedef uint64_t ticks;


static inline ticks
nv_getticks(void)
{
  unsigned hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

/* contains functions for working with non-volatile memory */


// TODO add operations to do moves from volatile memory to persistent memeory addresses using non-temporal stores
// for examples, see https://github.com/pmem/nvml/blob/master/src/libpmem/pmem.c



static inline size_t num_cache_lines(size_t size_bytes) {
	if (size_bytes == 0) return 0;
	return (((size_bytes - 1)/ CACHE_LINE_SIZE) + 1);
}

#ifdef NEW_INTEL_INSTRUCTIONS

#define _mm_clflushopt(addr) asm volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)addr));
#define _mm_clwb(addr) asm volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)addr));

static inline void wait_writes() {
	_mm_sfence();
}

static inline void write_data_nowait(void* addr, size_t sz) {
	uintptr_t p;

	for (p = (uintptr_t)adddr & ~(CACHE_LINE_SIZE - 1; p < (uintptr_t)addr + sz; p += CACHE_LINE_SIZE) {
		_mm_clwb((void*)p);
	}
}

static inline void write_data_wait(void* addr, size_t sz) {
	uintptr_t p;

		for (p = (uintptr_t)adddr & ~(CACHE_LINE_SIZE - 1; p < (uintptr_t)addr + sz; p += CACHE_LINE_SIZE) {
			_mm_clwb((void*)p);
		}
	_mm_sfence();
}

#else

#define _mm_clflushopt(addr) _mm_clflush(addr)
#define _mm_clwb(addr) _mm_clflush(addr)


static inline void wait_writes() {
#ifdef SIMULATE_LATENCIES
	uint64_t startCycles = nv_getticks();
	uint64_t endCycles = startCycles + WAIT_WRITES_DELAY;
	uint64_t cycles = startCycles;

	while (cycles < endCycles) {
		cycles = nv_getticks();
	}
	_mm_sfence();

//	printf ("[wait_writes - tcache.h] simulate latency!!!!!!!!!!!!!\n");
#else
	//_mm_sfence();
#endif
}

//size is in terms of number of cache lines
static inline void write_data_wait(void* addr, size_t sz) {
#ifdef SIMULATE_LATENCIES
	uint64_t startCycles =nv_getticks();
	uint64_t endCycles = startCycles + WRITE_DATA_WAIT_DELAY;
	uint64_t cycles = startCycles;

    //fprintf(stderr, "here\n");
	while (cycles < endCycles) {
		cycles = nv_getticks();
        _mm_pause();
	}
	_mm_sfence();
//	printf ("[wait_writes - tcache.h] simulate latency!!!!!!!!!!!!!\n");
#else
	uintptr_t p;

	for (p = (uintptr_t)addr & ~(CACHE_LINE_SIZE - 1); p < (uintptr_t)addr + sz; p += CACHE_LINE_SIZE) {
		_mm_clflush((void*)p);
	}
#endif
}

static inline void write_data_nowait(void* addr, size_t sz) {
#ifdef SIMULATE_LATENCIES
	//this might just take a few cycles if we are not waiting for the op to complete
	//just reading rdtsc may take a few tens of cycles
	//so perhaps better to just do a pause
	if (WRITE_DATA_NOWAIT_DELAY < 10) {
		_mm_pause();
	}
	else {
		uint64_t startCycles = nv_getticks();
		uint64_t endCycles = startCycles + WRITE_DATA_NOWAIT_DELAY;
		uint64_t cycles = startCycles;

		while (cycles < endCycles) {
			cycles = nv_getticks();
		}
	}
//	printf ("[wait_writes - tcache.h] simulate latency!!!!!!!!!!!!!\n");
#else
	write_data_wait(addr, sz);
#endif
}
#endif


JEMALLOC_INLINE void
tcache_flush(void)
{
	tsd_t *tsd;

	cassert(config_tcache);

	tsd = tsd_fetch();
	tcache_cleanup(tsd);
}

JEMALLOC_INLINE bool
tcache_enabled_get(void)
{
	tsd_t *tsd;
	tcache_enabled_t tcache_enabled;

	cassert(config_tcache);

	tsd = tsd_fetch();
	tcache_enabled = tsd_tcache_enabled_get(tsd);
	if (tcache_enabled == tcache_enabled_default) {
		tcache_enabled = (tcache_enabled_t)opt_tcache;
		tsd_tcache_enabled_set(tsd, tcache_enabled);
	}

	return ((bool)tcache_enabled);
}

JEMALLOC_INLINE void
tcache_enabled_set(bool enabled)
{
	tsd_t *tsd;
	tcache_enabled_t tcache_enabled;

	cassert(config_tcache);

	tsd = tsd_fetch();

	tcache_enabled = (tcache_enabled_t)enabled;
	tsd_tcache_enabled_set(tsd, tcache_enabled);

	if (!enabled)
		tcache_cleanup(tsd);
}

JEMALLOC_ALWAYS_INLINE tcache_t *
tcache_get(tsd_t *tsd, bool create)
{
	tcache_t *tcache;

	if (!config_tcache)
		return (NULL);

	tcache = tsd_tcache_get(tsd);
	if (!create)
		return (tcache);
	if (unlikely(tcache == NULL) && tsd_nominal(tsd)) {
		tcache = tcache_get_hard(tsd);
		tsd_tcache_set(tsd, tcache);
	}

	return (tcache);
}

JEMALLOC_ALWAYS_INLINE void
tcache_event(tsd_t *tsd, tcache_t *tcache)
{

	if (TCACHE_GC_INCR == 0)
		return;

	if (unlikely(ticker_tick(&tcache->gc_ticker)))
		tcache_event_hard(tsd, tcache);
}

JEMALLOC_ALWAYS_INLINE void *
tcache_next_easy(tcache_bin_t *tbin, bool *tcache_success)
{
	void *ret;

	if (unlikely(tbin->ncached == 0)) {
		tbin->low_water = -1;
		*tcache_success = false;
		return (NULL);
	}
	/*
	 * tcache_success (instead of ret) should be checked upon the return of
	 * this function.  We avoid checking (ret == NULL) because there is
	 * never a null stored on the avail stack (which is unknown to the
	 * compiler), and eagerly checking ret would cause pipeline stall
	 * (waiting for the cacheline).
	 */
	*tcache_success = true;
	ret = *(tbin->avail - tbin->ncached);
	//tbin->ncached--;

	if (unlikely((int)tbin->ncached < tbin->low_water))
		tbin->low_water = tbin->ncached;

	return (ret);
}

JEMALLOC_ALWAYS_INLINE void *
tcache_alloc_easy(tcache_bin_t *tbin, bool *tcache_success)
{
	void *ret;

	if (unlikely(tbin->ncached == 0)) {
		tbin->low_water = -1;
		*tcache_success = false;
		return (NULL);
	}
	/*
	 * tcache_success (instead of ret) should be checked upon the return of
	 * this function.  We avoid checking (ret == NULL) because there is
	 * never a null stored on the avail stack (which is unknown to the
	 * compiler), and eagerly checking ret would cause pipeline stall
	 * (waiting for the cacheline).
	 */
	*tcache_success = true;
	ret = *(tbin->avail - tbin->ncached);
	tbin->ncached--;

	write_data_nowait((void*)(tbin->avail - tbin->ncached), 1);
    write_data_nowait((void*)&(tbin->ncached), 1);

	if (unlikely((int)tbin->ncached < tbin->low_water))
		tbin->low_water = tbin->ncached;

	return (ret);
}

JEMALLOC_ALWAYS_INLINE void *
tcache_next_small(tsd_t *tsd, arena_t *arena, tcache_t *tcache, size_t size,
    szind_t binind, bool zero, bool slow_path)
{
	void *ret;
	tcache_bin_t *tbin;
	bool tcache_success;
	size_t usize JEMALLOC_CC_SILENCE_INIT(0);

	assert(binind < NBINS);
	tbin = &tcache->tbins[binind];
	ret = tcache_next_easy(tbin, &tcache_success);
	assert(tcache_success == (ret != NULL));
	if (unlikely(!tcache_success)) {
		bool tcache_hard_success;
		arena = arena_choose(tsd, arena);
		if (unlikely(arena == NULL))
			return (NULL);

		ret = tcache_next_small_hard(tsd_tsdn(tsd), arena, tcache,
		    tbin, binind, &tcache_hard_success);
		if (tcache_hard_success == false)
			return (NULL);
	}

	assert(ret);

	return (ret);
}

JEMALLOC_ALWAYS_INLINE void *
tcache_alloc_small(tsd_t *tsd, arena_t *arena, tcache_t *tcache, size_t size,
    szind_t binind, bool zero, bool slow_path)
{
	void *ret;
	tcache_bin_t *tbin;
	bool tcache_success;
	size_t usize JEMALLOC_CC_SILENCE_INIT(0);

	assert(binind < NBINS);
	tbin = &tcache->tbins[binind];
	ret = tcache_alloc_easy(tbin, &tcache_success);
	assert(tcache_success == (ret != NULL));
	if (unlikely(!tcache_success)) {
		bool tcache_hard_success;
		arena = arena_choose(tsd, arena);
		if (unlikely(arena == NULL))
			return (NULL);

		ret = tcache_alloc_small_hard(tsd_tsdn(tsd), arena, tcache,
		    tbin, binind, &tcache_hard_success);
		if (tcache_hard_success == false)
			return (NULL);
	}

	assert(ret);
	/*
	 * Only compute usize if required.  The checks in the following if
	 * statement are all static.
	 */
	if (config_prof || (slow_path && config_fill) || unlikely(zero)) {
		usize = index2size(binind);
		assert(tcache_salloc(tsd_tsdn(tsd), ret) == usize);
	}

	if (likely(!zero)) {
		if (slow_path && config_fill) {
			if (unlikely(opt_junk_alloc)) {
				arena_alloc_junk_small(ret,
				    &arena_bin_info[binind], false);
			} else if (unlikely(opt_zero))
				memset(ret, 0, usize);
		}
	} else {
		if (slow_path && config_fill && unlikely(opt_junk_alloc)) {
			arena_alloc_junk_small(ret, &arena_bin_info[binind],
			    true);
		}
		memset(ret, 0, usize);
	}

	if (config_stats)
		tbin->tstats.nrequests++;
	if (config_prof)
		tcache->prof_accumbytes += usize;
	tcache_event(tsd, tcache);
	return (ret);
}

JEMALLOC_ALWAYS_INLINE void *
tcache_alloc_large(tsd_t *tsd, arena_t *arena, tcache_t *tcache, size_t size,
    szind_t binind, bool zero, bool slow_path)
{
	void *ret;
	tcache_bin_t *tbin;
	bool tcache_success;

	assert(binind < nhbins);
	tbin = &tcache->tbins[binind];
	ret = tcache_alloc_easy(tbin, &tcache_success);
	assert(tcache_success == (ret != NULL));
	if (unlikely(!tcache_success)) {
		/*
		 * Only allocate one large object at a time, because it's quite
		 * expensive to create one and not use it.
		 */
		arena = arena_choose(tsd, arena);
		if (unlikely(arena == NULL))
			return (NULL);

		ret = large_malloc(tsd_tsdn(tsd), arena, s2u(size), zero);
		if (ret == NULL)
			return (NULL);
	} else {
		size_t usize JEMALLOC_CC_SILENCE_INIT(0);

		/* Only compute usize on demand */
		if (config_prof || (slow_path && config_fill) ||
		    unlikely(zero)) {
			usize = index2size(binind);
			assert(usize <= tcache_maxclass);
		}

		if (likely(!zero)) {
			if (slow_path && config_fill) {
				if (unlikely(opt_junk_alloc)) {
					memset(ret, JEMALLOC_ALLOC_JUNK,
					    usize);
				} else if (unlikely(opt_zero))
					memset(ret, 0, usize);
			}
		} else
			memset(ret, 0, usize);

		if (config_stats)
			tbin->tstats.nrequests++;
		if (config_prof)
			tcache->prof_accumbytes += usize;
	}

	tcache_event(tsd, tcache);
	return (ret);
}

JEMALLOC_ALWAYS_INLINE void
tcache_dalloc_small(tsd_t *tsd, tcache_t *tcache, void *ptr, szind_t binind,
    bool slow_path)
{
	tcache_bin_t *tbin;
	tcache_bin_info_t *tbin_info;

	assert(tcache_salloc(tsd_tsdn(tsd), ptr) <= SMALL_MAXCLASS);

	if (slow_path && config_fill && unlikely(opt_junk_free))
		arena_dalloc_junk_small(ptr, &arena_bin_info[binind]);

	tbin = &tcache->tbins[binind];
	tbin_info = &tcache_bin_info[binind];
	if (unlikely(tbin->ncached == tbin_info->ncached_max)) {
		tcache_bin_flush_small(tsd, tcache, tbin, binind,
		    (tbin_info->ncached_max >> 1));
	}
	assert(tbin->ncached < tbin_info->ncached_max);

	tbin->ncached++;
	*(tbin->avail - tbin->ncached) = ptr;
	write_data_nowait((void*)(tbin->avail - tbin->ncached), 1);
    write_data_nowait((void*)&(tbin->ncached), 1);

	tcache_event(tsd, tcache);
}

JEMALLOC_ALWAYS_INLINE void
tcache_dalloc_large(tsd_t *tsd, tcache_t *tcache, void *ptr, size_t size,
    bool slow_path)
{
	szind_t binind;
	tcache_bin_t *tbin;
	tcache_bin_info_t *tbin_info;

	assert(tcache_salloc(tsd_tsdn(tsd), ptr) > SMALL_MAXCLASS);
	assert(tcache_salloc(tsd_tsdn(tsd), ptr) <= tcache_maxclass);

	binind = size2index(size);

	if (slow_path && config_fill && unlikely(opt_junk_free))
		large_dalloc_junk(ptr, size);

	tbin = &tcache->tbins[binind];
	tbin_info = &tcache_bin_info[binind];
	if (unlikely(tbin->ncached == tbin_info->ncached_max)) {
		tcache_bin_flush_large(tsd, tbin, binind,
		    (tbin_info->ncached_max >> 1), tcache);
	}
	assert(tbin->ncached < tbin_info->ncached_max);
	tbin->ncached++;
	*(tbin->avail - tbin->ncached) = ptr;

	write_data_nowait((void*)(tbin->avail - tbin->ncached), 1);
    write_data_nowait((void*)&(tbin->ncached), 1);
	tcache_event(tsd, tcache);
}

JEMALLOC_ALWAYS_INLINE tcache_t *
tcaches_get(tsd_t *tsd, unsigned ind)
{
	tcaches_t *elm = &tcaches[ind];
	if (unlikely(elm->tcache == NULL)) {
		elm->tcache = tcache_create(tsd_tsdn(tsd), arena_choose(tsd,
		    NULL));
	}
	return (elm->tcache);
}
#endif

#endif /* JEMALLOC_H_INLINES */
/******************************************************************************/
