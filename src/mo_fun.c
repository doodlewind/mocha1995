/*
** Mocha function methods.
**
** Brendan Eich, 11/15/95
*/
#include <ctype.h>
#include <string.h>
#include "mo_atom.h"
#include "mo_bcode.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mo_parse.h"
#include "mo_scan.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

static MochaAtom *
function_to_atom(MochaContext *mc, MochaFunction *fun)
{
    MochaPrinter *mp;
    MochaBoolean ok;
    char *str;

    mp = mocha_NewPrinter(mc, atom_name(fun->atom), 0);
    if (!mp)
	return 0;
    ok = mocha_DecompileFunction(fun, mp);
    if (ok)
	ok = mocha_GetPrinterOutput(mp, &str);
    mocha_DestroyPrinter(mp);
    if (!ok)
	return 0;
    return mocha_Atomize(mc, str, ATOM_STRING);
}

MochaBoolean
mocha_FunctionToString(MochaContext *mc, MochaFunction *fun, MochaAtom **atomp)
{
    MochaAtom *atom;

    atom = function_to_atom(mc, fun);
    if (!atom)
	return MOCHA_FALSE;
    *atomp = mocha_HoldAtom(mc, atom);
    return MOCHA_TRUE;
}

MochaBoolean
mocha_DatumToFunction(MochaContext *mc, MochaDatum d, MochaFunction **funp)
{
    MochaDatum aval, rval;
    MochaObject *obj;
    MochaAtom *atom;

    aval = d;
    if (!mocha_ResolveValue(mc, &d))
	return MOCHA_FALSE;
    switch (d.tag) {
      case MOCHA_FUNCTION:
	obj = &d.u.fun->object;
	break;
      case MOCHA_OBJECT:
	obj = d.u.obj;
	if (obj) {
	    rval = MOCHA_void;
	    if (!OBJ_CONVERT(mc, obj, MOCHA_FUNCTION, &rval))
		return MOCHA_FALSE;
	    MOCHA_MIX_TAINT(mc, mc->taintInfo->accum, rval.taint);
	    if (rval.tag == MOCHA_FUNCTION) {
		obj = &rval.u.fun->object;
		break;
	    }
	}
	/* FALL THROUGH */
      default:
	if (mocha_RawDatumToString(mc, aval, &atom)) {
	    MOCHA_ReportError(mc, "%s is not a function", atom_name(atom));
	    mocha_DropAtom(mc, atom);
	}
	return MOCHA_FALSE;
    }
    *funp = (MochaFunction *)MOCHA_HoldObject(mc, obj);
    return MOCHA_TRUE;
}

static MochaBoolean
IsIdentifier(const char *s)
{
    char c = *s;

    if (!isalpha(c) && c != '_')
	return MOCHA_FALSE;
    while ((c = *++s) != '\0') {
	if (!isalnum(c) && c != '_')
	    return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

enum fun_slot {
    FUN_CALLER    = -1,         /* function that called this fun activation */
    FUN_ARGUMENTS = -2,         /* actual arguments for this call to fun */
    FUN_LENGTH    = -3          /* number of arguments to this call */
};

static MochaPropertySpec function_props[] = {
    {"caller",          FUN_CALLER},
    {"arguments",       FUN_ARGUMENTS,  MDF_BACKEDGE},
    {"length",          FUN_LENGTH},
    {0}
};

static MochaBoolean
fun_get_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		 MochaDatum *dp)
{
    MochaFunction *fun;
    MochaStackFrame *fp;
    MochaBoolean valid;

    fun = (MochaFunction *)obj;
    fp = mc->stack.frame;
    valid = (fp && fp->fun == fun);

    switch (slot) {
      case FUN_CALLER:
	if (valid && fp->down) {
	    MOCHA_INIT_DATUM(mc, dp, MOCHA_FUNCTION, u.fun, fp->down->fun);
	} else {
	    *dp = MOCHA_null;
	}
	break;

      case FUN_ARGUMENTS:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_OBJECT, u.obj, valid ? obj : 0);
	break;

      case FUN_LENGTH:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval,
			 valid ? fp->argc : fun->nargs);
	break;

      default:
	if (slot < 0)
	    return MOCHA_TRUE;
	if (valid && (uint32)slot < (uint32)fp->argc)
	    *dp = fp->argv[slot];
	break;
    }

    return MOCHA_TRUE;
}

static MochaBoolean
fun_convert(MochaContext *mc, MochaObject *obj, MochaTag tag, MochaDatum *dp);

static void
fun_finalize(MochaContext *mc, MochaObject *obj)
{
    MochaFunction *fun;

    fun = (MochaFunction *)obj;
    mocha_DropAtom(mc, fun->atom);
    if (fun->bound)
	MOCHA_DropObject(mc, fun->object.parent);
    if (fun->script)
	mocha_DestroyScript(mc, fun->script);
}

static MochaClass function_class = {
    "Function",
    fun_get_property, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub, fun_convert, fun_finalize
};

/* This needs function_class, so it has a forward declaration above. */
static MochaBoolean
fun_convert(MochaContext *mc, MochaObject *obj, MochaTag tag, MochaDatum *dp)
{
    switch (tag) {
      case MOCHA_FUNCTION:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_FUNCTION, u.fun, (MochaFunction *)obj);
	return MOCHA_TRUE;
      default:
	return MOCHA_TRUE;
    }
}

static MochaBoolean
fun_to_string(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFunction *fun;
    MochaAtom *atom;

    if (!MOCHA_InstanceOf(mc, obj, &function_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    fun = (MochaFunction *)obj;
    atom = function_to_atom(mc, fun);
    if (!atom)
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
fun_value_of(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    if (!MOCHA_InstanceOf(mc, obj, &function_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_FUNCTION, u.fun, (MochaFunction *)obj);
    return MOCHA_TRUE;
}

static MochaFunctionSpec function_methods[] = {
    {mocha_toStringStr,		fun_to_string,		0},
    {mocha_valueOfStr,		fun_value_of,		0},
    {0}
};

static MochaBoolean
Function(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFunction *fun;
    unsigned i, nargs, lineno;
    MochaAtom *atom;
    const char *name, *filename;
    MochaSymbol *arg, *args, **argp;
    MochaTokenStream *ts;
    MochaBoolean ok;

    nargs = argc ? argc - 1 : 0;
    fun = mocha_NewFunction(mc, 0, nargs, obj->parent, mocha_anonymousAtom);
    if (!fun)
	return MOCHA_FALSE;

    args = 0;
    argp = &args;
    for (i = 0; i < nargs; i++) {
	if (!mocha_DatumToString(mc, argv[i], &atom))
	    goto fail;
	name = atom_name(atom);
	if (!IsIdentifier(name)) {
	    MOCHA_ReportError(mc, "illegal formal argument name %s", name);
	    mocha_DropAtom(mc, atom);
	    goto fail;
	}

	arg = mocha_DefineSymbol(mc, fun->object.scope, atom, SYM_ARGUMENT, 0);
	mocha_DropAtom(mc, atom);
	if (!arg) goto fail;
	arg->slot = i;
	*argp = arg;
	argp = &arg->next;
    }

    if (i < argc) {
	if (!mocha_DatumToString(mc, argv[i], &atom))
	    goto fail;
    } else {
	atom = mocha_HoldAtom(mc, MOCHA_empty.u.atom);
    }
    if (mc->script) {
	filename = mc->script->filename;
	lineno = mocha_PCtoLineNumber(mc->script, mc->pc);
    } else {
	filename = 0;
	lineno = 0;
    }
    ts = mocha_NewTokenStream(mc, atom_name(atom), atom->length,
			      filename, lineno);
    if (ts) {
	ok = mocha_ParseFunctionBody(mc, ts, fun);
	(void) mocha_CloseTokenStream(ts);
    } else {
	ok = MOCHA_FALSE;
    }
    mocha_DropAtom(mc, atom);
    if (!ok)
	goto fail;
    fun->script->args = args;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, &fun->object);
    return MOCHA_TRUE;

fail:
    mocha_DestroyFunction(mc, fun);
    *rval = MOCHA_null;
    return MOCHA_TRUE;
}

MochaObject *
mocha_InitFunctionClass(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_InitClass(mc, obj, &function_class, 0, Function, 1,
			   function_props, function_methods, 0, 0);
}

MochaObject *
mocha_FunctionToObject(MochaContext *mc, MochaFunction *fun)
{
    return &fun->object;
}

MochaFunction *
mocha_NewFunction(MochaContext *mc, MochaNativeCall call, unsigned nargs,
		  MochaObject *parent, MochaAtom *atom)
{
    MochaFunction *fun;
    MochaObject *prototype;

    /* Allocate a function object. */
    fun = MOCHA_malloc(mc, sizeof *fun);
    if (!fun)
	return 0;

    /* Initialize base state. */
    if (!mocha_GetPrototype(mc, &function_class, &prototype) ||
	!mocha_InitObject(mc, &fun->object, &function_class, 0, prototype,
			  parent)) {
	MOCHA_free(mc, fun);
	return 0;
    }

    /* Initialize derived class state. */
    fun->call = call;
    fun->nargs = nargs;
    fun->bound = MOCHA_FALSE;
    fun->spare = 0;
    fun->atom = mocha_HoldAtom(mc, atom);
    fun->script = 0;
    return fun;
}

void
mocha_DestroyFunction(MochaContext *mc, MochaFunction *fun)
{
    mocha_DestroyObject(mc, &fun->object);
}

/* XXX orthogonalize a la NewObject/DefineObject/DefineNewObject */
MochaFunction *
mocha_DefineFunction(MochaContext *mc, MochaObject *obj, MochaAtom *atom,
		     MochaNativeCall call, unsigned nargs, unsigned flags)
{
    MochaFunction *fun;
    MochaDatum fd;

    fun = mocha_NewFunction(mc, call, nargs, obj, atom);
    if (!fun)
	return 0;
    MOCHA_INIT_FULL_DATUM(mc, &fd, MOCHA_FUNCTION, flags, MOCHA_TAINT_IDENTITY,
			  u.fun, fun);
    if (!mocha_SetProperty(mc, obj->scope, atom, obj->scope->minslot-1, fd)) {
	mocha_DestroyFunction(mc, fun);
	return 0;
    }
    return fun;
}
