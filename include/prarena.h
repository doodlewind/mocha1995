#ifndef prarena_h___
#define prarena_h___
/*
** Lifetime-based fast allocation, inspired by much prior art, including
** "Fast Allocation and Deallocation of Memory Based on Object Lifetimes"
** David R. Hanson, Software -- Practice and Experience, Vol. 20(1).
**
** Also supports LIFO allocation (PR_ARENA_MARK/PR_ARENA_RELEASE).
**
** Brendan Eich, 6/16/95
*/
#include <stddef.h>
#include "prtypes.h"

typedef struct PRArena          PRArena;
typedef struct PRArenaPool      PRArenaPool;

struct PRArena {
    PRArena     *next;          /* next arena for this lifetime */
    uprword_t   limit;          /* one beyond last byte in arena */
    uprword_t   avail;          /* points to next available byte */
};

#ifdef ARENAMETER
typedef struct PRArenaStats PRArenaStats;

struct PRArenaStats {
    PRArenaStats *next;         /* next in arenaStats list */
    char        *name;          /* name for debugging */
    uint32      nallocs;        /* number of PR_ARENA_ALLOCATE() calls */
    uint32      nreclaims;      /* number of reclaims from freeArenas */
    uint32      nmallocs;       /* number of malloc() calls */
    uint32      ndeallocs;      /* number of lifetime deallocations */
    uint32      ngrows;         /* number of PR_ARENA_GROW() calls */
    uint32      ninplace;       /* number of in-place growths */
    uint32      nreleases;      /* number of PR_ARENA_RELEASE() calls */
    uint32      nfastrels;      /* number of "fast path" releases */
    size_t      nbytes;         /* total bytes allocated */
    size_t      maxsize;        /* maximum allocation size in bytes */
    double      variance;       /* size variance accumulator */
};
#endif

struct PRArenaPool {
    PRArena     first;          /* first arena in pool list */
    PRArena     *current;       /* arena from which to allocate space */
    size_t      minsize;        /* net minimum size of a new arena */
    uprword_t   mask;           /* alignment mask (power-of-2 - 1) */
#ifdef ARENAMETER
    PRArenaStats stats;
#endif
};

/*
** If the including .c file uses only one power-of-2 alignment, it may define
** PR_ARENA_CONST_ALIGN_MASK to the alignment mask and save a few instructions
** per ALLOCATE and GROW.
*/
#ifdef PR_ARENA_CONST_ALIGN_MASK
#define PR_ARENA_ALIGN(ap, n)	(((uprword_t)(n) + PR_ARENA_CONST_ALIGN_MASK) \
				 & ~PR_ARENA_CONST_ALIGN_MASK)

#define PR_INIT_ARENA_POOL(ap, name, size) \
	PR_InitArenaPool(ap, name, size, PR_ARENA_CONST_ALIGN_MASK + 1)
#else
#define PR_ARENA_ALIGN(ap, n)   (((uprword_t)(n) + (ap)->mask) & ~(ap)->mask)
#endif

#define PR_ARENA_ALLOCATE(p, ap, nb)                                          \
    NSPR_BEGIN_MACRO                                                          \
	PRArena *_a = (ap)->current;                                          \
	size_t _nb = (size_t)PR_ARENA_ALIGN(ap, nb);                          \
	uprword_t _p = _a->avail;                                             \
	uprword_t _q = _p + _nb;                                              \
	if (_q > _a->limit)                                                   \
	    _p = (uprword_t)PR_ArenaAllocate(ap, _nb);                        \
	else                                                                  \
	    _a->avail = _q;                                                   \
	p = (void *)_p;                                                       \
	PR_ArenaCountAllocation(ap, nb);                                      \
    NSPR_END_MACRO

#define PR_ARENA_GROW(p, ap, size, incr)                                      \
    NSPR_BEGIN_MACRO                                                          \
	PRArena *_a = (ap)->current;                                          \
	size_t _incr = PR_ARENA_ALIGN(ap, incr);                              \
	if (_a->avail == (uprword_t)(p) + PR_ARENA_ALIGN(ap, size) &&         \
	    _a->avail + _incr <= _a->limit) {                                 \
	    _a->avail += _incr;                                               \
	    PR_ArenaCountInplaceGrowth(ap, size, incr);                       \
	} else {                                                              \
	    p = PR_ArenaGrow(ap, p, size, incr);                              \
	}                                                                     \
	PR_ArenaCountGrowth(ap, size, incr);                                  \
    NSPR_END_MACRO

#define PR_ARENA_MARK(ap)	((void *) (ap)->current->avail)
#define PR_UPTRDIFF(p,q)	((uprword_t)(p) - (uprword_t)(q))

#ifdef DEBUG
#define PR_CLEAR_UNUSED(a)	(PR_ASSERT((a)->avail <= (a)->limit),         \
				 memset((void*)(a)->avail, 0xDA,              \
					(a)->limit - (a)->avail))
#else
#define PR_CLEAR_UNUSED(a)	/* nothing */
#endif

#define PR_ARENA_RELEASE(ap, mark)                                            \
    NSPR_BEGIN_MACRO                                                          \
	char *_m = (char *)(mark);                                            \
	PRArena *_a = (ap)->current;                                          \
	if (PR_UPTRDIFF(_m, _a) <= PR_UPTRDIFF(_a->avail, _a)) {              \
	    _a->avail = (uprword_t)PR_ARENA_ALIGN(ap, _m);                    \
	    PR_CLEAR_UNUSED(_a);                                              \
	    PR_ArenaCountRetract(ap, _m);                                     \
	} else {                                                              \
	    PR_ArenaRelease(ap, _m);                                          \
	}                                                                     \
	PR_ArenaCountRelease(ap, _m);                                         \
    NSPR_END_MACRO

/*
** Initialize an arena pool at ap, with the given name for debugging and stats
** labeling, and with a minimum size per arena of size bytes.
*/
extern PR_PUBLIC_API(void) PR_InitArenaPool(PRArenaPool *ap, const char *name, 
                                           size_t size, size_t align);

/*
** Free the arenas pooled in ap.  The user may continue to allocate from ap
** after calling this function.  There is no need to call PR_InitArenaPool()
** again unless PR_FinishArenaPool(ap) has been called.
*/
extern PR_PUBLIC_API(void) PR_FreeArenaPool(PRArenaPool *ap);

/*
** Free the arenas in ap and finish using it altogether.
*/
extern PR_PUBLIC_API(void) PR_FinishArenaPool(PRArenaPool *ap);

/*
** Compact all of the areans in a pools so that no space is wasted
** in the pool.
*/
extern PR_PUBLIC_API(void) PR_CompactArenaPool(PRArenaPool *ap);

/*
** Finish using arenas, freeing all memory associated with them.
*/
extern PR_PUBLIC_API(void) PR_ArenaFinish(void);

/* private functions used by the PR_ARENA_*() macros. */
extern PR_PUBLIC_API(void *) PR_ArenaAllocate(PRArenaPool *ap, size_t nb);
extern PR_PUBLIC_API(void *) PR_ArenaGrow(PRArenaPool *ap, void *p, size_t size, 
                                        size_t incr);
extern PR_PUBLIC_API(void) PR_ArenaRelease(PRArenaPool *ap, char *mark);

#ifdef ARENAMETER

#include <stdio.h>

extern PR_PUBLIC_API(void) PR_ArenaCountAllocation(PRArenaPool *ap, size_t nb);
extern PR_PUBLIC_API(void) PR_ArenaCountInplaceGrowth(PRArenaPool *ap, 
                                                     size_t size, size_t incr);
extern PR_PUBLIC_API(void) PR_ArenaCountGrowth(PRArenaPool *ap, size_t size, 
                                              size_t incr);
extern PR_PUBLIC_API(void) PR_ArenaCountRelease(PRArenaPool *ap, char *mark);
extern PR_PUBLIC_API(void) PR_ArenaCountRetract(PRArenaPool *ap, char *mark);
extern PR_PUBLIC_API(void) PR_DumpArenaStats(FILE *fp);

#else  /* !ARENAMETER */

#define PR_ArenaCountAllocation(ap, nb)                 /* nothing */
#define PR_ArenaCountInplaceGrowth(ap, size, incr)      /* nothing */
#define PR_ArenaCountGrowth(ap, size, incr)             /* nothing */
#define PR_ArenaCountRelease(ap, mark)                  /* nothing */
#define PR_ArenaCountRetract(ap, mark)                  /* nothing */

#endif /* !ARENAMETER */

#endif /* prarena_h___ */
