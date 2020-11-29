/*
** Mocha symbol tables.
**
** Brendan Eich, 6/20/95
*/
#include <memory.h>
#include <stdio.h>
#include <string.h>
#include "prhash.h"
#include "prlog.h"
#include "prmem.h"
#include "prprf.h"
#include "mo_atom.h"
#include "mo_cntxt.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochaapi.h"

/*
** MochaScope hash allocator ops.
*/
PR_STATIC_CALLBACK(void *)
AllocScopeSpace(void *pool, size_t size)
{
    return MOCHA_malloc(pool, size);
}

PR_STATIC_CALLBACK(void)
FreeScopeSpace(void *pool, void *item)
{
    MOCHA_free(pool, item);
}

PR_STATIC_CALLBACK(PRHashEntry *)
AllocSymbol(void *pool)
{
    MochaSymbol *sym;

    sym = MOCHA_malloc(pool, sizeof *sym);
    if (!sym)
	return 0;
    return &sym->entry;
}

PR_STATIC_CALLBACK(void)
FreeSymbol(void *pool, PRHashEntry *he, int flag)
{
    MochaContext *mc;
    MochaSymbol *sym, *lastsym, **sp;
    MochaDatum *vp;
    MochaProperty *prop;
    MochaScope *scope;
    MochaSlot slot;

    mc = pool;
    sym = (MochaSymbol *)he;
    vp = sym->entry.value;

    /* Robustify reference counting by using a signed type and <= 0. */
    if (vp) {
	PR_ASSERT(vp->nrefs > 0);
	if (vp->nrefs <= 0) return;
	if (--vp->nrefs == 0) {
	    switch (sym->type) {
	      case SYM_VARIABLE:
		mocha_DropRef(mc, vp);
		MOCHA_free(mc, vp);
		break;

	      case SYM_PROPERTY:
		prop = (MochaProperty *)vp;
		scope = sym->scope;
		slot = prop->slot;
		mocha_DropRef(mc, &prop->datum);
		PROP_UNLINK(scope, prop);
		MOCHA_free(mc, prop);

		/* Depending on slot's sign, reset freeslot or minslot. */
		if (slot >= 0) {
		    if (slot + 1 == scope->freeslot)
			scope->freeslot = slot;
		} else {
		    if (slot == scope->minslot)
			scope->minslot = slot + 1;
		}
		break;

	      default:;
	    }
	    vp = 0;
	}
	sym->entry.value = 0;
    }

    if (flag == HT_FREE_ENTRY) {
	mocha_DropAtom(mc, sym_atom(sym));
	if (vp && sym->type == SYM_PROPERTY) {
	    prop = (MochaProperty *)vp;
	    lastsym = 0;
	    for (sp = &prop->lastsym; *sp; sp = &(*sp)->next) {
		if (*sp == sym) {
		    *sp = sym->next;
		    if (!*sp) break;
		}
		lastsym = *sp;
	    }
	    prop->lastsym = lastsym;
	}
	MOCHA_free(mc, he);
    }
}

static PRHashAllocOps scopeHashAllocOps = {
    AllocScopeSpace, FreeScopeSpace,
    AllocSymbol, FreeSymbol
};

PR_STATIC_CALLBACK(PRHashNumber)
HashAtom(const void *key)
{
    const MochaAtom *atom = key;

    return atom->number;
}

PR_STATIC_CALLBACK(int)
ComparePointers(const void *ptr1, const void *ptr2)
{
    return ptr1 == ptr2;
}

MochaScope *
mocha_NewScope(MochaContext *mc, MochaObject *obj)
{
    MochaScope *scope;

    scope = PR_NEW(MochaScope);
    if (!scope) {
	MOCHA_ReportOutOfMemory(mc);
	return 0;
    }
    scope->nrefs = 0;
    scope->object = obj;
    scope->table = 0;
    scope->list = 0;
    scope->freeslot = scope->minslot = 0;
    scope->props = 0;
    scope->proptail = &scope->props;
    return scope;
}

void
mocha_DestroyScope(MochaContext *mc, MochaScope *scope)
{
    mocha_ClearScope(mc, scope);
    PR_DELETE(scope);
}

MochaScope *
mocha_HoldScope(MochaContext *mc, MochaScope *scope)
{
    PR_ASSERT(scope->nrefs >= 0);
    scope->nrefs++;
    return scope;
}

MochaScope *
mocha_DropScope(MochaContext *mc, MochaScope *scope)
{
    PR_ASSERT(scope->nrefs > 0);
    if (--scope->nrefs == 0) {
	mocha_DestroyScope(mc, scope);
	return 0;
    }
    return scope;
}

static MochaSymbol *
CopySymbol(MochaContext *mc, MochaSymbol *sym, MochaScope *to)
{
    MochaProperty *prop, *copy;

    if (sym->type == SYM_PROPERTY) {
	/* XXX avoid recursion through SetProperty/RawLookupSymbol */
	(void) mocha_DefineSymbol(mc, to, sym_atom(sym), SYM_UNDEF, 0);

	/* XXX kludge around incomplete parameterization */
	prop = sym_property(sym);
	sym = mocha_SetProperty(mc, to, sym_atom(sym), prop->slot, prop->datum);
	if (sym) {
	    copy = sym_property(sym);
	    copy->getter = prop->getter;
	    copy->setter = prop->setter;
	}
    } else {
	sym = mocha_DefineSymbol(mc, to, sym_atom(sym), sym->type,
				 sym->entry.value);
    }
    return sym;
}

#ifdef NOTUSED
typedef struct CopyArgs {
    MochaContext *context;
    MochaScope   *scope;
    MochaBoolean status;
} CopyArgs;

PR_STATIC_CALLBACK(int)
CopyHashEntry(PRHashEntry *he, int i, void *arg)
{
    MochaSymbol *sym = (MochaSymbol *)he;
    CopyArgs *ca = arg;
    MochaContext *mc = ca->context;
    MochaScope *to = ca->scope;

    if (!CopySymbol(mc, sym, to)) {
	ca->status = MOCHA_FALSE;
	return HT_ENUMERATE_STOP;
    }
    return HT_ENUMERATE_NEXT;
}

MochaBoolean
mocha_CopyScope(MochaContext *mc, MochaScope *from, MochaScope *to)
{
    CopyArgs ca;
    MochaSymbol *sym;

    ca.context = mc;
    ca.scope = to;
    ca.status = MOCHA_TRUE;
    if (from->table) {
	PR_HashTableEnumerateEntries(from->table, CopyHashEntry, &ca);
    } else {
	for (sym = from->list; sym; sym = (MochaSymbol *)sym->entry.next) {
	    if (CopyHashEntry(&sym->entry, 0, &ca) == HT_ENUMERATE_STOP)
		break;
	}
    }
    return ca.status;
}
#endif

void
mocha_ClearScope(MochaContext *mc, MochaScope *scope)
{
    MochaSymbol *sym, **sp;

    if (scope->table) {
	scope->table->allocPool = mc;
	PR_HashTableDestroy(scope->table);
	scope->table = 0;
    } else {
	sp = &scope->list;
	while ((sym = *sp) != 0) {
	    *sp = (MochaSymbol *)sym->entry.next;
	    FreeSymbol(mc, &sym->entry, HT_FREE_ENTRY);
	}
    }
}

MochaBoolean
RawLookupSymbol(MochaContext *mc, MochaScope *scope, PRHashNumber hash,
		const MochaAtom *atom, MochaLookupFlag flag,
		MochaSymbol **symp)
{
    MochaObject *obj;
    MochaScope *first;
    PRHashEntry **hep;
    MochaSymbol *sym, **sp;

    first = scope;
    obj = scope->object;
    for (;;) {
	if (scope->table) {
	    hep = PR_HashTableRawLookup(scope->table, hash, atom);
	    sym = (MochaSymbol *) *hep;
	    if (sym)
		goto out;
	} else {
	    for (sp = &scope->list; (sym = *sp) != 0;
		 sp = (MochaSymbol **)&sym->entry.next) {
		if (sym_atom(sym) == atom) {
		    /* Move sym to the front for shorter searches. */
		    *sp = (MochaSymbol *)sym->entry.next;
		    sym->entry.next = (PRHashEntry *)scope->list;
		    scope->list = sym;
		    goto out;
		}
	    }
	}
	obj = scope->object->prototype;
	if (!obj || obj->scope == scope)
	    break;
	scope = obj->scope;
    }

out:
    if (flag == MLF_SET && sym && sym->scope != first) {
	sym = CopySymbol(mc, sym, first);
	if (!sym)
	    return MOCHA_FALSE;
    }
    *symp = sym;
    return MOCHA_TRUE;
}

MochaBoolean
mocha_LookupSymbol(MochaContext *mc, MochaScope *scope, const MochaAtom *atom,
                   MochaLookupFlag flag, MochaSymbol **symp)
{
    PRHashNumber hash;

    hash = HashAtom(atom);
    return RawLookupSymbol(mc, scope, hash, atom, flag, symp);
}

MochaBoolean
mocha_SearchScopes(MochaContext *mc, const MochaAtom *atom,
		   MochaLookupFlag flag, MochaPair *pair)
{
    PRHashNumber hash;
    MochaObjectStack *stack;
    MochaScope *scope;
    MochaObject *slink;

    /* Try dynamic scopes nested by "with" statements at run time. */
    hash = HashAtom(atom);
    for (stack = mc->objectStack; stack; stack = stack->down) {
	scope = stack->object->scope;
	if (!RawLookupSymbol(mc, scope, hash, atom, flag, &pair->sym))
	    return MOCHA_FALSE;
	if (pair->sym) {
	    pair->obj = stack->object;
	    return MOCHA_TRUE;
	}
    }

    /* No luck -- try statically linked scopes. */
    for (slink = mc->staticLink; slink; slink = slink->parent) {
	scope = slink->scope;
	if (!RawLookupSymbol(mc, scope, hash, atom, flag, &pair->sym))
	    return MOCHA_FALSE;
	if (pair->sym) {
	    pair->obj = slink;
	    return MOCHA_TRUE;
	}
    }
    pair->obj = 0;
    pair->sym = 0;
    return MOCHA_TRUE;
}

#define HASH_THRESHOLD	10

MochaSymbol *
mocha_DefineSymbol(MochaContext *mc, MochaScope *scope, MochaAtom *atom,
		   MochaSymbolType type, void *value)
{
    MochaSlot nsyms;
    MochaSymbol *sym, *next;
    PRHashEntry **hep;

    if (!scope->table) {
	for (nsyms = 0, sym = scope->list; sym;
	     sym = (MochaSymbol *)sym->entry.next) {
	    if (sym_atom(sym) == atom)
		break;
	    nsyms++;
	}
	if (nsyms >= HASH_THRESHOLD) {
	    scope->table = PR_NewHashTable(nsyms, HashAtom,
					   ComparePointers, ComparePointers,
					   &scopeHashAllocOps, mc);
	    if (scope->table) {
		for (sym = scope->list; sym; sym = next) {
		    next = (MochaSymbol *)sym->entry.next;
		    sym->entry.keyHash = HashAtom(sym->entry.key);
		    sym->entry.next = 0;
		    hep = PR_HashTableRawLookup(scope->table,
						sym->entry.keyHash,
						sym->entry.key);
		    *hep = &sym->entry;
		}
		scope->list = 0;
	    }
	}
    }

    if (scope->table) {
	scope->table->allocPool = mc;
	sym = (MochaSymbol *)PR_HashTableAdd(scope->table, atom, value);
	if (!sym)
	    return 0;
	mocha_HoldAtom(mc, atom);
    } else {
	if (sym) {
	    FreeSymbol(mc, &sym->entry, HT_FREE_VALUE);
	} else {
	    sym = (MochaSymbol *)AllocSymbol(mc);
	    if (!sym)
		return 0;
	    /* Don't set sym->entry.keyHash until we know we need it. */
	    sym->entry.key = mocha_HoldAtom(mc, atom);
	    sym->entry.next = (PRHashEntry *)scope->list;
	    scope->list = sym;
	}
	sym->entry.value = value;
    }

    sym->scope = scope;
    sym->type = type;
    sym->slot = 0;
    sym->next = 0;

    if (value)
	(*(MochaRefCount *)value)++;
    return sym;
}

void
mocha_RemoveSymbol(MochaContext *mc, MochaScope *scope, MochaAtom *atom)
{
    MochaSymbol **sp, *sym;

    if (scope->table) {
	scope->table->allocPool = mc;
	PR_HashTableRemove(scope->table, atom);
    } else {
	for (sp = &scope->list; (sym = *sp) != 0;
	     sp = (MochaSymbol **)&sym->entry.next) {
	    if (sym_atom(sym) == atom) {
		*sp = (MochaSymbol *)sym->entry.next;
		FreeSymbol(mc, &sym->entry, HT_FREE_ENTRY);
		return;
	    }
	}
    }
}

MochaSymbol *
mocha_SetProperty(MochaContext *mc, MochaScope *scope, MochaAtom *atom,
		  MochaSlot slot, MochaDatum datum)
{
    MochaObject *obj;
    MochaAtom *slotAtom;
    char buf[16];
    PRHashNumber hash;
    MochaSymbol *sym, *sym2;
    MochaDatum oldDatum;
    MochaProperty *prop;

    obj = scope->object;
    if (scope->table) scope->table->allocPool = mc;

    if (slot < 0) {
	/* Negative slots have no index atom, so bail if name atom is null. */
	PR_ASSERT(atom);
	if (!atom) return 0;
	slotAtom = atom;
    } else {
	/* Create canonical index name. */
	PR_snprintf(buf, sizeof buf, "%ld", (long)slot);
	slotAtom = mocha_Atomize(mc, buf, ATOM_NUMBER);
	if (!slotAtom)
	    return 0;
	slotAtom->fval = slot;
    }

    /* Look it up in scope to find a pre-existing slot datum. */
    hash = HashAtom(slotAtom);
    if (!RawLookupSymbol(mc, scope, hash, slotAtom, MLF_SET, &sym))
	return 0;
    if (sym && sym->type == SYM_PROPERTY) {
	sym->slot = slot;
	prop = sym_property(sym);
	PR_ASSERT(prop);
	oldDatum = prop->datum;
	prop->datum = datum;
	prop->datum.nrefs = oldDatum.nrefs;
	mocha_HoldRef(mc, &prop->datum);
	mocha_DropRef(mc, &oldDatum);
    } else {
	/* No such slot -- allocate a new property for this slot. */
	prop = MOCHA_malloc(mc, sizeof *prop);
	if (!prop)
	    return 0;
	prop->datum = datum;
	prop->datum.nrefs = 0;
	mocha_HoldRef(mc, &prop->datum);
	prop->slot = slot;
	prop->getter = obj->clazz->getProperty;
	prop->setter = obj->clazz->setProperty;

	/* Install a symbol for the new property if there isn't one already. */
	if (sym) {
	    FreeSymbol(mc, &sym->entry, HT_FREE_VALUE);
	    sym->type = SYM_PROPERTY;
	    sym->entry.value = prop;
	    prop->datum.nrefs = 1;
	} else {
	    sym = mocha_DefineSymbol(mc, scope, slotAtom, SYM_PROPERTY, prop);
	    if (!sym) {
		MOCHA_free(mc, prop);
		return 0;
	    }
	}
	sym->slot = slot;
	sym->next = 0;
	prop->lastsym = sym;

	/* Append the new property at the end of obj's property list. */
	PROP_APPEND(scope, prop);
    }

    /* Now that we have slot's property, define a symbol named atom for it. */
    if (atom && atom != slotAtom) {
	sym = mocha_DefineSymbol(mc, scope, atom, SYM_PROPERTY, prop);
	if (!sym)
	    return 0;	/* XXX undo above side effects? */
	sym->slot = slot;
	for (sym2 = prop->lastsym; sym2 != sym; sym2 = sym2->next) {
	    if (!sym2) {
		sym->next = prop->lastsym;
		prop->lastsym = sym;
		break;
	    }
	}
    }

    /* Now that we've succeeded, update freeslot and minslot. */
    if (slot >= scope->freeslot)
	scope->freeslot = slot + 1;
    if (slot < scope->minslot)
	scope->minslot = slot;
    return sym;
}

void
mocha_RemoveProperty(MochaContext *mc, MochaScope *scope, MochaAtom *atom)
{
    /* XXX properties should be keyed by datum, not atom */
    mocha_RemoveSymbol(mc, scope, atom);
}
