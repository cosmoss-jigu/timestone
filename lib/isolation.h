#ifndef _ISOLATION_H
#define _ISOLATION_H

#include "timestone_i.h"
#include "util.h"
#include "debug.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void isolation_reset(ts_isolation_info_t *isolation, int level)
{
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	ptrset_reset(&isolation->read_set);
	isolation->stale_read_occured = 0;
#else
	ts_assert(level == TS_SNAPSHOT);
#endif
	isolation->level = level;
}

static inline int isolation_init(ts_isolation_info_t *isolation)
{
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	int rc;
	rc = ptrset_init(&isolation->read_set);
	isolation_reset(isolation, TS_INVALID);
	return rc;
#else
	return 0;
#endif
}

static inline void isolation_deinit(ts_isolation_info_t *isolation)
{
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	isolation_reset(isolation, TS_INVALID);
	ptrset_deinit(&isolation->read_set);
#endif
}

static inline int stale_read_occurred(ts_isolation_info_t *isolation)
{
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	return (isolation->stale_read_occured &&
		isolation->level == TS_LINEARIZABILITY);
#endif
	return 0;
}

static inline void read_set_add(ts_isolation_info_t *isolation,
				ts_act_vhdr_t *p_act_vhdr, void *p_latest,
				void *p_copy)
{
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	ts_ptr_set_t *read_set = &isolation->read_set;

	switch (isolation->level) {
	case TS_LINEARIZABILITY:
		/* If the p_copy is not latest, we should abort here.
		 * However, we defer the abort until next try_lock()
		 * or reader_unlock(). Therefore, we don't need to update
		 * the read set because the transaction will fail anyway. */
		if (unlikely(!stale_read_occurred(isolation) &&
			     p_latest != NULL && p_latest != p_copy)) {
			isolation->stale_read_occured = 1;
			return;
		}
		/* Fall through */
	case TS_SERIALIZABILITY:
		/* p_act_vhdr and p_latest could be NULL. */
		ptrset_push(read_set, p_act_vhdr);
		ptrset_push(read_set, p_latest);
		ptrset_push(read_set, p_copy);
		return;
	case TS_SNAPSHOT:
		/* Do nothing */
		return;
	default:
		ts_assert(0 && "Never be here");
		return;
	};
#endif
}

static inline void write_set_add(ts_isolation_info_t *isolation, void *p_lock)
{
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	/* In fact, we don't need to track the write set
	 * because write without read is impossible
	 * and a version chain already has information
	 * on locking (i.e., p_lock). */
#endif
}

static inline int validate_read_set(ts_isolation_info_t *isolation)
{
#ifdef TS_ENABLE_SERIALIZABILITY_LINEARIZABILITY
	ts_ptr_set_t *read_set;
	int i, num;
	ts_act_vhdr_t *p_act_vhdr;
	void *p_latest, *p_copy;

	switch (isolation->level) {
	case TS_LINEARIZABILITY:
		if (stale_read_occurred(isolation)) {
			return 0;
		}
		/* Fall through */
	case TS_SERIALIZABILITY:
		read_set = &isolation->read_set;
		num = read_set->num_ptrs;

		for (i = 0; i < num;) {
			/* Fetch pointers */
			p_act_vhdr = read_set->ptrs[i++];
			p_latest = read_set->ptrs[i++];
			p_copy = read_set->ptrs[i++];

			/* If the p_copy had a copy ... */
			if (likely(p_act_vhdr)) {
				/* Check if a newer version is added */
				if (p_act_vhdr->p_copy != p_latest) {
					return 0;
				}
			}
			/* Otherwise ... */
			else {
				/* Check if a newer version is added */
				ts_assert(p_latest == NULL);
				p_act_vhdr = get_act_vhdr(p_copy);
				if (p_act_vhdr && p_act_vhdr->p_copy != NULL) {
					return 0;
				}
			}
		}
		return 1;
	case TS_SNAPSHOT:
		/* Do nothing */
		return 1;
	default:
		ts_assert(0 && "Never be here");
		return 0;
	};

#endif
	return 1;
}

#ifdef __cplusplus
}
#endif
#endif
