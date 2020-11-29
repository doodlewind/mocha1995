/*
** Mocha API.
**
** Brendan Eich, 6/21/95
*/
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prarena.h"
#include "prlog.h"
#include "prmem.h"
#include "prprf.h"
#include "mo_atom.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mo_parse.h"
#include "mo_scan.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

uint16 (*mocha_MixTaint)(MochaContext *mc, uint16 accum, uint16 taint);

MochaDatum MOCHA_void  = {0,MOCHA_UNDEF,  MDF_TRACEBITS,MOCHA_TAINT_IDENTITY};
MochaDatum MOCHA_null  = {0,MOCHA_OBJECT, MDF_TRACEBITS,MOCHA_TAINT_IDENTITY};
MochaDatum MOCHA_zero  = {0,MOCHA_NUMBER, MDF_TRACEBITS,MOCHA_TAINT_IDENTITY};
MochaDatum MOCHA_NaN   = {0,MOCHA_NUMBER, MDF_TRACEBITS,MOCHA_TAINT_IDENTITY};
MochaDatum MOCHA_false = {0,MOCHA_BOOLEAN,MDF_TRACEBITS,MOCHA_TAINT_IDENTITY};
MochaDatum MOCHA_true  = {0,MOCHA_BOOLEAN,MDF_TRACEBITS,MOCHA_TAINT_IDENTITY};
MochaDatum MOCHA_empty = {0,MOCHA_STRING, MDF_TRACEBITS,MOCHA_TAINT_IDENTITY};

/*
** Do first things first, at most once.
*/
static void
mocha_StaticInit(MochaContext *mc)
{
    if (MOCHA_true.u.bval)
	return;
    MOCHA_true.u.bval = MOCHA_TRUE;
    MOCHA_empty.u.atom = mocha_Atomize(mc, "", ATOM_HELD | ATOM_STRING);
}

void
MOCHA_HoldRef(MochaContext *mc, MochaDatum *dp)
{
    mocha_HoldRef(mc, dp);
}

void
MOCHA_DropRef(MochaContext *mc, MochaDatum *dp)
{
    mocha_DropRef(mc, dp);
}

void
MOCHA_WeakenRef(MochaContext *mc, MochaDatum *dp)
{
    switch (dp->tag) {
      case MOCHA_ATOM:
      case MOCHA_STRING:
	dp->u.atom->nrefs--;
	break;
      case MOCHA_FUNCTION:
	dp->u.fun->object.nrefs--;
	break;
      case MOCHA_OBJECT:
      case MOCHA_SYMBOL:
	if (dp->u.obj)
	    dp->u.obj->nrefs--;
	break;
      default:;
    }
}

MochaBoolean
MOCHA_ConvertDatum(MochaContext *mc, MochaDatum d, MochaTag tag,
		   MochaDatum *dp)
{
    MochaFunction *fun;
    MochaObject *obj;
    MochaFloat fval;
    MochaBoolean bval;
    MochaAtom *atom;

    switch (tag) {
      case MOCHA_FUNCTION:
	if (!mocha_DatumToFunction(mc, d, &fun))
	    return MOCHA_FALSE;
	MOCHA_INIT_DATUM(mc, dp, MOCHA_FUNCTION, u.fun, fun);
	break;

      case MOCHA_OBJECT:
	if (!mocha_DatumToObject(mc, d, &obj))
	    return MOCHA_FALSE;
	MOCHA_INIT_DATUM(mc, dp, MOCHA_OBJECT, u.obj, obj);
	break;

      case MOCHA_NUMBER:
	if (!mocha_DatumToNumber(mc, d, &fval))
	    return MOCHA_FALSE;
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval, fval);
	break;

      case MOCHA_BOOLEAN:
	if (!mocha_DatumToBoolean(mc, d, &bval))
	    return MOCHA_FALSE;
	MOCHA_INIT_DATUM(mc, dp, MOCHA_BOOLEAN, u.bval, bval);
	break;

      case MOCHA_STRING:
	if (!mocha_DatumToString(mc, d, &atom))
	    return MOCHA_FALSE;
	MOCHA_INIT_DATUM(mc, dp, MOCHA_STRING, u.atom, atom);
	break;

      default:
	if ((unsigned)tag >= (unsigned)MOCHA_NTYPES) {
	    MOCHA_ReportError(mc, "illegal type tag %d", tag);
	} else if (mocha_RawDatumToString(mc, d, &atom)) {
	    MOCHA_ReportError(mc, "can't convert %s to %s",
			      atom_name(atom), mocha_typeStr[tag]);
	    mocha_DropAtom(mc, atom);
	}
	return MOCHA_FALSE;
    }
    MOCHA_WeakenRef(mc, dp);
    return MOCHA_TRUE;
}

MochaBoolean
MOCHA_DatumToFunction(MochaContext *mc, MochaDatum d, MochaFunction **funp)
{
    return mocha_DatumToFunction(mc, d, funp);
}

MochaBoolean
MOCHA_DatumToObject(MochaContext *mc, MochaDatum d, MochaObject **objp)
{
    return mocha_DatumToObject(mc, d, objp);
}

MochaBoolean
MOCHA_DatumToNumber(MochaContext *mc, MochaDatum d, MochaFloat *fvalp)
{
    return mocha_DatumToNumber(mc, d, fvalp);
}

MochaBoolean
MOCHA_DatumToBoolean(MochaContext *mc, MochaDatum d, MochaBoolean *bvalp)
{
    return mocha_DatumToBoolean(mc, d, bvalp);
}

MochaBoolean
MOCHA_DatumToString(MochaContext *mc, MochaDatum d, MochaAtom **atomp)
{
    return mocha_DatumToString(mc, d, atomp);
}

const char *
MOCHA_TypeOfDatum(MochaContext *mc, MochaDatum d)
{
    MochaAtom *atom;

    if (!mocha_TypeOfDatum(mc, d, &atom))
	return 0;
    return atom_name(atom);
}

MochaContext *
MOCHA_NewContext(size_t stackSize)
{
    MochaContext *mc;

    mc = mocha_NewContext(stackSize);
    if (mc) {
	/* Can't initialize trailing union arms, so do this here. */
	mocha_StaticInit(mc);
    }
    return mc;
}

void
MOCHA_DestroyContext(MochaContext *mc)
{
    mocha_DestroyContext(mc);
}

MochaContext *
MOCHA_ContextIterator(MochaContext **iterp)
{
    return mocha_ContextIterator(iterp);
}

/* XXX comment me */
MochaObject *
MOCHA_GetGlobalObject(MochaContext *mc)
{
    return mc->globalObject;
}

/* XXX comment me */
MochaBoolean
MOCHA_SetGlobalObject(MochaContext *mc, MochaObject *obj)
{
    if (obj->parent) {
	MOCHA_ReportError(mc, "illegal global object %s", obj->clazz->name);
	return MOCHA_FALSE;
    }
    mc->globalObject = obj;	/* XXX weak link */
    return mocha_InitStandardLibrary(mc, obj);
}

MochaObject *
MOCHA_GetStaticLink(MochaContext *mc)
{
    return mc->staticLink;
}

void *
MOCHA_malloc(MochaContext *mc, size_t nbytes)
{
    void *p = malloc(nbytes);
    if (!p)
	MOCHA_ReportOutOfMemory(mc);
    return p;
}

char *
MOCHA_strdup(MochaContext *mc, const char *s)
{
    char *p = MOCHA_malloc(mc, strlen(s) + 1);
    if (!p)
	return 0;
    return strcpy(p, s);
}

void
MOCHA_free(MochaContext *mc, void *p)
{
    free(p);
}

MochaBoolean
MOCHA_PropertyStub(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		   MochaDatum *dp)
{
    return MOCHA_TRUE;
}

MochaBoolean
MOCHA_ListPropStub(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_TRUE;
}

MochaBoolean
MOCHA_ResolveStub(MochaContext *mc, MochaObject *obj, const char *name)
{
    return MOCHA_TRUE;
}

MochaBoolean
MOCHA_ConvertStub(MochaContext *mc, MochaObject *obj, MochaTag tag,
		  MochaDatum *dp)
{
    return MOCHA_TRUE;
}

void
MOCHA_FinalizeStub(MochaContext *mc, MochaObject *obj)
{
}

MochaObject *
MOCHA_InitClass(MochaContext *mc, MochaObject *obj, MochaClass *clazz,
		void *data, MochaNativeCall constructor, unsigned nargs,
		MochaPropertySpec *ps, MochaFunctionSpec *fs,
		MochaPropertySpec *static_ps, MochaFunctionSpec *static_fs)
{
    MochaAtom *atom;
    MochaObject *prototype;
    MochaFunction *fun;
    MochaDatum d;

    atom = mocha_Atomize(mc, clazz->name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return 0;

    /* Define the constructor function in a mutable scope for obj. */
    prototype = 0;
    if (!mocha_GetMutableScope(mc, obj))
	goto out;
    fun = mocha_DefineFunction(mc, obj, atom, constructor, nargs, 0);
    if (!fun)
	goto out;

    /* Construct a prototype object for this class. */
    prototype = mocha_NewObject(mc, clazz, data, 0, &fun->object);
    if (!prototype)
	goto out;
    d = MOCHA_void;
    if (!(*constructor)(mc, prototype, 0, 0, &d)) {
	prototype->clazz = &mocha_ObjectClass;
	mocha_DestroyObject(mc, prototype);
	prototype = 0;
	goto out;
    }
    if (d.tag == MOCHA_OBJECT && d.u.obj && d.u.obj != prototype) {
	prototype->clazz = &mocha_ObjectClass;
	mocha_DestroyObject(mc, prototype);
	prototype = d.u.obj;
    }

    /* Bootstrap Function.prototype (XXX see mochalib.c for crucial order). */
    if (!fun->object.prototype && fun->object.clazz == clazz) {
	PR_ASSERT(fun->object.scope->object == &fun->object);
	mocha_DropScope(mc, fun->object.scope);
	fun->object.scope = mocha_HoldScope(mc, prototype->scope);
	fun->object.prototype = MOCHA_HoldObject(mc, prototype);
    }

    /* Add properties and methods to the prototype and the constructor. */
    if ((ps && !MOCHA_SetProperties(mc, prototype, ps)) ||
	(fs && !MOCHA_DefineFunctions(mc, prototype, fs)) ||
	(static_ps && !MOCHA_SetProperties(mc, &fun->object, static_ps)) ||
	(static_fs && !MOCHA_DefineFunctions(mc, &fun->object, static_fs)) ||
	!mocha_SetPrototype(mc, fun, prototype)) {
	mocha_DestroyObject(mc, prototype);
	prototype = 0;
	goto out;
    }

out:
    if (!prototype)
	mocha_RemoveSymbol(mc, obj->scope, atom);
    mocha_DropAtom(mc, atom);
    return prototype;
}

MochaBoolean
MOCHA_InstanceOf(MochaContext *mc, MochaObject *obj, MochaClass *clazz,
		 MochaFunction *fun)
{
    if (obj->clazz != clazz) {
	MOCHA_ReportError(mc, "method %s.%s called on incompatible %s",
			  clazz->name, atom_name(fun->atom), obj->clazz->name);
	return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

MochaObject *
MOCHA_DefineNewObject(MochaContext *mc, MochaObject *obj, const char *name,
		      MochaClass *clazz, void *data,
		      MochaObject *prototype, unsigned flags,
		      MochaPropertySpec *ps, MochaFunctionSpec *fs)
{
    MochaObject *nobj;

    nobj = MOCHA_NewObject(mc, clazz, data, prototype, obj, ps, fs);
    if (!nobj)
	return 0;
    if (!MOCHA_DefineObject(mc, obj, name, nobj, flags)) {
	MOCHA_DestroyObject(mc, nobj);
	return 0;
    }
    return nobj;
}

MochaObject *
MOCHA_NewObject(MochaContext *mc, MochaClass *clazz, void *data,
		MochaObject *prototype, MochaObject *parent,
		MochaPropertySpec *ps, MochaFunctionSpec *fs)
{
    MochaObject *obj;

    obj = mocha_NewObject(mc, clazz, data, prototype, parent);
    if (!obj)
	return 0;
    if ((ps && !MOCHA_SetProperties(mc, obj, ps)) ||
	(fs && !MOCHA_DefineFunctions(mc, obj, fs))) {
	mocha_DestroyObject(mc, obj);
	return 0;
    }
    return obj;
}

MochaBoolean
MOCHA_DefineObject(MochaContext *mc, MochaObject *obj, const char *name,
		   MochaObject *nobj, unsigned flags)
{
    MochaAtom *atom;
    MochaDatum od;
    MochaSymbol *sym;

    if (!mocha_GetMutableScope(mc, obj))
	return MOCHA_FALSE;
    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_FULL_DATUM(mc, &od, MOCHA_OBJECT, flags, MOCHA_TAINT_IDENTITY,
			  u.obj, nobj);
    sym = mocha_SetProperty(mc, obj->scope, atom, obj->scope->minslot-1, od);
    mocha_DropAtom(mc, atom);
    return sym != 0;
}

static MochaSymbol *
SetProperty(MochaContext *mc, MochaObject *obj, const char *name,
	    MochaSlot slot, MochaDatum datum)
{
    MochaAtom *atom;
    MochaSymbol *sym;

    if (!name) {
	atom = 0;
    } else {
	atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
	if (!atom)
	    return MOCHA_FALSE;
    }
    sym = mocha_SetProperty(mc, obj->scope, atom, slot, datum);
    if (atom)
	mocha_DropAtom(mc, atom);
    return sym;
}

MochaBoolean
MOCHA_SetProperties(MochaContext *mc, MochaObject *obj, MochaPropertySpec *ps)
{
    MochaDatum d;
    MochaSymbol *sym;
    MochaProperty *prop;

    if (!mocha_GetMutableScope(mc, obj))
	return MOCHA_FALSE;
    for (; ps->name; ps++) {
	MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_OBJECT,
			      ps->flags, MOCHA_TAINT_IDENTITY,
			      u.obj, 0);
	sym = SetProperty(mc, obj, ps->name, ps->slot, d);
	if (!sym)
	    return MOCHA_FALSE;
	prop = sym_property(sym);
	if (ps->getter)
	    prop->getter = ps->getter;
	if (ps->setter)
	    prop->setter = ps->setter;
    }
    return MOCHA_TRUE;
}

void
MOCHA_DestroyObject(MochaContext *mc, MochaObject *obj)
{
    mocha_DestroyObject(mc, obj);
}

MochaBoolean
MOCHA_SetProperty(MochaContext *mc, MochaObject *obj, const char *name,
		  MochaSlot slot, MochaDatum datum)
{
    if (!mocha_GetMutableScope(mc, obj))
	return MOCHA_FALSE;
    return (SetProperty(mc, obj, name, slot, datum) != 0);
}

void
MOCHA_RemoveProperty(MochaContext *mc, MochaObject *obj, const char *name)
{
    MochaAtom *atom;

    if (!mocha_GetMutableScope(mc, obj))
	return;	/* XXX */
    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return;
    mocha_RemoveProperty(mc, obj->scope, atom);
    mocha_DropAtom(mc, atom);
}

MochaBoolean
MOCHA_GetSlot(MochaContext *mc, MochaObject *obj, MochaSlot slot,
	      MochaDatum *dp)
{
    char buf[20];
    MochaAtom *atom;
    MochaBoolean ok;
    MochaPair pair;

    PR_snprintf(buf, sizeof buf, "%d", slot);
    atom = mocha_Atomize(mc, buf, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return MOCHA_FALSE;
    ok = mocha_LookupSymbol(mc, obj->scope, atom, MLF_GET, &pair.sym);
    if (ok && !pair.sym) {
	ok = mocha_GetMutableScope(mc, obj);
	if (ok) {
	    pair.sym = mocha_SetProperty(mc, obj->scope, atom, slot,
					 MOCHA_null);
	    ok = (pair.sym != 0);
	}
    }
    mocha_DropAtom(mc, atom);
    if (!ok)
	return MOCHA_FALSE;
    pair.obj = obj;
    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_SYMBOL, 0, MOCHA_TAINT_IDENTITY,
			  u.pair, pair);
    return mocha_ResolveValue(mc, dp);
}

/* XXX this does not call OBJ_SET_PROPERTY; should distinguish Init from Set */
MochaBoolean
MOCHA_SetSlot(MochaContext *mc, MochaObject *obj, MochaSlot slot,
	      MochaDatum datum)
{
    if (!mocha_GetMutableScope(mc, obj))
	return MOCHA_FALSE;
    return mocha_SetProperty(mc, obj->scope, 0, slot, datum) != 0;
}

void
MOCHA_RemoveSlot(MochaContext *mc, MochaObject *obj, MochaSlot slot)
{
    char buf[20];

    PR_snprintf(buf, sizeof buf, "%d", slot);
    MOCHA_RemoveProperty(mc, obj, buf);
}

MochaAtom *
MOCHA_Atomize(MochaContext *mc, const char *name)
{
    if (!name)
	return MOCHA_empty.u.atom;
    return mocha_Atomize(mc, name, ATOM_STRING);
}

const char *
MOCHA_GetAtomName(MochaContext *mc, MochaAtom *atom)
{
    if (!atom)
	return 0;
    return atom_name(atom);
}

MochaAtom *
MOCHA_HoldAtom(MochaContext *mc, MochaAtom *atom)
{
    if (!atom)
	return 0;
    return mocha_HoldAtom(mc, atom);
}

MochaAtom *
MOCHA_DropAtom(MochaContext *mc, MochaAtom *atom)
{
    if (!atom)
	return 0;
    return mocha_DropAtom(mc, atom);
}

MochaFunction *
MOCHA_NewFunction(MochaContext *mc, MochaObject *obj, MochaNativeCall call,
		  unsigned nargs)
{
    return mocha_NewFunction(mc, call, nargs, obj, mocha_anonymousAtom);
}

void
MOCHA_DestroyFunction(MochaContext *mc, MochaFunction *fun)
{
    mocha_DestroyFunction(mc, fun);
}

static MochaFunction *
DefineFunction(MochaContext *mc, MochaObject *obj, const char *name,
	       MochaNativeCall call, unsigned nargs, unsigned flags)
{
    MochaAtom *atom;
    MochaFunction *fun;

    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return 0;
    fun = mocha_DefineFunction(mc, obj, atom, call, nargs, flags);
    mocha_DropAtom(mc, atom);
    return fun;
}

MochaFunction *
MOCHA_DefineFunction(MochaContext *mc, MochaObject *obj, const char *name,
		     MochaNativeCall call, unsigned nargs, unsigned flags)
{
    if (!mocha_GetMutableScope(mc, obj))
	return 0;
    return DefineFunction(mc, obj, name, call, nargs, flags);
}

MochaBoolean
MOCHA_DefineFunctions(MochaContext *mc, MochaObject *obj, MochaFunctionSpec *fs)
{
    MochaFunction *fun;
    MochaDatum fd;
    MochaSymbol *sym;

    if (!mocha_GetMutableScope(mc, obj))
	return MOCHA_FALSE;
    for (; fs->name; fs++) {
	if (!fs->fun) {
	    fun = DefineFunction(mc, obj, fs->name, fs->call, fs->nargs,
				 fs->flags);
	    if (!fun)
		return MOCHA_FALSE;
	    fs->fun = (MochaFunction *)MOCHA_HoldObject(mc, &fun->object);
	} else {
	    MOCHA_INIT_FULL_DATUM(mc, &fd, MOCHA_FUNCTION,
				  fs->flags, MOCHA_TAINT_IDENTITY,
				  u.fun, fs->fun);
	    sym = SetProperty(mc, obj, fs->name, obj->scope->minslot-1, fd);
	    if (!sym)
		return MOCHA_FALSE;
	}
    }
    return MOCHA_TRUE;
}

MochaObject *
MOCHA_HoldObject(MochaContext *mc, MochaObject *obj)
{
    if (!obj)
	return 0;
    PR_ASSERT(obj->nrefs == MOCHA_FINALIZING || obj->nrefs >= 0);
    if (obj->nrefs != MOCHA_FINALIZING)
	obj->nrefs++;
    return obj;
}

MochaObject *
MOCHA_DropObject(MochaContext *mc, MochaObject *obj)
{
    if (!obj)
	return 0;
    PR_ASSERT(obj->nrefs == MOCHA_FINALIZING || obj->nrefs > 0);
    if (obj->nrefs != MOCHA_FINALIZING && --obj->nrefs == 0) {
	mocha_DestroyObject(mc, obj);
	return 0;
    }
    if (obj->parent && obj->parent->nrefs == MOCHA_FINALIZING)
	obj->parent = 0;
    return obj;
}

MochaBoolean
MOCHA_LookupName(MochaContext *mc, MochaObject *obj, const char *name,
		 MochaDatum *dp)
{
    MochaAtom *atom;
    MochaPair pair;
    MochaBoolean ok;

    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return MOCHA_FALSE;
    ok = mocha_LookupSymbol(mc, obj->scope, atom, MLF_GET, &pair.sym);
    mocha_DropAtom(mc, atom);
    if (!ok)
	return MOCHA_FALSE;
    if (!pair.sym || pair.sym->type == SYM_UNDEF) {
	*dp = MOCHA_void;
	return MOCHA_TRUE;
    }
    pair.obj = obj;
    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_SYMBOL, 0, MOCHA_TAINT_IDENTITY,
			  u.pair, pair);
    return mocha_ResolveValue(mc, dp);
}

MochaBoolean
MOCHA_ResolveName(MochaContext *mc, MochaObject *obj, const char *name,
		  MochaDatum *dp)
{
    MochaAtom *atom;
    MochaObject *oldslink;
    MochaBoolean ok;

    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_FULL_DATUM(mc, dp, MOCHA_ATOM, 0, MOCHA_TAINT_IDENTITY,
			  u.atom, atom);
    oldslink = mc->staticLink;
    mc->staticLink = obj;
    ok = mocha_ResolveSymbol(mc, dp, MLF_GET);
    mc->staticLink = oldslink;
    mocha_DropAtom(mc, atom);
    if (!ok)
	return MOCHA_FALSE;
    if (dp->tag != MOCHA_SYMBOL) {
	*dp = MOCHA_void;
	return MOCHA_TRUE;
    }
    return mocha_ResolveValue(mc, dp);
}

void
MOCHA_UndefineName(MochaContext *mc, MochaObject *obj, const char *name)
{
    MochaAtom *atom;

    if (!mocha_GetMutableScope(mc, obj))
	return;
    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return;
    mocha_RemoveSymbol(mc, obj->scope, atom);
    mocha_DropAtom(mc, atom);
}

void
MOCHA_ClearScope(MochaContext *mc, MochaObject *obj)
{
    mocha_ClearScope(mc, obj->scope);
}

static MochaScript *
CompileTokenStream(MochaContext *mc, MochaObject *obj, MochaTokenStream *ts,
		   void *tempMark)
{
    void *codeMark;
    CodeGenerator cg;
    unsigned lineno;
    MochaScript *script;

    codeMark = PR_ARENA_MARK(&mc->codePool);
    if (!mocha_InitCodeGenerator(mc, &cg, &mc->codePool))
	return 0;
    lineno = ts->lineno;
    if (mocha_Parse(mc, obj, ts, &cg))
	script = mocha_NewScript(mc, &cg, ts->filename, lineno);
    else
	script = 0;
    if (!mocha_CloseTokenStream(ts) && script) {
	mocha_DestroyScript(mc, script);
	script = 0;
    }
    PR_ARENA_RELEASE(&mc->codePool, codeMark);
    PR_ARENA_RELEASE(&mc->tempPool, tempMark);
    return script;
}

MochaScript *
MOCHA_CompileBuffer(MochaContext *mc, MochaObject *obj,
		    const char *base, size_t length,
		    const char *filename, unsigned lineno)
{
    void *mark;
    MochaTokenStream *ts;

    mark = PR_ARENA_MARK(&mc->tempPool);
    ts = mocha_NewTokenStream(mc, base, length, filename, lineno);
    if (!ts)
	return MOCHA_FALSE;
    return CompileTokenStream(mc, obj, ts, mark);
}

#ifdef MOCHAFILE
MochaScript *
MOCHA_CompileFile(MochaContext *mc, MochaObject *obj, const char *filename)
{
    void *mark;
    MochaTokenStream *ts;

    mark = PR_ARENA_MARK(&mc->tempPool);
    if (filename && strcmp(filename, "-") != 0) {
	ts = mocha_NewFileTokenStream(mc, filename);
	if (!ts) return MOCHA_FALSE;
    } else {
	ts = mocha_NewBufferTokenStream(mc, 0, 0);
	if (!ts) return MOCHA_FALSE;
	ts->file = stdin;
    }
    return CompileTokenStream(mc, obj, ts, mark);
}
#endif

MochaBoolean
MOCHA_CompileMethod(MochaContext *mc, MochaObject *obj,
		    const char *name, unsigned nargs,
		    const char *base, size_t length,
		    const char *filename, unsigned lineno)
{
    void *mark;
    MochaTokenStream *ts;
    MochaAtom *atom;
    MochaFunction *fun;
    MochaBoolean ok;

    mark = PR_ARENA_MARK(&mc->tempPool);
    ts = mocha_NewTokenStream(mc, base, length, filename, lineno);
    if (!ts)
	return MOCHA_FALSE;
    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    ok = (atom != 0);
    if (ok) {
	if (!mocha_GetMutableScope(mc, obj)) {
	    ok = MOCHA_FALSE;
	} else {
	    fun = mocha_DefineFunction(mc, obj, atom, 0, nargs, 0);
	    if (!fun)
		ok = MOCHA_FALSE;
	}
	mocha_DropAtom(mc, atom);
	if (ok) {
	    ok = mocha_ParseFunctionBody(mc, ts, fun);
	    if (!ok)
		mocha_RemoveSymbol(mc, obj->scope, atom);
	}
    }
    PR_ARENA_RELEASE(&mc->tempPool, mark);
    return ok;
}

MochaBoolean
MOCHA_DecompileScript(MochaContext *mc, MochaScript *script, const char *name,
		      unsigned indent, char **sp)
{
    MochaPrinter *mp;
    MochaBoolean ok;

    mp = mocha_NewPrinter(mc, name, indent);
    if (!mp)
	return MOCHA_FALSE;
    ok = mocha_DecompileScript(script, mp);
    if (ok)
	ok = mocha_GetPrinterOutput(mp, sp);
    mocha_DestroyPrinter(mp);
    return ok;
}

MochaBoolean
MOCHA_DecompileFunctionBody(MochaContext *mc, MochaFunction *fun,
			    unsigned indent, char **sp)
{
    return MOCHA_DecompileScript(mc, fun->script, atom_name(fun->atom),
				 indent, sp);
}

MochaBoolean
MOCHA_ExecuteScript(MochaContext *mc, MochaObject *obj, MochaScript *script,
		    MochaDatum *result)
{
    return mocha_Interpret(mc, obj, script, result);
}

void
MOCHA_DestroyScript(MochaContext *mc, MochaScript *script)
{
    mocha_DestroyScript(mc, script);
}

MochaBoolean
MOCHA_EvaluateBuffer(MochaContext *mc, MochaObject *obj,
		     const char *base, size_t length,
		     const char *filename, unsigned lineno,
		     MochaDatum *result)
{
    MochaScript *script;
    MochaBoolean ok;

    script = MOCHA_CompileBuffer(mc, obj, base, length, filename, lineno);
    if (!script)
        return MOCHA_FALSE;
    ok = MOCHA_ExecuteScript(mc, obj, script, result);
    MOCHA_DestroyScript(mc, script);
    return ok;
}

MochaBoolean
MOCHA_CallMethod(MochaContext *mc, MochaObject *obj, const char *name,
		 unsigned argc, MochaDatum *argv, MochaDatum *result)
{
    MochaAtom *atom;
    MochaPair pair;
    MochaDatum fd;
    MochaBoolean ok;

    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return MOCHA_FALSE;
    if (!mocha_LookupSymbol(mc, obj->scope, atom, MLF_GET, &pair.sym))
	return MOCHA_FALSE;
    if (!pair.sym) {
	*result = MOCHA_void;
	return MOCHA_TRUE;
    }
    pair.obj = obj;
    MOCHA_INIT_FULL_DATUM(mc, &fd, MOCHA_SYMBOL, 0, MOCHA_TAINT_IDENTITY,
			  u.pair, pair);
    ok = mocha_Call(mc, fd, argc, argv, result);
    mocha_DropAtom(mc, atom);
    return ok;
}

MochaBranchCallback
MOCHA_SetBranchCallback(MochaContext *mc, MochaBranchCallback cb)
{
    MochaBranchCallback oldcb;

    oldcb = mc->branchCallback;
    mc->branchCallback = cb;
    return oldcb;
}

MochaBoolean
MOCHA_IsRunning(MochaContext *mc)
{
    return mc->script != 0;
}

void
MOCHA_ReportError(MochaContext *mc, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    mocha_ReportErrorVA(mc, format, ap);
    va_end(ap);
}

void
MOCHA_ReportOutOfMemory(MochaContext *mc)
{
    MOCHA_ReportError(mc, "out of memory");
}

MochaErrorReporter
MOCHA_SetErrorReporter(MochaContext *mc, MochaErrorReporter er)
{
    MochaErrorReporter older;

    older = mc->errorReporter;
    mc->errorReporter = er;
    return older;
}

void
MOCHA_SetTaintCallbacks(MochaTaintCounter hold, MochaTaintCounter drop,
                        MochaTaintMixer mix)
{
    mocha_HoldTaint = hold;
    mocha_DropTaint = drop;
    mocha_MixTaint = mix;
}

MochaTaintInfo *
MOCHA_GetTaintInfo(MochaContext *mc)
{
    return mc->taintInfo;
}

void
MOCHA_SetTaintInfo(MochaContext *mc, MochaTaintInfo *info)
{
    if (!info)
	mocha_InitTaintInfo(mc);
    else
	mc->taintInfo = info;
}

MochaBoolean
MOCHA_SetPropertyTaint(MochaContext *mc, MochaObject *obj, const char *name,
		       uint16 taint)
{
    MochaAtom *atom;
    MochaBoolean ok;
    MochaSymbol *sym;

    atom = mocha_Atomize(mc, name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return MOCHA_FALSE;
    ok = mocha_LookupSymbol(mc, obj->scope, atom, MLF_GET, &sym);
    mocha_DropAtom(mc, atom);
    if (!ok)
	return MOCHA_FALSE;
    if (sym && sym->type == SYM_PROPERTY)
	sym_property(sym)->datum.taint = taint;
    return MOCHA_TRUE;
}

void
MOCHA_SetCharFilter(MochaContext *mc, MochaCharFilter filter, void *arg)
{
    mc->charFilter = filter;
    mc->charFilterArg = arg;
}
