#ifndef _mo_scope_h_
#define _mo_scope_h_
/*
** Mocha symbol tables.
**
** Brendan Eich, 6/20/95
*/
#include <stddef.h>
#include "prhash.h"
#include "prmacros.h"
#include "mo_pubtd.h"
#include "mochaapi.h"

NSPR_BEGIN_EXTERN_C

/*
** The symbol type tells whether sym_value is non-null, and if so, what type
** of struct it points at.  All possible types must begin with a MochaRefCount
** member that is bumped when sym_value is set and dropped when a symbol entry
** is being removed.
*/
typedef enum MochaSymbolType {
    SYM_UNDEF,                          /* undefined property */
    SYM_ARGUMENT,                       /* sym->slot is argument stack slot */
    SYM_VARIABLE,                       /* sym->slot is variable stack slot */
    SYM_PROPERTY                        /* sym->sym_value -> MochaProperty */
} MochaSymbolType;

/*
** A Mocha symbol table, scope for short.
*/
struct MochaScope {
    MochaRefCount       nrefs;          /* reference count for sharing */
    MochaObject         *object;        /* object that owns this scope */
    PRHashTable         *table;         /* a scope is based on a hash table */
    MochaSymbol         *list;          /* or a linked list if few entries */
    MochaProperty       *props;         /* linked list of properties */
    MochaProperty       **proptail;     /* pointer to pointer to last prop */
    MochaSlot           freeslot;       /* next free property slot >= 0 */
    MochaSlot           minslot;        /* lowest property slot number */
};

/*
** Mocha property descriptor, extends MochaDatum.
*/
struct MochaProperty {
    MochaDatum          datum;          /* base class state */
    MochaProperty       *next;          /* next property in object props list */
    MochaProperty       **prevp;        /* ptr to previous property's next */
    MochaSymbol         *lastsym;       /* last name defined for this slot */
    MochaSlot           slot;           /* the property's slot number */
    MochaPropertyOp     getter;         /* property getter function */
    MochaPropertyOp     setter;         /* property setter function */
};

/*
** Property list operations.  MochaObject.props is a single-headed,
** doubly-linked list that uses back-pointers to each element's next pointer,
** for constant-time unlink.  A property slot may be named by more than one
** symbol.  The last defined symbol is pointed to by MochaProperty.lastsym,
** for enumeration via 'for (p in o) ...'.
*/
#define PROP_INIT_LINKS(prop)                                                 \
    NSPR_BEGIN_MACRO                                                          \
        (prop)->next = 0;                                                     \
        (prop)->prevp = &(prop)->next;                                        \
    NSPR_END_MACRO

#define PROP_APPEND(scope, prop)                                              \
    NSPR_BEGIN_MACRO                                                          \
        (prop)->next = 0;                                                     \
        (prop)->prevp = (scope)->proptail;                                    \
        *(scope)->proptail = (prop);                                          \
        (scope)->proptail = &(prop)->next;                                    \
    NSPR_END_MACRO

#define PROP_UNLINK(scope, prop)                                              \
    NSPR_BEGIN_MACRO                                                          \
        *(prop)->prevp = (prop)->next;                                        \
        if ((prop)->next)                                                     \
            (prop)->next->prevp = (prop)->prevp;                              \
        else                                                                  \
            (scope)->proptail = (prop)->prevp;                                \
        PROP_INIT_LINKS(prop);                                                \
    NSPR_END_MACRO

/*
** Dynamic scoping support, for with statements.
*/
struct MochaObjectStack {
    MochaObject         *object;        /* innermost "with" statement object */
    MochaObjectStack    *down;          /* outer dynamic scopes */
};

/*
** A Mocha symbol, an extension of PRHashEntry.
*/
struct MochaSymbol {
    PRHashEntry         entry;          /* base class state */
    MochaScope          *scope;         /* back-pointer to containing scope */
    MochaSymbolType     type;           /* see MochaSymbolType, above */
    MochaSlot           slot;           /* property or stack slot number */
    MochaSymbol         *next;          /* next symbol in type-specific list */
};

#define sym_atom(sym)       ((MochaAtom *)(sym)->entry.key)
#define sym_datum(sym)      ((MochaDatum *)(sym)->entry.value)
#define sym_property(sym)   ((MochaProperty *)(sym)->entry.value)

/*
** Initialize and finalize a Mocha scope.
*/
extern MochaScope *
mocha_NewScope(MochaContext *mc, MochaObject *obj);

extern void
mocha_DestroyScope(MochaContext *mc, MochaScope *scope);

extern MochaScope *
mocha_HoldScope(MochaContext *mc, MochaScope *scope);

extern MochaScope *
mocha_DropScope(MochaContext *mc, MochaScope *scope);

#ifdef NOTUSED
extern MochaBoolean
mocha_CopyScope(MochaContext *mc, MochaScope *from, MochaScope *to);
#endif

extern void
mocha_ClearScope(MochaContext *mc, MochaScope *scope);

/*
** mocha_LookupSymbol looks in scope and its prototypes for a symbol named
** by atom.  If flag is MLF_SET, it ensures that the found symbol it returns
** is in scope, not in its prototype (the symbol may have been set in the
** prototype after the scope mutated).  If flag is MLF_GET, the returned
** symbol could be in scope or its prototype, and should therefore not be
** set by assignment.
**
** mocha_SearchScopes searches through mc's dynamic scope and then static scope
** stacks for a symbol named by atom.
**
** Both functions may return false in the MLF_SET case, to indicate a memory
** allocation failure.  Otherwise, they return true with *symp set to null if
** no symbol was found, or to the non-null symbol pointer if found.
*/
typedef enum MochaLookupFlag { MLF_GET, MLF_SET } MochaLookupFlag;

extern MochaBoolean
mocha_LookupSymbol(MochaContext *mc, MochaScope *scope, const MochaAtom *atom,
                   MochaLookupFlag flag, MochaSymbol **symp);

extern MochaBoolean
mocha_SearchScopes(MochaContext *mc, const MochaAtom *atom,
		   MochaLookupFlag flag, MochaPair *pair);

/*
** Define a symbol named by atom, with the given type and value, in scope.
** Returns null if out of memory.
*/
extern MochaSymbol *
mocha_DefineSymbol(MochaContext *mc, MochaScope *scope, MochaAtom *atom,
                   MochaSymbolType type, void *value);

/*
** Remove any symbol named by atom in scope.
*/
extern void
mocha_RemoveSymbol(MochaContext *mc, MochaScope *scope, MochaAtom *atom);

/* XXX begin move me to mo_fun.h */
/*
** Create/destroy ops for MochaFunction.
** XXX comment me
*/
extern MochaFunction *
mocha_NewFunction(MochaContext *mc, MochaNativeCall call, unsigned nargs,
		  MochaObject *parent, MochaAtom *atom);

extern void
mocha_DestroyFunction(MochaContext *mc, MochaFunction *fun);

/*
** Define a SYM_PROPERTY entry in the given scope containing a MOCHA_FUNCTION
** ref to a new MochaFunction with the given name (atom), call, and nargs.
*/
extern MochaFunction *
mocha_DefineFunction(MochaContext *mc, MochaObject *obj, MochaAtom *atom,
                     MochaNativeCall call, unsigned nargs, unsigned flags);
/* XXX end move me to mo_fun.h */

/* XXX begin move me to mo_obj.h */
/*
** Initialize an already-allocated MochaObject.
** XXX comment me
*/
extern MochaBoolean
mocha_InitObject(MochaContext *mc, MochaObject *obj, MochaClass *clazz,
		 void *data, MochaObject *prototype, MochaObject *parent);

extern void
mocha_FreeObject(MochaContext *mc, MochaObject *obj);

/*
** Create a new MochaObject, not defined in any scope.
*/
extern MochaObject *
mocha_NewObject(MochaContext *mc, MochaClass *clazz, void *data,
		MochaObject *prototype, MochaObject *parent);

extern void
mocha_DestroyObject(MochaContext *mc, MochaObject *obj);
/* XXX end move me to mo_obj.h */

/*
** Insert a property named by atom and with the given slot number into scope.
** If atom is null, slot gives property's index as an unnamed array element.
** A property may already be associated with the given name or slot.  Several
** property names may alias the same slot.
*/
extern MochaSymbol *
mocha_SetProperty(MochaContext *mc, MochaScope *scope, MochaAtom *atom,
                  MochaSlot slot, MochaDatum datum);

/*
** Remove the property named by atom from scope.
*/
extern void
mocha_RemoveProperty(MochaContext *mc, MochaScope *scope, MochaAtom *atom);

NSPR_END_EXTERN_C

#endif /* _mo_scope_h_ */
