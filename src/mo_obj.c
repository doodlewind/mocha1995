/*
** Mocha user object operations.
**
** Brendan Eich, 11/15/95
*/
#include <string.h>
#include "prlog.h"
#include "prprf.h"
#include "alloca.h"
#include "mo_atom.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

MochaBoolean
mocha_DatumToObject(MochaContext *mc, MochaDatum d, MochaObject **objp)
{
    MochaDatum aval;
    MochaObject *obj;
    MochaAtom *atom;

    aval = d;
    if (!mocha_ResolveValue(mc, &d))
	return MOCHA_FALSE;
    switch (d.tag) {
      case MOCHA_UNDEF:
	obj = 0;	/* XXX backwards compat for undef property tests */
	break;
      case MOCHA_FUNCTION:
	obj = mocha_FunctionToObject(mc, d.u.fun);
	break;
      case MOCHA_NUMBER:
	obj = mocha_NumberToObject(mc, d.u.fval);
	break;
      case MOCHA_BOOLEAN:
	obj = mocha_BooleanToObject(mc, d.u.bval);
	break;
      case MOCHA_STRING:
	obj = mocha_StringToObject(mc, d.u.atom);
	break;
      case MOCHA_OBJECT:
	obj = d.u.obj;
	if (!obj)
	    break;
	if (!OBJ_CONVERT(mc, obj, MOCHA_OBJECT, &d))
	    return MOCHA_FALSE;
	if (d.tag == MOCHA_OBJECT) {
	    obj = d.u.obj;
	    MOCHA_MIX_TAINT(mc, mc->taintInfo->accum, d.taint);
	    break;
	}
	/* FALL THROUGH */
      default:
	if (mocha_RawDatumToString(mc, aval, &atom)) {
	    MOCHA_ReportError(mc, "%s is not an object", atom_name(atom));
	    mocha_DropAtom(mc, atom);
	}
	return MOCHA_FALSE;
    }
    *objp = MOCHA_HoldObject(mc, obj);
    return MOCHA_TRUE;
}

MochaBoolean
mocha_RawObjectToString(MochaContext *mc, MochaObject *obj, MochaAtom **atomp)
{
    const char *name;
    char *str;
    size_t size;
    MochaAtom *atom;
    
    name = obj->clazz->name;
    size = strlen(name) + 10;
    str = (char *)alloca(size);
    PR_snprintf(str, size, "[object %s]", name);
    atom = mocha_Atomize(mc, str, ATOM_HELD | ATOM_STRING);
    if (!atom)
	return MOCHA_FALSE;
    *atomp = atom;
    return MOCHA_TRUE;
}

MochaBoolean
mocha_ObjectToString(MochaContext *mc, MochaObject *obj, MochaAtom **atomp)
{
    MochaDatum rval;

    if (!obj) {
	*atomp = mocha_HoldAtom(mc, mocha_nullAtom);
	return MOCHA_TRUE;
    }

    rval = MOCHA_void;
    if (!OBJ_CONVERT(mc, obj, MOCHA_STRING, &rval))
	return MOCHA_FALSE;
    if (rval.tag == MOCHA_STRING) {
	*atomp = mocha_HoldAtom(mc, rval.u.atom);
	return MOCHA_TRUE;
    }

    /* Try the toString method, if it's defined. */
    if (!mocha_TryMethod(mc, obj, mocha_toStringAtom, 0, 0, &rval))
	return MOCHA_FALSE;
    if (rval.tag != MOCHA_STRING) {
	/* Try valueOf method, see if that returns a string. */
	mocha_DropRef(mc, &rval);
	if (!mocha_TryMethod(mc, obj, mocha_valueOfAtom, 0, 0, &rval))
	    return MOCHA_FALSE;
    }
    if (rval.tag == MOCHA_STRING) {
	/* XXX don't steal rval's atom ref -- it may hold taint. */
	*atomp = mocha_HoldAtom(mc, rval.u.atom);
	MOCHA_MIX_TAINT(mc, mc->taintInfo->accum, rval.taint);
	mocha_DropRef(mc, &rval);
	return MOCHA_TRUE;
    }

    mocha_DropRef(mc, &rval);
    return mocha_RawObjectToString(mc, obj, atomp);
}

MochaBoolean
mocha_TryMethod(MochaContext *mc, MochaObject *obj, MochaAtom *atom,
		unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaPair pair;
    MochaDatum fd;
    MochaErrorReporter older;

    if (!mocha_LookupSymbol(mc, obj->scope, atom, MLF_GET, &pair.sym))
	return MOCHA_FALSE;
    if (!pair.sym) {
	*rval = MOCHA_void;
	return MOCHA_TRUE;
    }
    pair.obj = obj;
    MOCHA_INIT_FULL_DATUM(mc, &fd, MOCHA_SYMBOL, 0, MOCHA_TAINT_IDENTITY,
			  u.pair, pair);
    older = MOCHA_SetErrorReporter(mc, 0);
    if (!mocha_Call(mc, fd, argc, argv, rval))
	*rval = MOCHA_void;
    MOCHA_SetErrorReporter(mc, older);
    return MOCHA_TRUE;
}

MochaClass mocha_ObjectClass = {
    "Object",
    MOCHA_PropertyStub, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub, MOCHA_ConvertStub, MOCHA_FinalizeStub
};

static MochaBoolean
obj_to_string(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    if (!mocha_RawObjectToString(mc, obj, &atom))
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    MOCHA_WeakenRef(mc, rval);
    return MOCHA_TRUE;
}

static MochaBoolean
obj_value_of(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, obj);
    return MOCHA_TRUE;
}

static MochaBoolean
obj_eval(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaBoolean no_parent;
    MochaAtom *atom;
    char *filename;
    unsigned lineno;
    MochaBoolean ok;

    if (argv[0].tag != MOCHA_STRING) {
	*rval = argv[0];
	return MOCHA_TRUE;
    }
    no_parent = (obj->parent == 0 && mc->staticLink != obj);
    if (no_parent)
	obj->parent = mc->staticLink;
    atom = argv[0].u.atom;
    filename = mc->script->filename;
    lineno = mocha_PCtoLineNumber(mc->script, mc->pc);
    ok = MOCHA_EvaluateBuffer(mc, obj, atom_name(atom), atom->length,
			      filename, lineno, rval);
    if (ok)
	MOCHA_WeakenRef(mc, rval);
    if (no_parent)
	obj->parent = 0;
    return ok;
}

static MochaFunctionSpec object_methods[] = {
    {"toString",	obj_to_string,		0},
    {"valueOf",		obj_value_of,		0},
    {"eval",            obj_eval,               1},
    {0}
};

static MochaBoolean
Object(MochaContext *mc, MochaObject *obj,
       unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    if (argc != 0) {
	if (!mocha_DatumToObject(mc, argv[0], &obj))
	    return MOCHA_FALSE;
	if (!obj)
	    return MOCHA_TRUE;
	obj->nrefs--;
    }
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, obj);
    return MOCHA_TRUE;
}

MochaObject *
mocha_InitObjectClass(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_InitClass(mc, obj, &mocha_ObjectClass, 0, Object, 0,
			   0, object_methods, 0, 0);
}

MochaBoolean
mocha_GetPrototype(MochaContext *mc, MochaClass *clazz, MochaObject **objp)
{
    MochaAtom *atom;
    MochaObject *prototype;
    MochaSymbol *sym;
    MochaBoolean ok;
    MochaFunction *fun;
    MochaDatum *dp;

    atom = mocha_Atomize(mc, clazz->name, ATOM_HELD | ATOM_NAME);
    if (!atom)
	return MOCHA_FALSE;

    /* XXX mc->globalObject vs. top level scope found from mc->staticLink. */
    prototype = 0;
    ok = mocha_LookupSymbol(mc, mc->globalObject->scope, atom, MLF_GET, &sym);
    if (ok && sym &&
	sym->type == SYM_PROPERTY &&
	sym_property(sym)->datum.tag == MOCHA_FUNCTION) {
	fun = sym_property(sym)->datum.u.fun;
	ok = mocha_LookupSymbol(mc, fun->object.scope, mocha_prototypeAtom,
				MLF_GET, &sym);
	if (ok && sym && sym->type == SYM_PROPERTY) {
	    dp = &sym_property(sym)->datum;
	    if (dp->tag == MOCHA_OBJECT)
		prototype = dp->u.obj;
	}
    }

    mocha_DropAtom(mc, atom);
    *objp = prototype;
    return ok;
}

MochaBoolean
mocha_SetPrototype(MochaContext *mc, MochaFunction *fun, MochaObject *obj)
{
    MochaDatum d;

    /* Define a property for the prototype in the function's scope. */
    if (!mocha_GetMutableScope(mc, &fun->object))
	return MOCHA_FALSE;
    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_OBJECT, 0, MOCHA_TAINT_IDENTITY,
			  u.obj, obj);
    if (!mocha_SetProperty(mc, fun->object.scope, mocha_prototypeAtom,
			   fun->object.scope->minslot-1, d)) {
	return MOCHA_FALSE;
    }

    /* Define a property for the constructor in the prototype's scope. */
    if (!mocha_GetMutableScope(mc, obj))
	return MOCHA_FALSE;
    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_FUNCTION,
			  MDF_BACKEDGE | MDF_READONLY, MOCHA_TAINT_IDENTITY,
			  u.fun, fun);
    if (!mocha_SetProperty(mc, obj->scope, mocha_constructorAtom,
			   obj->scope->minslot-1, d)) {
	return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

MochaBoolean
mocha_GetMutableScope(MochaContext *mc, MochaObject *obj)
{
    MochaScope *scope, *newscope;

    scope = obj->scope;
    if (scope->object == obj)
	return MOCHA_TRUE;
    newscope = mocha_NewScope(mc, obj);
    if (!newscope)
	return MOCHA_FALSE;
    newscope->minslot = scope->minslot;
    newscope->freeslot = scope->freeslot;
    obj->scope = mocha_HoldScope(mc, newscope);
    mocha_DropScope(mc, scope);
    return MOCHA_TRUE;
}

MochaBoolean
mocha_InitObject(MochaContext *mc, MochaObject *obj, MochaClass *clazz,
		 void *data, MochaObject *prototype, MochaObject *parent)
{
    MochaScope *scope;

    /* Get clazz's prototype from its constructor.prototype property. */
    if (!prototype && mc->globalObject) {
	if (!mocha_GetPrototype(mc, clazz, &prototype))
	    return MOCHA_FALSE;
	if (!prototype &&
	    !mocha_GetPrototype(mc, &mocha_ObjectClass, &prototype)) {
	    return MOCHA_FALSE;
	}
    }

    /* Share the given prototype's scope (create it if necessary). */
    if (prototype) {
	scope = prototype->scope;
    } else {
	scope = mocha_NewScope(mc, obj);
	if (!scope)
	    return MOCHA_FALSE;
    }

    obj->nrefs = 0;
    obj->clazz = clazz;
    obj->data = data;
    obj->scope = mocha_HoldScope(mc, scope);
    obj->prototype = MOCHA_HoldObject(mc, prototype);
    obj->parent = parent;
    return MOCHA_TRUE;
}

void
mocha_FreeObject(MochaContext *mc, MochaObject *obj)
{
    MochaScope *scope;

    /* Set obj->nrefs to a magic value that can't be incremented. */
    PR_ASSERT(obj->nrefs == 0);
    obj->nrefs = MOCHA_FINALIZING;

    /* Drop obj->scope first, in case kid finalizers use this obj->data. */
    scope = obj->scope;
    if (scope->object == obj)
	scope->object = 0;
    obj->scope = 0;
    mocha_DropScope(mc, scope);

    /* Finalize obj, which must be unreferenced, and clear obj->data. */
    OBJ_FINALIZE(mc, obj);
    obj->data = 0;

    /* Drop our prototype object reference and clear parent. */
    MOCHA_DropObject(mc, obj->prototype);
    obj->prototype = 0;
    obj->parent = 0;

    /* Sanity: clear obj->nrefs (XXX mark it MOCHA_FINALIZED instead?). */
    PR_ASSERT(obj->nrefs == MOCHA_FINALIZING);
    obj->nrefs = 0;
}

MochaObject *
mocha_NewObject(MochaContext *mc, MochaClass *clazz, void *data,
		MochaObject *prototype, MochaObject *parent)
{
    MochaObject *obj;

    obj = MOCHA_malloc(mc, sizeof *obj);
    if (!obj)
	return 0;
    if (!mocha_InitObject(mc, obj, clazz, data, prototype, parent)) {
	MOCHA_free(mc, obj);
	return 0;
    }
    return obj;
}

void
mocha_DestroyObject(MochaContext *mc, MochaObject *obj)
{
    mocha_FreeObject(mc, obj);
    MOCHA_free(mc, obj);
}

MochaObject *
mocha_NewObjectByClass(MochaContext *mc, MochaClass *clazz)
{
    return mocha_NewObject(mc, clazz, 0, 0, 0);
}

MochaObject *
mocha_NewObjectByPrototype(MochaContext *mc, MochaObject *prototype)
{
    return mocha_NewObject(mc, prototype->clazz, 0, prototype, 0);
}
