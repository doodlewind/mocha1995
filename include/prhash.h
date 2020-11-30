/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil -*-
 */
#ifndef prhash_h___
#define prhash_h___

/*
** API to portable hash table stuff
*/
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "prmacros.h"
#include "prtypes.h"

NSPR_BEGIN_EXTERN_C

typedef struct PRHashEntryStr PRHashEntry;
typedef struct PRHashTableStr PRHashTable;
typedef uint32 PRHashNumber;
#define PR_HASH_BITS 32
typedef PRHashNumber (*PRHashFunction)(const void *key);
typedef int (*PRHashComparator)(const void *v1, const void *v2);
typedef int (*PRHashEnumerator)(PRHashEntry *he, int i, void *arg);

/* Flag bits in PRHashEnumerator's return value */
#define HT_ENUMERATE_NEXT       0       /* continue enumerating entries */
#define HT_ENUMERATE_STOP       1       /* stop enumerating entries */
#define HT_ENUMERATE_REMOVE     2       /* remove and free the current entry */
#define HT_ENUMERATE_UNHASH     4       /* just unhash the current entry */

typedef struct PRHashAllocOps {
    void *              (*allocTable)(void *pool, size_t size);
    void                (*freeTable)(void *pool, void *item);
    PRHashEntry *       (*allocEntry)(void *pool);
    void                (*freeEntry)(void *pool, PRHashEntry *he, int flag);
} PRHashAllocOps;

#define HT_FREE_VALUE   0               /* just free the entry's value */
#define HT_FREE_ENTRY   1               /* free value and entire entry */
    
struct PRHashEntryStr {
    PRHashEntry         *next;          /* hash chain linkage */
    PRHashNumber        keyHash;        /* key hash function result */
    const void          *key;           /* ptr to opaque key */
    void                *value;         /* ptr to opaque value */
};

struct PRHashTableStr {
    PRHashEntry         **buckets;      /* vector of hash buckets */
    unsigned int        nentries;       /* number of entries in table */
    unsigned int        shift;          /* multiplicative hash shift */
    PRHashFunction      keyHash;        /* key hash function */
    PRHashComparator    keyCompare;     /* key comparison function */
    PRHashComparator    valueCompare;   /* value comparison function */
    PRHashAllocOps      *allocOps;      /* allocation operations */
    void                *allocPool;     /* allocation private data */
#ifdef HASHMETER
    unsigned int        nlookups;       /* total number of lookups */
    unsigned int        nsteps;         /* number of hash chains traversed */
    unsigned int        ngrows;         /* number of table expansions */
    unsigned int        nshrinks;       /* number of table contractions */
#endif
};

/*
** Create a new hash table.
** If allocOps is null, use default allocator ops built on top of malloc().
*/
extern PR_PUBLIC_API(PRHashTable *)
PR_NewHashTable(uint32 n, PRHashFunction keyHash,
                PRHashComparator keyCompare, PRHashComparator valueCompare,
                PRHashAllocOps *allocOps, void *allocPool);

extern PR_PUBLIC_API(void)
PR_HashTableDestroy(PRHashTable *ht);

/* Low level access methods */
extern PR_PUBLIC_API(PRHashEntry **)
PR_HashTableRawLookup(PRHashTable *ht, PRHashNumber keyHash, const void *key);

extern PR_PUBLIC_API(PRHashEntry *)
PR_HashTableRawAdd(PRHashTable *ht, PRHashEntry **hep, PRHashNumber keyHash,
                   const void *key, void *value);

extern PR_PUBLIC_API(void)
PR_HashTableRawRemove(PRHashTable *ht, PRHashEntry **hep, PRHashEntry *he);

/* Higher level access methods */
extern PR_PUBLIC_API(PRHashEntry *)
PR_HashTableAdd(PRHashTable *ht, const void *key, void *value);

extern PR_PUBLIC_API(PRBool)
PR_HashTableRemove(PRHashTable *ht, const void *key);

extern PR_PUBLIC_API(int)
PR_HashTableEnumerateEntries(PRHashTable *ht, PRHashEnumerator f, void *arg);

extern PR_PUBLIC_API(void *)
PR_HashTableLookup(PRHashTable *ht, const void *key);

extern PR_PUBLIC_API(int)
PR_HashTableDump(PRHashTable *ht, PRHashEnumerator dump, FILE *fp);

/* Standard string hash and compare functions */
extern PR_PUBLIC_API(PRHashNumber)
PR_HashString(const void *key);

PR_PUBLIC_API(int)
PR_CompareStrings(const void *v1, const void *v2);

/* Stub function just returns v1 == v2 */
PR_PUBLIC_API(int)
PR_CompareValues(const void *v1, const void *v2);

NSPR_END_EXTERN_C

#endif /* prhash_h___ */
