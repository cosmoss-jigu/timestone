/******************************************************************************/
#ifdef JEMALLOC_H_TYPES

typedef struct witness_s witness_t;
typedef unsigned witness_rank_t;
typedef ql_head(witness_t) witness_list_t;
typedef int witness_comp_t (const witness_t *, void *, const witness_t *,
    void *);

/*
 * Lock ranks.  Witnesses with rank WITNESS_RANK_OMIT are completely ignored by
 * the witness machinery.
 */
#define	WITNESS_RANK_OMIT		0U

#define	WITNESS_RANK_INIT		1U
#define	WITNESS_RANK_CTL		1U
#define	WITNESS_RANK_ARENAS		2U

#define	WITNESS_RANK_PROF_DUMP		3U
#define	WITNESS_RANK_PROF_BT2GCTX	4U
#define	WITNESS_RANK_PROF_TDATAS	5U
#define	WITNESS_RANK_PROF_TDATA		6U
#define	WITNESS_RANK_PROF_GCTX		7U

#define	WITNESS_RANK_ARENA		8U
#define	WITNESS_RANK_ARENA_EXTENTS	9U
#define	WITNESS_RANK_ARENA_EXTENT_CACHE	10

#define	WITNESS_RANK_RTREE_ELM		11U
#define	WITNESS_RANK_BASE		12U

#define	WITNESS_RANK_LEAF		0xffffffffU
#define	WITNESS_RANK_ARENA_BIN		WITNESS_RANK_LEAF
#define	WITNESS_RANK_ARENA_LARGE	WITNESS_RANK_LEAF
#define	WITNESS_RANK_DSS		WITNESS_RANK_LEAF
#define	WITNESS_RANK_PROF_ACTIVE	WITNESS_RANK_LEAF
#define	WITNESS_RANK_PROF_DUMP_SEQ	WITNESS_RANK_LEAF
#define	WITNESS_RANK_PROF_GDUMP		WITNESS_RANK_LEAF
#define	WITNESS_RANK_PROF_NEXT_THR_UID	WITNESS_RANK_LEAF
#define	WITNESS_RANK_PROF_THREAD_ACTIVE_INIT	WITNESS_RANK_LEAF

#define	WITNESS_INITIALIZER(name, rank) {name, rank, NULL, NULL, {NULL, NULL}}

#endif /* JEMALLOC_H_TYPES */
/******************************************************************************/
#ifdef JEMALLOC_H_STRUCTS

struct witness_s {
	/* Name, used for printing lock order reversal messages. */
	const char		*name;

	/*
	 * Witness rank, where 0 is lowest and UINT_MAX is highest.  Witnesses
	 * must be acquired in order of increasing rank.
	 */
	witness_rank_t		rank;

	/*
	 * If two witnesses are of equal rank and they have the samp comp
	 * function pointer, it is called as a last attempt to differentiate
	 * between witnesses of equal rank.
	 */
	witness_comp_t		*comp;

	/* Opaque data, passed to comp(). */
	void			*opaque;

	/* Linkage for thread's currently owned locks. */
	ql_elm(witness_t)	link;
};

#endif /* JEMALLOC_H_STRUCTS */
/******************************************************************************/
#ifdef JEMALLOC_H_EXTERNS

void	witness_init(witness_t *witness, const char *name, witness_rank_t rank,
    witness_comp_t *comp, void *opaque);
#ifdef JEMALLOC_JET
typedef void (witness_lock_error_t)(const witness_list_t *, const witness_t *);
extern witness_lock_error_t *witness_lock_error;
#else
void	witness_lock_error(const witness_list_t *witnesses,
    const witness_t *witness);
#endif
#ifdef JEMALLOC_JET
typedef void (witness_owner_error_t)(const witness_t *);
extern witness_owner_error_t *witness_owner_error;
#else
void	witness_owner_error(const witness_t *witness);
#endif
#ifdef JEMALLOC_JET
typedef void (witness_not_owner_error_t)(const witness_t *);
extern witness_not_owner_error_t *witness_not_owner_error;
#else
void	witness_not_owner_error(const witness_t *witness);
#endif
#ifdef JEMALLOC_JET
typedef void (witness_lockless_error_t)(const witness_list_t *);
extern witness_lockless_error_t *witness_lockless_error;
#else
void	witness_lockless_error(const witness_list_t *witnesses);
#endif

void	witnesses_cleanup(tsd_t *tsd);
void	witness_prefork(tsd_t *tsd);
void	witness_postfork_parent(tsd_t *tsd);
void	witness_postfork_child(tsd_t *tsd);

#endif /* JEMALLOC_H_EXTERNS */
/******************************************************************************/
#ifdef JEMALLOC_H_INLINES

#ifndef JEMALLOC_ENABLE_INLINE
bool	witness_owner(tsd_t *tsd, const witness_t *witness);
void	witness_assert_owner(tsdn_t *tsdn, const witness_t *witness);
void	witness_assert_not_owner(tsdn_t *tsdn, const witness_t *witness);
void	witness_assert_lockless(tsdn_t *tsdn);
void	witness_lock(tsdn_t *tsdn, witness_t *witness);
void	witness_unlock(tsdn_t *tsdn, witness_t *witness);
#endif

#if (defined(JEMALLOC_ENABLE_INLINE) || defined(JEMALLOC_MUTEX_C_))
/* Helper, not intended for direct use. */
JEMALLOC_INLINE bool
witness_owner(tsd_t *tsd, const witness_t *witness)
{
	witness_list_t *witnesses;
	witness_t *w;

	cassert(config_debug);

	witnesses = tsd_witnessesp_get(tsd);
	ql_foreach(w, witnesses, link) {
		if (w == witness)
			return (true);
	}

	return (false);
}

JEMALLOC_INLINE void
witness_assert_owner(tsdn_t *tsdn, const witness_t *witness)
{
	tsd_t *tsd;

	if (!config_debug)
		return;

	if (tsdn_null(tsdn))
		return;
	tsd = tsdn_tsd(tsdn);
	if (witness->rank == WITNESS_RANK_OMIT)
		return;

	if (witness_owner(tsd, witness))
		return;
	witness_owner_error(witness);
}

JEMALLOC_INLINE void
witness_assert_not_owner(tsdn_t *tsdn, const witness_t *witness)
{
	tsd_t *tsd;
	witness_list_t *witnesses;
	witness_t *w;

	if (!config_debug)
		return;

	if (tsdn_null(tsdn))
		return;
	tsd = tsdn_tsd(tsdn);
	if (witness->rank == WITNESS_RANK_OMIT)
		return;

	witnesses = tsd_witnessesp_get(tsd);
	ql_foreach(w, witnesses, link) {
		if (w == witness)
			witness_not_owner_error(witness);
	}
}

JEMALLOC_INLINE void
witness_assert_lockless(tsdn_t *tsdn)
{
	tsd_t *tsd;
	witness_list_t *witnesses;
	witness_t *w;

	if (!config_debug)
		return;

	if (tsdn_null(tsdn))
		return;
	tsd = tsdn_tsd(tsdn);

	witnesses = tsd_witnessesp_get(tsd);
	w = ql_last(witnesses, link);
	if (w != NULL)
		witness_lockless_error(witnesses);
}

JEMALLOC_INLINE void
witness_lock(tsdn_t *tsdn, witness_t *witness)
{
	tsd_t *tsd;
	witness_list_t *witnesses;
	witness_t *w;

	if (!config_debug)
		return;

	if (tsdn_null(tsdn))
		return;
	tsd = tsdn_tsd(tsdn);
	if (witness->rank == WITNESS_RANK_OMIT)
		return;

	witness_assert_not_owner(tsdn, witness);

	witnesses = tsd_witnessesp_get(tsd);
	w = ql_last(witnesses, link);
	if (w == NULL) {
		/* No other locks; do nothing. */
	} else if (tsd_witness_fork_get(tsd) && w->rank <= witness->rank) {
		/* Forking, and relaxed ranking satisfied. */
	} else if (w->rank > witness->rank) {
		/* Not forking, rank order reversal. */
		witness_lock_error(witnesses, witness);
	} else if (w->rank == witness->rank && (w->comp == NULL || w->comp !=
	    witness->comp || w->comp(w, w->opaque, witness, witness->opaque) >
	    0)) {
		/*
		 * Missing/incompatible comparison function, or comparison
		 * function indicates rank order reversal.
		 */
		witness_lock_error(witnesses, witness);
	}

	ql_elm_new(witness, link);
	ql_tail_insert(witnesses, witness, link);
}

JEMALLOC_INLINE void
witness_unlock(tsdn_t *tsdn, witness_t *witness)
{
	tsd_t *tsd;
	witness_list_t *witnesses;

	if (!config_debug)
		return;

	if (tsdn_null(tsdn))
		return;
	tsd = tsdn_tsd(tsdn);
	if (witness->rank == WITNESS_RANK_OMIT)
		return;

	/*
	 * Check whether owner before removal, rather than relying on
	 * witness_assert_owner() to abort, so that unit tests can test this
	 * function's failure mode without causing undefined behavior.
	 */
	if (witness_owner(tsd, witness)) {
		witnesses = tsd_witnessesp_get(tsd);
		ql_remove(witnesses, witness, link);
	} else
		witness_assert_owner(tsdn, witness);
}
#endif

#endif /* JEMALLOC_H_INLINES */
/******************************************************************************/
