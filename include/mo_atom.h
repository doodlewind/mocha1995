#ifndef _mo_atom_h_
#define _mo_atom_h_
/*
** Mocha atom table.
**
** Brendan Eich, 6/16/95
*/
#include <stddef.h>
#include "prhash.h"
#include "prmacros.h"
#include "mo_prvtd.h"
#include "mochaapi.h"

NSPR_BEGIN_EXTERN_C

typedef uint8 MochaAtomFlags;

#define ATOM_KEYWORD    0x01            /* keyIndex is keyword table slot */
#define ATOM_NAME       0x02            /* atom is an identifier */
#define ATOM_NUMBER     0x04            /* atom is a numeric literal */
#define ATOM_STRING     0x08            /* atom is a string literal */
#define ATOM_HELD       0x40            /* ask mocha_Atomize() to hold atom */
#define ATOM_INDEXED    0x80            /* indexed for literal mapping */
#define ATOM_TYPEMASK   0x3f            /* isolate atom type bits */

struct MochaAtom {
    PRHashEntry         entry;          /* key is string, value keyword info */
    MochaRefCount       nrefs;          /* reference count (not at front!) */
    size_t              length;         /* length of atom name (entry.key) */
    MochaAtomFlags      flags;          /* tags atom name, keyIndex, and fval */
    uint8               keyIndex;       /* keyword index if ATOM_KEYWORD */
    uint16              index;          /* atom table index for literal map */
    MochaAtomNumber     number;         /* atom serial number and hash code */
    MochaFloat          fval;           /* value if atom is numeric literal */
};

#define atom_name(atom) ((const char *)(atom)->entry.key)
#define atom_next(atom) ((MochaAtom *)(atom)->entry.value)

struct MochaAtomMap {
    MochaAtom           **vector;       /* array of ptrs to indexed atoms */
    MochaAtomNumber     length;         /* count of (to-be-)indexed atoms */
};

struct MochaAtomState {
    MochaBoolean        valid;          /* successfully initialized */
    MochaRefCount       nrefs;          /* number of active contexts */
    PRHashTable         *table;         /* hash table containing all atoms */
    MochaAtomNumber     number;         /* count of all atoms */
};

/* Well-known predefined atoms and their strings. */
extern MochaAtom    *mocha_typeAtoms[MOCHA_NTYPES];
extern MochaAtom    *mocha_booleanAtoms[2];
extern MochaAtom    *mocha_nullAtom;

extern MochaAtom    *mocha_anonymousAtom;
extern MochaAtom    *mocha_assignAtom;
extern MochaAtom    *mocha_constructorAtom;
extern MochaAtom    *mocha_finalizeAtom;
extern MochaAtom    *mocha_getPropertyAtom;
extern MochaAtom    *mocha_listPropertiesAtom;
extern MochaAtom    *mocha_prototypeAtom;
extern MochaAtom    *mocha_resolveNameAtom;
extern MochaAtom    *mocha_setPropertyAtom;
extern MochaAtom    *mocha_toStringAtom;
extern MochaAtom    *mocha_valueOfAtom;

extern char         *mocha_typeStr[];

extern char         mocha_anonymousStr[];
extern char         mocha_assignStr[];
extern char         mocha_constructorStr[];
extern char         mocha_finalizeStr[];
extern char         mocha_getPropertyStr[];
extern char         mocha_listPropertiesStr[];
extern char         mocha_prototypeStr[];
extern char         mocha_resolveNameStr[];
extern char         mocha_setPropertyStr[];
extern char         mocha_toStringStr[];
extern char         mocha_valueOfStr[];

/*
** Initialize atom bookkeeping.  Return true on success, false with an out of
** memory error report on failure.
*/
extern MochaBoolean
mocha_InitAtomState(MochaContext *mc);

/*
** Free and clear atom bookkeeping.
*/
extern void
mocha_FreeAtomState(MochaContext *mc);

/*
** Find or create the atom for string.  If we create a new atom, give it the
** type indicated in flags.  Return 0 on failure to allocate memory.
*/
extern MochaAtom *
mocha_Atomize(MochaContext *mc, const char *string, MochaAtomFlags flags);

extern MochaAtomNumber
mocha_IndexAtom(MochaContext *mc, MochaAtom *atom, CodeGenerator *cg);

/*
** Atom reference counting operators.
*/
extern MochaAtom *
mocha_HoldAtom(MochaContext *mc, MochaAtom *atom);

extern MochaAtom *
mocha_DropAtom(MochaContext *mc, MochaAtom *atom);

/*
** Get the atom with index n from map.
*/
extern MochaAtom *
mocha_GetAtom(MochaContext *mc, MochaAtomMap *map, MochaAtomNumber n);

/*
** For all unindexed atoms recorded since the last map was initialized, add a
** mapping from the atom's index to its address.
*/
extern MochaBoolean
mocha_InitAtomMap(MochaContext *mc, MochaAtomMap *map, CodeGenerator *cg);

extern void
mocha_FreeAtomMap(MochaContext *mc, MochaAtomMap *map);

extern void
mocha_DropUnmappedAtoms(MochaContext *mc, CodeGenerator *cg);

NSPR_END_EXTERN_C

#endif /* _mo_atom_h_ */
