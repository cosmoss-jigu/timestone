#define	JEMALLOC_PAGES_C_
#include "jemalloc/internal/jemalloc_internal.h"

#ifdef JEMALLOC_SYSCTL_VM_OVERCOMMIT
#include <sys/sysctl.h>
#endif

#ifdef USE_NVM_DEVICE
#include <linux/limits.h>
#include <unistd.h>
#endif

/******************************************************************************/
/* Data. */

#ifndef _WIN32
#  define PAGES_PROT_COMMIT (PROT_READ | PROT_WRITE)
#  define PAGES_PROT_DECOMMIT (PROT_NONE)
static int	mmap_flags;
#endif
static bool	os_overcommits;

/******************************************************************************/

void *
pages_map(void *addr, size_t size, bool *commit)
{
#ifdef USE_NVM_DEVICE
	unsigned long mapped_len;
	int is_pmem;
	char path[PATH_MAX];
#endif
	void *ret;

	assert(size != 0);

	if (os_overcommits)
		*commit = true;

#ifdef _WIN32
	/*
	 * If VirtualAlloc can't allocate at the given address when one is
	 * given, it fails and returns NULL.
	 */
	ret = VirtualAlloc(addr, size, MEM_RESERVE | (*commit ? MEM_COMMIT : 0),
	    PAGE_READWRITE);
#else
	/*
	 * We don't use MAP_FIXED here, because it can cause the *replacement*
	 * of existing mappings, and we only want to create new mappings.
	 */
	{
		int prot = *commit ? PAGES_PROT_COMMIT : PAGES_PROT_DECOMMIT;
#ifdef USE_NVM_DEVICE
		const static char *_NVM_DIR_PATH = "/mnt/pmem";
		const static int num_dir = 97; /* prime number */
		static __thread unsigned long map_cnt;
		int fd, result;

		snprintf(path, PATH_MAX, "%s/ts-%lx/%p-0x%lx",
			 _NVM_DIR_PATH, map_cnt % num_dir,
			 &map_cnt, map_cnt);
		fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT,
			  S_IRUSR | S_IWUSR);
		if (unlikely(fd == -1)) {
			snprintf(path, PATH_MAX, "%s/ts-%lx",
				 _NVM_DIR_PATH, map_cnt % num_dir);
			mkdir(path, 0777); /* silently ignore errors. */

			/* then, retry ... */
			snprintf(path, PATH_MAX, "%s/ts-%lx/%p-0x%lx",
				 _NVM_DIR_PATH, map_cnt % num_dir,
				 &map_cnt, map_cnt);
			fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT,
				  S_IRUSR | S_IWUSR);
		}
		result = ftruncate(fd, size);
		if (unlikely(result)) {
			perror("Fail to truncate a file\n");
			exit(1); /* TODO: ugly :( */
		}
		ret = mmap(addr, size, prot, mmap_flags, fd, 0);
		assert(addr != MAP_FAILED);
		close(fd);
		unlink(path);
		map_cnt++;
#else
		ret = mmap(addr, size, prot, mmap_flags, -1, 0);
#endif
	}
	assert(ret != NULL);

	if (ret == MAP_FAILED)
		ret = NULL;
	else if (addr != NULL && ret != addr) {
		/*
		 * We succeeded in mapping memory, but not in the right place.
		 */
		pages_unmap(ret, size);
		ret = NULL;
	}

	if (ret == NULL) {
		printf("[pages_map] alloc failed [%p, %ld] errno = %d, path = %s\n",
		       addr, size, errno, path);
		perror("Fail to mmap a file\n");
	}

#endif
	assert(ret == NULL || (addr == NULL && ret != addr)
	    || (addr != NULL && ret == addr));
	return (ret);
}

void
pages_unmap(void *addr, size_t size)
{

#ifdef _WIN32
	if (VirtualFree(addr, 0, MEM_RELEASE) == 0)
#else

#if 0
#ifdef USE_NVM_DEVICE
	if (pmem_unmap(addr, size) == -1)
#endif
#endif

	if (munmap(addr, size) == -1)
#endif
	{
		char buf[BUFERROR_BUF];

		buferror(get_errno(), buf, sizeof(buf));
		malloc_printf("<jemalloc>: Error in "
#ifdef _WIN32
		              "VirtualFree"
#else
		              "munmap"
#endif
		              "(): %s\n", buf);
		if (opt_abort)
			abort();
	}
}

void *
pages_trim(void *addr, size_t alloc_size, size_t leadsize, size_t size,
    bool *commit)
{
	void *ret = (void *)((uintptr_t)addr + leadsize);

	assert(alloc_size >= leadsize + size);
#ifdef _WIN32
	{
		void *new_addr;

		pages_unmap(addr, alloc_size);
		new_addr = pages_map(ret, size, commit);
		if (new_addr == ret)
			return (ret);
		if (new_addr)
			pages_unmap(new_addr, size);
		return (NULL);
	}
#else
	{
		size_t trailsize = alloc_size - leadsize - size;

		if (leadsize != 0)
			pages_unmap(addr, leadsize);
		if (trailsize != 0)
			pages_unmap((void *)((uintptr_t)ret + size), trailsize);
		return (ret);
	}
#endif
}

static bool
pages_commit_impl(void *addr, size_t size, bool commit)
{

	if (os_overcommits)
		return (true);

#ifdef _WIN32
	return (commit ? (addr != VirtualAlloc(addr, size, MEM_COMMIT,
	    PAGE_READWRITE)) : (!VirtualFree(addr, size, MEM_DECOMMIT)));
#else
	{
		int prot = commit ? PAGES_PROT_COMMIT : PAGES_PROT_DECOMMIT;
		void *result = mmap(addr, size, prot, mmap_flags | MAP_FIXED,
		    -1, 0);
		printf ("[pages_commit_impl - calling mmap()] addr:%p result:%p size:%lu\n",
			addr, result, size);

		if (result == MAP_FAILED)
			return (true);
		if (result != addr) {
			/*
			 * We succeeded in mapping memory, but not in the right
			 * place.
			 */
			pages_unmap(result, size);
			return (true);
		}
		return (false);
	}
#endif
}

bool
pages_commit(void *addr, size_t size)
{

	return (pages_commit_impl(addr, size, true));
}

bool
pages_decommit(void *addr, size_t size)
{

	return (pages_commit_impl(addr, size, false));
}

bool
pages_purge(void *addr, size_t size)
{
	bool unzeroed;

#ifdef _WIN32
	VirtualAlloc(addr, size, MEM_RESET, PAGE_READWRITE);
	unzeroed = true;
#elif defined(JEMALLOC_HAVE_MADVISE)
#  ifdef JEMALLOC_PURGE_MADVISE_DONTNEED
#    define JEMALLOC_MADV_PURGE MADV_DONTNEED
#    define JEMALLOC_MADV_ZEROS true
#  elif defined(JEMALLOC_PURGE_MADVISE_FREE)
#    define JEMALLOC_MADV_PURGE MADV_FREE
#    define JEMALLOC_MADV_ZEROS false
#  else
#    error "No madvise(2) flag defined for purging unused dirty pages."
#  endif
	int err = madvise(addr, size, JEMALLOC_MADV_PURGE);
	unzeroed = (!JEMALLOC_MADV_ZEROS || err != 0);
#  undef JEMALLOC_MADV_PURGE
#  undef JEMALLOC_MADV_ZEROS
#else
	/* Last resort no-op. */
	unzeroed = true;
#endif
	return (unzeroed);
}

#ifdef JEMALLOC_SYSCTL_VM_OVERCOMMIT
static bool
os_overcommits_sysctl(void)
{
	int vm_overcommit;
	size_t sz;

	sz = sizeof(vm_overcommit);
	if (sysctlbyname("vm.overcommit", &vm_overcommit, &sz, NULL, 0) != 0)
		return (false); /* Error. */

	return ((vm_overcommit & 0x3) == 0);
}
#endif

#ifdef JEMALLOC_PROC_SYS_VM_OVERCOMMIT_MEMORY
/*
 * Use syscall(2) rather than {open,read,close}(2) when possible to avoid
 * reentry during bootstrapping if another library has interposed system call
 * wrappers.
 */
static bool
os_overcommits_proc(void)
{
	int fd;
	char buf[1];
	ssize_t nread;

#ifdef SYS_open
	fd = (int)syscall(SYS_open, "/proc/sys/vm/overcommit_memory", O_RDONLY);
#else
	fd = open("/proc/sys/vm/overcommit_memory", O_RDONLY);
#endif
	if (fd == -1)
		return (false); /* Error. */

#ifdef SYS_read
	nread = (ssize_t)syscall(SYS_read, fd, &buf, sizeof(buf));
#else
	nread = read(fd, &buf, sizeof(buf));
#endif

#ifdef SYS_close
	syscall(SYS_close, fd);
#else
	close(fd);
#endif

	if (nread < 1)
		return (false); /* Error. */
	/*
	 * /proc/sys/vm/overcommit_memory meanings:
	 * 0: Heuristic overcommit.
	 * 1: Always overcommit.
	 * 2: Never overcommit.
	 */
	return (buf[0] == '0' || buf[0] == '1');
}
#endif

void
pages_boot(void)
{

#ifndef _WIN32
#ifdef USE_NVM_DEVICE
	mmap_flags = MAP_SHARED;
#else
	mmap_flags = MAP_PRIVATE | MAP_ANON;
#endif
#endif

#ifdef JEMALLOC_SYSCTL_VM_OVERCOMMIT
	os_overcommits = os_overcommits_sysctl();
#elif defined(JEMALLOC_PROC_SYS_VM_OVERCOMMIT_MEMORY)
	os_overcommits = os_overcommits_proc();
#  ifdef MAP_NORESERVE
	if (os_overcommits)
		mmap_flags |= MAP_NORESERVE;
#  endif
#else
	os_overcommits = false;
#endif
}
