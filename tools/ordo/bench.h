#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

#include "config.h"

#if defined (__SVR4) && defined (__sun)
#include <sys/mman.h>
#include <sys/inttypes.h>
#include <unistd.h>
#else
#include <sys/user.h>
#include <inttypes.h>
#endif

#define __noret__ __attribute__((noreturn))
#define __align__ __attribute__((aligned(CACHE_BYTES)))

#define READ_ONCE(v) \
	({ \
	 union { typeof(x) __val; char __c[1]; } __u; \
	 __read_once_size(&(x), __u.__c); \
	 __u.__val; \
	 })

static inline void __read_once_size(const volatile void *p, void *res)
{
	*(uint64_t *)res = *(volatile uint64_t *)p;
}

void __noret__ die(const char* errstr, ...);
void __noret__ edie(const char* errstr, ...);

void setaffinity(int c);

uint64_t usec(void);

static inline uint64_t read_tsc(void)
{
	uint32_t a, d;
	__asm __volatile("rdtsc" : "=a" (a), "=d" (d));
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}

static inline uint64_t read_tscp(void)
{
	uint32_t a, d;
	__asm __volatile("rdtscp": "=a"(a), "=d"(d));
	return ((uint64_t) a) | (((uint64_t) d) << 32);
}

static inline uint64_t read_serialized_tsc(void)
{
	uint64_t tsc;
	__asm __volatile("cpuid; rdtsc; shl $32, %%rdx; or %%rdx, %%rax"
			 : "=a" (tsc)
			 : : "%rbx", "%rcx", "%rdx");
	return tsc;
}

static inline void smp_wmb(void)
{
    __asm__ __volatile__("sfence":::"memory");
}

static inline void smp_rmb(void)
{
    __asm__ __volatile__("lfence":::"memory");
}

static inline void smp_mb(void)
{
    __asm__ __volatile__("mfence":::"memory");
}


static inline unsigned int get_page_size(void)
{
#if defined (__SVR4) && defined (__sun)
	return sysconf(_SC_PAGE_SIZE);
#else
	return PAGE_SIZE;
#endif
}

static inline void * xmalloc(unsigned int sz)
{
	size_t s;
	void *r;
	
	s = ((sz - 1) + CACHE_BYTES) & ~(CACHE_BYTES - 1);
#if defined (__SVR4) && defined (__sun)
	r = memalign(s, CACHE_BYTES);
#else
	if (posix_memalign(&r, CACHE_BYTES, s))
		edie("posix_memalign");
#endif
	memset(r, 0, sz);
	return r;
}

static inline uint32_t rnd(uint32_t *seed)
{
	*seed = *seed * 1103515245 + 12345;
	return *seed & 0x7fffffff;
}

static inline void nop_pause(void)
{
	__asm __volatile("pause");
}

static inline void rep_nop(void)
{
	__asm __volatile("rep; nop" ::: "memory");
}

static inline void barrier(void)
{
	__asm __volatile("":::"memory");
}

static inline void cpu_relax(void)
{
	rep_nop();
}

//Swap uint64_t
static inline uint64_t swap_uint64(volatile uint64_t* target,  uint64_t x) {
    __asm__ __volatile__("xchgq %0,%1"
            :"=r" ((uint64_t) x)
            :"m" (*(volatile uint64_t *)target), "0" ((uint64_t) x)
            :"memory");

    return x;
}

//Swap uint32_t
static inline uint32_t swap_uint32(volatile uint32_t* target,  uint32_t x) {
    __asm__ __volatile__("xchgl %0,%1"
            :"=r" ((uint32_t) x)
            :"m" (*(volatile uint32_t *)target), "0" ((uint32_t) x)
            :"memory");

    return x;
}

//Swap uint16_t
static inline uint16_t swap_uint16(volatile uint16_t* target,  uint16_t x) {
    __asm__ __volatile__("xchgw %0,%1"
            :"=r" ((uint16_t) x)
            :"m" (*(volatile uint16_t *)target), "0" ((uint16_t) x)
            :"memory");

    return x;
}

//Swap uint8_t
static inline uint8_t swap_uint8(volatile uint8_t* target,  uint8_t x) {
    __asm__ __volatile__("xchgb %0,%1"
            :"=r" ((uint8_t) x)
            :"m" (*(volatile uint8_t *)target), "0" ((uint8_t) x)
            :"memory");

    return x;
}
