/*
 * Name mangling for public symbols is controlled by --with-mangling and
 * --with-jemalloc-prefix.  With default settings the je_ prefix is stripped by
 * these macro definitions.
 */
#ifndef JEMALLOC_NO_RENAME
#  define je_malloc_conf nv_malloc_conf
#  define je_malloc_message nv_malloc_message
#  define je_malloc nv_malloc
#  define je_calloc nv_calloc
#  define je_posix_memalign nv_posix_memalign
#  define je_aligned_alloc nv_aligned_alloc
#  define je_realloc nv_realloc
#  define je_free nv_free
#  define je_mallocx nv_mallocx
#  define je_nextx nv_nextx
#  define je_rallocx nv_rallocx
#  define je_xallocx nv_xallocx
#  define je_sallocx nv_sallocx
#  define je_dallocx nv_dallocx
#  define je_sdallocx nv_sdallocx
#  define je_nallocx nv_nallocx
#  define je_mallctl nv_mallctl
#  define je_mallctlnametomib nv_mallctlnametomib
#  define je_mallctlbymib nv_mallctlbymib
#  define je_malloc_stats_print nv_malloc_stats_print
#  define je_malloc_usable_size nv_malloc_usable_size
#  define je_memalign nv_memalign
#  define je_valloc nv_valloc
#endif
