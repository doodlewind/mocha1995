/*
** Mocha atom table.
**
** Brendan Eich, 6/16/95
*/
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "prhash.h"
#include "prlog.h"
#include "prmem.h"
#include "mo_atom.h"
#include "mo_bcode.h"
#include "mo_emit.h"
#include "mo_cntxt.h"
#include "mocha.h"
#include "mochaapi.h"

MochaAtom *mocha_typeAtoms[MOCHA_NTYPES];
MochaAtom *mocha_booleanAtoms[2];
MochaAtom *mocha_nullAtom;

MochaAtom *mocha_anonymousAtom;
MochaAtom *mocha_assignAtom;
MochaAtom *mocha_constructorAtom;
MochaAtom *mocha_finalizeAtom;
MochaAtom *mocha_getPropertyAtom;
MochaAtom *mocha_listPropertiesAtom;
MochaAtom *mocha_prototypeAtom;
MochaAtom *mocha_resolveNameAtom;
MochaAtom *mocha_setPropertyAtom;
MochaAtom *mocha_toStringAtom;
MochaAtom *mocha_valueOfAtom;

/* Keep this in sync with mochaapi.h -- an assertion below will insist. */
char *mocha_typeStr[] = {
    "undefined",
    "internal",
    "atom",
    "symbol",
    "function",
    "object",
    "number",
    "boolean",
    "string",
};

char mocha_anonymousStr[]       = "anonymous";
char mocha_assignStr[]          = "assign";
char mocha_constructorStr[]     = "constructor";
char mocha_finalizeStr[]        = "finalize";
char mocha_getPropertyStr[]     = "getProperty";
char mocha_listPropertiesStr[]  = "listProperties";
char mocha_prototypeStr[]       = "prototype";
char mocha_resolveNameStr[]     = "resolveName";
char mocha_setPropertyStr[]     = "setProperty";
char mocha_toStringStr[]        = "toString";
char mocha_valueOfStr[]         = "valueOf";

MochaAtomState mocha_AtomState;

PR_STATIC_CALLBACK(void *)
AllocAtomSpace(void *pool, size_t size)
{
    return malloc(size);
}

PR_STATIC_CALLBACK(void)
FreeAtomStub(void *pool, void *item)
{
    free(item);
}

PR_STATIC_CALLBACK(PRHashEntry *)
AllocAtom(void *pool)
{
    MochaAtom *atom;
    
    atom = PR_NEW(MochaAtom);
    if (!atom)
	return 0;
    return &atom->entry;
}

PR_STATIC_CALLBACK(void)
FreeAtom(void *pool, PRHashEntry *he, int flag)
{
    MochaAtom *atom = (MochaAtom *)he;

    PR_ASSERT(flag == HT_FREE_ENTRY);
    if (flag == HT_FREE_ENTRY) {
	free((char *)atom->entry.key);
	PR_DELETE(atom);
    }
}

static PRHashAllocOps atomAllocOps = {
    AllocAtomSpace, FreeAtomStub,
    AllocAtom, FreeAtom
};

#define MOCHA_ATOM_HASH_SIZE	1024

MochaBoolean
mocha_InitAtomState(MochaContext *mc)
{
    unsigned i;
    MochaAtom *atom;

    if (mocha_AtomState.valid) {
	mocha_AtomState.nrefs++;
	return MOCHA_TRUE;
    }

    mocha_AtomState.table = PR_NewHashTable(MOCHA_ATOM_HASH_SIZE,
					    PR_HashString,
					    PR_CompareStrings,
					    PR_CompareValues,
					    &atomAllocOps, 0);
    if (!mocha_AtomState.table) {
	MOCHA_ReportOutOfMemory(mc);
	return MOCHA_FALSE;
    }

#define FROB(lval,str,type) {                                                 \
    if (lval) mocha_DropAtom(mc, lval);                                       \
    lval = atom = mocha_Atomize(mc, str, ATOM_HELD | type);                   \
    if (!atom) return MOCHA_FALSE;                                            \
}

    PR_ASSERT(sizeof mocha_typeStr / sizeof mocha_typeStr[0] == MOCHA_NTYPES);
    for (i = 0; i < MOCHA_NTYPES; i++)
	FROB(mocha_typeAtoms[i],    mocha_typeStr[i],         ATOM_NAME);

    /* XXX redundant w.r.t. mo_scan.c */
    FROB(mocha_booleanAtoms[0],     mocha_false,              ATOM_KEYWORD);
    FROB(mocha_booleanAtoms[1],     mocha_true,               ATOM_KEYWORD);
    atom->fval = 1;
    FROB(mocha_nullAtom,            mocha_null,               ATOM_KEYWORD);

    FROB(mocha_anonymousAtom,       mocha_anonymousStr,       ATOM_NAME);
    FROB(mocha_assignAtom,          mocha_assignStr,          ATOM_NAME);
    FROB(mocha_constructorAtom,     mocha_constructorStr,     ATOM_NAME);
    FROB(mocha_finalizeAtom,        mocha_finalizeStr,        ATOM_NAME);
    FROB(mocha_getPropertyAtom,     mocha_getPropertyStr,     ATOM_NAME);
    FROB(mocha_listPropertiesAtom,  mocha_listPropertiesStr,  ATOM_NAME);
    FROB(mocha_prototypeAtom,       mocha_prototypeStr,       ATOM_NAME);
    FROB(mocha_resolveNameAtom,     mocha_resolveNameStr,     ATOM_NAME);
    FROB(mocha_setPropertyAtom,     mocha_setPropertyStr,     ATOM_NAME);
    FROB(mocha_toStringAtom,        mocha_toStringStr,        ATOM_NAME);
    FROB(mocha_valueOfAtom,         mocha_valueOfStr,         ATOM_NAME);

#undef FROB

    mocha_AtomState.valid = MOCHA_TRUE;
    mocha_AtomState.nrefs = 1;
    return MOCHA_TRUE;
}

void
mocha_FreeAtomState(MochaContext *mc)
{
    PR_ASSERT(mocha_AtomState.nrefs > 0);
    if (mocha_AtomState.nrefs <= 0) return;
    if (--mocha_AtomState.nrefs == 0) {
	PR_HashTableDestroy(mocha_AtomState.table);
	memset(&mocha_AtomState, 0, sizeof mocha_AtomState);
    }
}

MochaAtom *
mocha_Atomize(MochaContext *mc, const char *string, MochaAtomFlags flags)
{
    MochaBoolean doHold;
    unsigned length;
    PRHashNumber keyHash;
    PRHashEntry *he, **hep;
    char *newString;
    MochaAtom *atom;

    doHold  = (flags & ATOM_HELD) ? MOCHA_TRUE : MOCHA_FALSE;
    flags &= ATOM_TYPEMASK;
    length = strlen(string);

    keyHash = PR_HashString(string);
    hep = PR_HashTableRawLookup(mocha_AtomState.table, keyHash, string);
    if ((he = *hep) != 0) {
        atom = (MochaAtom *)he;
	atom->flags |= flags;
    } else {
	newString = MOCHA_malloc(mc, length + 1);
	if (!newString)
	    return 0;
	strcpy(newString, string);
	he = PR_HashTableRawAdd(mocha_AtomState.table, hep, keyHash,
				newString, 0);
	if (!he) {
	    MOCHA_ReportOutOfMemory(mc);
	    return 0;
	}
	atom = (MochaAtom *)he;
	atom->entry.value = 0;
	atom->nrefs = 0;
	atom->length = length;
	atom->flags = flags;
	atom->keyIndex = -1;
	atom->index = 0;
	atom->number = mocha_AtomState.number++;
	atom->fval = 0;
    }
#ifdef DEBUG_brendan
    hep = PR_HashTableRawLookup(mocha_AtomState.table, keyHash, string);
    he = *hep;
    PR_ASSERT(atom == (MochaAtom *)he);
#endif

    if (doHold)
	mocha_HoldAtom(mc, atom);
    return atom;
}

MochaAtomNumber
mocha_IndexAtom(MochaContext *mc, MochaAtom *atom, CodeGenerator *cg)
{
    if (!(atom->flags & ATOM_INDEXED)) {
	atom->flags |= ATOM_INDEXED;
	atom->index = (uint16) cg->atomCount++;
	atom->entry.value = cg->atomList;
	cg->atomList = mocha_HoldAtom(mc, atom);
    }
    return atom->index;
}

MochaAtom *
mocha_HoldAtom(MochaContext *mc, MochaAtom *atom)
{
#ifdef DEBUG_brendan
    const char *string;
    PRHashNumber keyHash;
    PRHashEntry *he, **hep;

    string = atom->entry.key;
    keyHash = PR_HashString(string);
    hep = PR_HashTableRawLookup(mocha_AtomState.table, keyHash, string);
    he = *hep;
    PR_ASSERT(atom == (MochaAtom *)he);
#endif
    atom->nrefs++;
    PR_ASSERT(atom->nrefs > 0);
    return atom;
}

MochaAtom *
mocha_DropAtom(MochaContext *mc, MochaAtom *atom)
{
#ifdef DEBUG_brendan
    const char *string;
    PRHashNumber keyHash;
    PRHashEntry *he, **hep;

    string = strdup(atom->entry.key);
    if (string) {
	keyHash = PR_HashString(string);
	hep = PR_HashTableRawLookup(mocha_AtomState.table, keyHash, string);
	he = *hep;
	PR_ASSERT(atom == (MochaAtom *)he);
    }
#endif
    PR_ASSERT(atom->nrefs > 0);
    if (atom->nrefs <= 0) return 0;
    if (--atom->nrefs == 0) {
	PR_HashTableRemove(mocha_AtomState.table, atom->entry.key);
#ifdef DEBUG_brendan
	if (string) {
	    hep = PR_HashTableRawLookup(mocha_AtomState.table, keyHash, string);
	    he = *hep;
	    PR_ASSERT(he == 0);
	}
#endif
	atom = 0;
    }
#ifdef DEBUG_brendan
    PR_FREEIF((char *)string);
#endif
    return atom;
}

MochaAtom *
mocha_GetAtom(MochaContext *mc, MochaAtomMap *map, MochaAtomNumber n)
{
    MochaAtom *atom;

    PR_ASSERT(map->vector && n < map->length);
    if (!map->vector || n >= map->length) {
	MOCHA_ReportError(mc, "internal error: no index for atom %ld", (long)n);
	return 0;
    }
    atom = map->vector[n];
    PR_ASSERT(atom);
    return atom;
}

MochaBoolean
mocha_InitAtomMap(MochaContext *mc, MochaAtomMap *map, CodeGenerator *cg)
{
    MochaAtom *atom, *next, **vector;
    MochaAtomNumber length;

    atom = cg->atomList;
    if (!atom) {
	map->vector = 0, map->length = 0;
	return MOCHA_TRUE;
    }

    length = cg->atomCount;
    if (length >= MOCHA_ATOM_INDEX_MAX) {
        MOCHA_ReportError(mc, "too many atoms");
	return MOCHA_FALSE;
    }
    vector = MOCHA_malloc(mc, length * sizeof *vector);
    if (!vector)
	return MOCHA_FALSE;

    do {
        vector[atom->index] = atom;
	atom->flags &= ~ATOM_INDEXED;
	next = atom_next(atom);
	atom->entry.value = 0;
    } while ((atom = next) != 0);
    cg->atomList = 0;
    cg->atomCount = 0;

    map->vector = vector;
    map->length = length;
    return MOCHA_TRUE;
}

void
mocha_FreeAtomMap(MochaContext *mc, MochaAtomMap *map)
{
    unsigned i;

    if (map->vector) {
        for (i = 0; i < map->length; i++)
	    mocha_DropAtom(mc, map->vector[i]);
	free(map->vector);
	map->vector = 0;
    }
    map->length = 0;
}

void
mocha_DropUnmappedAtoms(MochaContext *mc, CodeGenerator *cg)
{
    MochaAtom *atom, *next;

    for (atom = cg->atomList; atom; atom = next) {
	atom->flags &= ~ATOM_INDEXED;
	next = atom_next(atom);
	atom->entry.value = 0;
	mocha_DropAtom(mc, atom);
    }
    cg->atomList = 0;
    cg->atomCount = 0;
}
