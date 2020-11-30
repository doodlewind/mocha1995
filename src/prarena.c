/*
** Lifetime-based fast allocation, inspired by much prior art, including
** "Fast Allocation and Deallocation of Memory Based on Object Lifetimes"
** David R. Hanson, Software -- Practice and Experience, Vol. 20(1).
**
** Brendan Eich, 6/16/95
*/

#include <stdlib.h>
#include <string.h>
#include "prtypes.h"
#include "prglobal.h"
#include "prarena.h"
#include "prmem.h"
#include "prsync.h"
#include "prarena.h"

static PRArena *freeArenas;

#ifdef ARENAMETER
static PRArenaStats *arenaStats;

#define COUNT(ap,what)  (ap)->stats.what++
#else
#define COUNT(ap,what)  /* nothing */
#endif

#define PR_ARENA_DEFAULT_ALIGN  sizeof(double)


PR_PUBLIC_API(void)
PR_InitArenaPool(PRArenaPool *ap, const char *name, size_t size, size_t align)
{
    if (align == 0)
	align = PR_ARENA_DEFAULT_ALIGN;
    ap->mask = PR_BITMASK(PR_CeilingLog2(align));
    ap->first.next = 0;
    ap->first.avail = ap->first.limit =
	(uprword_t)PR_ARENA_ALIGN(ap, &ap->first + 1);
    ap->current = &ap->first;
    ap->minsize = size;
#ifdef ARENAMETER
    memset(&ap->stats, 0, sizeof ap->stats);
    ap->stats.name = strdup(name);
    ap->stats.next = arenaStats;
    arenaStats = &ap->stats;
#endif
}

PR_PUBLIC_API(void *)
PR_ArenaAllocate(PRArenaPool *ap, size_t nb)
{
    PRArena *a, *b;
    size_t sz;
    void *p;

    PR_ASSERT((nb & ap->mask) == 0);
#if defined(XP_PC) && !defined(_WIN32)
    if (nb >= 60000U) return NULL;
#endif  /* WIN16 */
    for (a = ap->current; (uprword_t)a->avail + nb > a->limit; ap->current = a) {
        if (a->next) {                          /* move to next arena */
            a = a->next;
        } else if ((b = freeArenas) != 0) {	/* reclaim a free arena */
            if (PR_CAS(b->next, b, &freeArenas) != b)
		continue;
	    a = a->next = b;
            a->next = 0;
            COUNT(ap, nreclaims);
        } else {                                /* allocate a new arena */
            sz = nb + ap->minsize + sizeof *a;
#if defined(XP_PC) && !defined(_WIN32)
	    if (sz < nb) return NULL;
#endif  /* WIN16 */
	    b = malloc(sz);
	    if (!b) return 0;
            a = a->next = b;
            a->limit = (uprword_t)a + sz;
            a->next = 0;
            COUNT(ap, nmallocs);
        }
        a->avail = (uprword_t)PR_ARENA_ALIGN(ap, a + 1);
    }
    p = (void *)a->avail;
    a->avail += nb;
    return p;
}

PR_PUBLIC_API(void *)
PR_ArenaGrow(PRArenaPool *ap, void *p, size_t size, size_t incr)
{
    void *newp;

    PR_ARENA_ALLOCATE(newp, ap, size + incr);
    memcpy(newp, p, size);
    return newp;
}

static void
PR_FreeArenaList(PRArenaPool *ap, PRArena *head, PRBool reallyFree)
{
    PRArena *last, *next, *curr;

    last = ap->current;
    next = head->next;
    if (!next) {
	PR_ASSERT(last == head);
	return;
    }

#ifdef DEBUG
    {
	uprword_t base;

	curr = head;
	do {
	    curr = curr->next;
	    base = (uprword_t)PR_ARENA_ALIGN(ap, curr + 1);
	    PR_ASSERT(base <= curr->avail && curr->avail <= curr->limit);
	    curr->avail = base;
	    PR_CLEAR_UNUSED(curr);
	} while (curr != last);
    }
#endif

    if (reallyFree) {
	/*
	** Really free each arena from next through last, one by one.
	*/
	do {
	    curr = next;
	    next = curr->next;
#ifdef DEBUG
	    memset(curr, 0xDA, sizeof *curr);
#endif
	    free(curr);
	} while (next != 0);
    } else {
	/*
	** Insert the whole arena chain at the front of the freelist.
	** Loop until the compare-and-swap succeeds.
	*/
	do {
	    curr = freeArenas;
	    last->next = curr;
	} while (PR_CAS(next, curr, &freeArenas) != curr);
    }

    head->next = 0;
    ap->current = head;
}

PR_PUBLIC_API(void)
PR_ArenaRelease(PRArenaPool *ap, char *mark)
{
    PRArena *a;

    for (a = ap->first.next; a; a = a->next) {
	if (PR_UPTRDIFF(mark, a) < PR_UPTRDIFF(a->avail, a)) {
	    a->avail = (uprword_t)PR_ARENA_ALIGN(ap, mark);
	    PR_FreeArenaList(ap, a, PR_TRUE);
	    return;
	}
    }
}

PR_PUBLIC_API(void)
PR_FreeArenaPool(PRArenaPool *ap)
{
    PR_FreeArenaList(ap, &ap->first, PR_FALSE);
    COUNT(ap, ndeallocs);
}

PR_PUBLIC_API(void)
PR_FinishArenaPool(PRArenaPool *ap)
{
    PR_FreeArenaList(ap, &ap->first, PR_TRUE);
#ifdef ARENAMETER
    {
	PRArenaStats *as, **asp;

	PR_FREEIF(ap->stats.name);
	for (asp = &arenaStats; (as = *asp) != 0; asp = &as->next) {
	    if (as == &ap->stats) {
		*asp = as->next;
		return;
	    }
	}
    }
#endif
}

PR_PUBLIC_API(void)
PR_CompactArenaPool(PRArenaPool *ap)
{
#if XP_MAC
    PRArena *curr = &(ap->first);
    while (curr) {
        reallocSmaller(curr, curr->avail - (uprword_t)curr);
        curr->limit = curr->avail;
        curr = curr->next;
    }
#endif
}

PR_PUBLIC_API(void)
PR_ArenaFinish()
{
    PRArena *a, *next;

    for (a = freeArenas; a; a = next) {
        next = a->next;
        free(a);
    }
}

#ifdef ARENAMETER
PR_PUBLIC_API(void)
PR_ArenaCountAllocation(PRArenaPool *ap, size_t nb)
{
    ap->stats.nallocs++;
    ap->stats.nbytes += nb;
    if (nb > ap->stats.maxsize)
        ap->stats.maxsize = nb;
    ap->stats.variance += nb * nb;
}

PR_PUBLIC_API(void)
PR_ArenaCountInplaceGrowth(PRArenaPool *ap, size_t size, size_t incr)
{
    ap->stats.ninplace++;
}

PR_PUBLIC_API(void)
PR_ArenaCountGrowth(PRArenaPool *ap, size_t size, size_t incr)
{
    ap->stats.ngrows++;
    ap->stats.nbytes += incr;
    ap->stats.variance -= size * size;
    size += incr;
    if (size > ap->stats.maxsize)
        ap->stats.maxsize = size;
    ap->stats.variance += size * size;
}

PR_PUBLIC_API(void)
PR_ArenaCountRelease(PRArenaPool *ap, char *mark)
{
    ap->stats.nreleases++;
}

PR_PUBLIC_API(void)
PR_ArenaCountRetract(PRArenaPool *ap, char *mark)
{
    ap->stats.nfastrels++;
}

#include <math.h>
#include <stdio.h>

PR_PUBLIC_API(void)
PR_DumpArenaStats(FILE *fp)
{
    PRArenaStats *as;
    double mean, variance;

    for (as = arenaStats; as; as = as->next) {
        if (as->nallocs != 0) {
	    mean = (double)as->nbytes / as->nallocs;
	    variance = fabs(as->variance / as->nallocs - mean * mean);
	} else {
	    mean = variance = 0;
	}

        fprintf(fp, "\n%s allocation statistics:\n", as->name);
        fprintf(fp, "         number of allocations: %u\n", as->nallocs);
        fprintf(fp, " number of free arena reclaims: %u\n", as->nreclaims);
        fprintf(fp, "      number of malloc() calls: %u\n", as->nmallocs);
        fprintf(fp, "       number of deallocations: %u\n", as->ndeallocs);
        fprintf(fp, "  number of allocation growths: %u\n", as->ngrows);
        fprintf(fp, "    number of in-place growths: %u\n", as->ninplace);
        fprintf(fp, "number of released allocations: %u\n", as->nreleases);
        fprintf(fp, "       number of fast releases: %u\n", as->nfastrels);
        fprintf(fp, "         total bytes allocated: %u\n", as->nbytes);
        fprintf(fp, "          mean allocation size: %g\n", mean);
        fprintf(fp, "            standard deviation: %g\n", sqrt(variance));
        fprintf(fp, "       maximum allocation size: %u\n", as->maxsize);
    }
}
#endif
