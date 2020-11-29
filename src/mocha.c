/*
** Mocha virtual machine.
**
** Brendan Eich, 6/20/95
*/
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "prlog.h"
#include "alloca.h"
#include "mo_atom.h"
#include "mo_bcode.h"
#include "mo_cntxt.h"
#include "mo_scope.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

#ifdef DEBUG
# include "mo_emit.h"	/* for mocha_PCtoLineNumber() */
#endif

static void stub_taint_counter(MochaContext *mc, uint16 taint) { }

MochaTaintCounter mocha_HoldTaint = stub_taint_counter;
MochaTaintCounter mocha_DropTaint = stub_taint_counter;

/*
** MochaDatum tag values used only in this file, for secret stack data types.
** If Properties and ObjectStacks were Objects, mocha_Hold/DropRef() might be
** simpler, but the structs would be fatter, and for-in and with code would
** be complicated.
*/
#define MOCHA_PROPERTY		255	/* u.pair, but obj+prop not obj+sym */
#define MOCHA_OBJECTSTACK	254	/* u.ptr, points at MochaObjectStack */

/*
** Hold and release object references from a stack datum, a global variable,
** a property, or a stack frame's return value datum.
*/
void
mocha_HoldRef(MochaContext *mc, MochaDatum *dp)
{
    switch (dp->tag) {
      case MOCHA_ATOM:
      case MOCHA_STRING:
	mocha_HoldAtom(mc, dp->u.atom);
	break;
      case MOCHA_SYMBOL:
	MOCHA_HoldObject(mc, dp->u.pair.obj);
	break;
      case MOCHA_FUNCTION:
      case MOCHA_OBJECT:
	if ((dp->flags & MDF_BACKEDGE) == 0)
	    MOCHA_HoldObject(mc, dp->u.obj);
	break;
    }
    if (dp->taint != MOCHA_TAINT_IDENTITY)
	(*mocha_HoldTaint)(mc, dp->taint);
}

void
mocha_DropRef(MochaContext *mc, MochaDatum *dp)
{
    MochaProperty *prop;
    MochaObjectStack *top;

    switch (dp->tag) {
      case MOCHA_ATOM:
      case MOCHA_STRING:
	dp->u.atom = mocha_DropAtom(mc, dp->u.atom);
	if (!dp->u.atom)
	    dp->tag = MOCHA_UNDEF;
	break;

      case MOCHA_SYMBOL:
	dp->u.pair.obj = MOCHA_DropObject(mc, dp->u.pair.obj);
	if (!dp->u.pair.obj) {
	    dp->tag = MOCHA_UNDEF;
	    dp->u.pair.sym = 0;
	}
	break;

      case MOCHA_FUNCTION:
      case MOCHA_OBJECT:
	if ((dp->flags & MDF_BACKEDGE) == 0) {
	    dp->u.obj = MOCHA_DropObject(mc, dp->u.obj);
	    if (!dp->u.obj)
		dp->tag = MOCHA_UNDEF;
	}
	break;

      case MOCHA_PROPERTY:
	prop = (MochaProperty *)dp->u.pair.sym;	/* XXX type me please */
	if (prop) {
	    PR_ASSERT(dp->u.pair.obj);
	    MOCHA_DropObject(mc, dp->u.pair.obj);
	    dp->u.pair.obj = 0;
	    dp->u.pair.sym = 0;
	}
	break;

      case MOCHA_OBJECTSTACK:
	top = dp->u.ptr;
	PR_ASSERT(top && top->object);
	mocha_PopObject(mc, top);
	dp->u.ptr = 0;
	break;
    }
    if (dp->taint != MOCHA_TAINT_IDENTITY)
	(*mocha_DropTaint)(mc, dp->taint);
}

/*
** These can't over- or underflow because the compiler computed worst-case
** stack depth, and mocha_Interpret() checks that mc has enough room before
** it starts pushing and popping.
*/
static void
Push(MochaContext *mc, MochaDatum d)
{
    PR_ASSERT(mc->stack.ptr < mc->stack.limit);

    MOCHA_ASSERT_VALID_DATUM_FLAGS(&d);
    mocha_HoldRef(mc, &d);
    *mc->stack.ptr++ = d;
}

static MochaDatum
Pop(MochaContext *mc, MochaBoolean drop)
{
    MochaDatum *dp;

    PR_ASSERT(mc->stack.ptr > mc->stack.base);
    dp = --mc->stack.ptr;

    MOCHA_ASSERT_VALID_DATUM_FLAGS(dp);
    if (drop)
	mocha_DropRef(mc, dp);
    return *dp;
}

static void
ReportStackOverflow(MochaContext *mc)
{
    MOCHA_ReportError(mc, "stack overflow in %s",
		      mc->stack.frame
		      ? atom_name(mc->stack.frame->fun->atom)
		      : "top-level");
}

static void
PushSymbol(MochaContext *mc, MochaObject *obj, MochaSymbol *sym)
{
    MochaPair pair;
    MochaDatum d;

    pair.obj = obj, pair.sym = sym;
    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_SYMBOL, 0, mc->taintInfo->accum,
			  u.pair, pair);
    Push(mc, d);
}

static void
PushObject(MochaContext *mc, MochaObject *obj)
{
    MochaDatum d;

    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_OBJECT, 0, mc->taintInfo->accum,
			  u.obj, obj);
    Push(mc, d);
}

static void
PushNumber(MochaContext *mc, MochaFloat fval)
{
    MochaDatum d;

    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_NUMBER, 0, mc->taintInfo->accum,
			  u.fval, fval);
    Push(mc, d);
}

static void
PushBoolean(MochaContext *mc, MochaBoolean bval)
{
    MochaDatum d;

    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_BOOLEAN, 0, mc->taintInfo->accum,
			  u.bval, bval);
    Push(mc, d);
}

static void
PushString(MochaContext *mc, MochaAtom *atom)
{
    MochaDatum d;

    MOCHA_INIT_FULL_DATUM(mc, &d, MOCHA_STRING, 0, mc->taintInfo->accum,
			  u.atom, atom);
    Push(mc, d);
}

static MochaBoolean
PopNumber(MochaContext *mc, MochaFloat *fvalp)
{
    MochaDatum d;
    MochaBoolean ok;

    d = Pop(mc, MOCHA_FALSE);
    ok = mocha_DatumToNumber(mc, d, fvalp);
    mocha_DropRef(mc, &d);
    return ok;
}

static MochaBoolean
PopInt(MochaContext *mc, MochaInt *ivalp, MochaBoolean *validp)
{
    MochaFloat fval;
    MochaInt ival;

    if (!PopNumber(mc , &fval))
	return MOCHA_FALSE;
    ival = (MochaInt)fval;
    *ivalp = ival;
#ifdef XP_PC
    if (MOCHA_FLOAT_IS_NaN(fval)) {
	*validp = MOCHA_FALSE;
	return MOCHA_TRUE;
    }
#endif
    *validp &= ((MochaFloat)ival == fval ||
		(MochaFloat)(MochaUint)ival == fval);
    return MOCHA_TRUE;
}

static MochaBoolean
PopBoolean(MochaContext *mc, MochaBoolean *bvalp)
{
    MochaDatum d;
    MochaBoolean ok;

    d = Pop(mc, MOCHA_FALSE);
    ok = mocha_DatumToBoolean(mc, d, bvalp);
    mocha_DropRef(mc, &d);
    return ok;
}

MochaBoolean
mocha_ResolveSymbol(MochaContext *mc, MochaDatum *dp, MochaLookupFlag flag)
{
    MochaPair pair;
    MochaAtom *atom;

    if (dp->tag == MOCHA_SYMBOL) {
	pair = dp->u.pair;
	atom = sym_atom(pair.sym);
    } else {
	if (dp->tag != MOCHA_ATOM)
	    return MOCHA_TRUE;
	atom = dp->u.atom;
	if (!mocha_SearchScopes(mc, atom, flag, &pair))
	    return MOCHA_FALSE;
	if (!pair.sym) {
	    pair.obj = mc->objectStack ? mc->objectStack->object
				       : mc->staticLink;
	    PR_ASSERT(pair.obj);
	    if (!pair.obj) return MOCHA_TRUE;
	}
    }
    if (!pair.sym || pair.sym->type == SYM_UNDEF) {
	if (!OBJ_RESOLVE_NAME(mc, pair.obj, atom_name(atom)))
	    return MOCHA_FALSE;
	if (!mocha_LookupSymbol(mc, pair.obj->scope, atom, flag, &pair.sym))
	    return MOCHA_FALSE;
	if (!pair.sym)
	    return MOCHA_TRUE;
    }
    dp->tag = MOCHA_SYMBOL;
    dp->u.pair = pair;
    return MOCHA_TRUE;
}

static MochaDatum *
NewVariable(MochaContext *mc, MochaSymbol *sym)
{
    MochaDatum *vp;

    vp = MOCHA_malloc(mc, sizeof *vp);
    if (!vp)
	return 0;
    *vp = MOCHA_void;
    vp->nrefs = 1;
    sym->entry.value = vp;
    return vp;
}

MochaDatum *
mocha_ResolveVariable(MochaContext *mc, MochaSymbol *sym)
{
    MochaStackFrame *fp, *fp2;
    MochaDatum *vp;
    MochaSlot nvars, delta;
    ptrdiff_t nbytes;

    for (fp = mc->stack.frame; fp && fp->fun->call; fp = fp->down)
	/* find non-native function frame */;
    if (!fp || sym->scope != fp->fun->object.scope)
	return NewVariable(mc, sym);

    switch (sym->type) {
      case SYM_ARGUMENT:
	PR_ASSERT((unsigned)sym->slot < fp->fun->nargs);
	vp = &fp->argv[sym->slot];
	break;

      case SYM_VARIABLE:
	PR_ASSERT((unsigned)sym->slot < fp->fun->object.scope->freeslot);
	nvars = sym->slot + 1;
	delta = nvars - fp->nvars;
	if (delta > 0) {
	    /* XXX over-conservative */
	    if (fp->vars + nvars + mc->script->depth > mc->stack.limit) {
		ReportStackOverflow(mc);
		return 0;
	    }

	    /* Add delta slots to the current stack frame. */
	    vp = &fp->vars[fp->nvars];
	    fp->nvars = nvars;
	    nbytes = (char *)mc->stack.ptr - (char *)vp;
	    PR_ASSERT(nbytes >= 0);
	    if (nbytes > 0)
		memmove(vp + delta, vp, nbytes);
	    mc->stack.ptr += delta;

	    /* Run down the stack frames from top to fp, fixing pointers. */
	    for (fp2 = mc->stack.frame; fp2 != fp; fp2 = fp2->down) {
		fp2->argv += delta;
		fp2->vars += delta;
	    }

	    /* Clear the new slots. */
	    do {
		*vp++ = MOCHA_void;
	    } while (--delta > 0);
	}
	vp = &fp->vars[sym->slot];
	break;

      default:
	PR_ASSERT(0);
	return 0;
    }
    return vp;
}

MochaBoolean
mocha_ResolveValue(MochaContext *mc, MochaDatum *dp)
{
    MochaSymbol *sym;
    MochaObject *obj;
    MochaDatum *vp, rval;
    MochaProperty *prop;

    if (!mocha_ResolveSymbol(mc, dp, MLF_GET))
	return MOCHA_FALSE;
    if (dp->tag != MOCHA_SYMBOL) {
	if (dp->tag == MOCHA_ATOM) {
	    MOCHA_ReportError(mc, "%s is not defined", atom_name(dp->u.atom));
	    return MOCHA_FALSE;
	}
    } else {
	sym = dp->u.pair.sym;
	switch (sym->type) {
	  case SYM_UNDEF:
	    *dp = MOCHA_void;
	    break;

	  case SYM_ARGUMENT:
	  case SYM_VARIABLE:
	    vp = sym_datum(sym);
	    if (!vp) {
		vp = mocha_ResolveVariable(mc, sym);
		if (!vp) {
		    *dp = MOCHA_void;
		    break;
		}
	    }
	    MOCHA_INIT_FULL_DATUM(mc, dp, vp->tag, 0, vp->taint, u, vp->u);
	    break;

	  case SYM_PROPERTY:
	    PR_ASSERT(sym->entry.value);
	    obj = dp->u.pair.obj;
	    prop = sym_property(sym);
	    vp = &prop->datum;
	    rval = *vp;
	    if (!(*prop->getter)(mc, obj, sym->slot, &rval))
		return MOCHA_FALSE;
	    MOCHA_ASSERT_VALID_DATUM_FLAGS(&rval);

/* XXX cope with naughty mo_java.c and lm_img.c (and others?) */
if (rval.tag == MOCHA_STRING && !rval.u.atom) rval.u.atom = MOCHA_empty.u.atom;

	    /* Hold any rval reference before dropping the old ref in vp. */
	    mocha_HoldRef(mc, &rval);
	    mocha_DropRef(mc, vp);

	    /* Update vp from rval, now that OBJ_GET_PROPERTY has succeeded. */
	    MOCHA_INIT_FULL_DATUM(mc, vp, rval.tag, rval.flags, rval.taint,
				  u, rval.u);

	    /* Copy most of vp to the temporary pointed at by dp. */
	    MOCHA_INIT_DATUM(mc, dp, vp->tag, u, vp->u);

	    /* NB: dp may have different taint from vp. */
	    MOCHA_MIX_TAINT(mc, dp->taint, vp->taint);
	    break;

	  default:
	    PR_ASSERT(0);
	    return MOCHA_FALSE;
	}
    }

    /* Accumulate taint according to the data tainting algebra. */
    MOCHA_MIX_TAINT(mc, mc->taintInfo->accum, dp->taint);
    return MOCHA_TRUE;
}

MochaBoolean
mocha_ResolvePrimitiveValue(MochaContext *mc, MochaDatum *dp)
{
    MochaDatum rval;

    if (!mocha_ResolveValue(mc, dp))
	return MOCHA_FALSE;

    if (dp->tag == MOCHA_OBJECT && dp->u.obj) {
	rval = *dp;
	if (!mocha_TryMethod(mc, dp->u.obj, mocha_valueOfAtom, 0, 0, &rval))
	    return MOCHA_FALSE;
	if (rval.tag != MOCHA_UNDEF) {
	    MOCHA_WeakenRef(mc, &rval);

	    /* Copy most of rval to the temporary pointed at by dp. */
	    MOCHA_INIT_DATUM(mc, dp, rval.tag, u, rval.u);

	    /* NB: dp may have different taint from rval. */
	    MOCHA_MIX_TAINT(mc, dp->taint, rval.taint);
	}
    }
    return MOCHA_TRUE;
}

static MochaBoolean
ResolveString(MochaContext *mc, MochaDatum d, MochaAtom **atomp)
{
    if (!mocha_ResolveValue(mc, &d))
	return MOCHA_FALSE;
    if (d.tag == MOCHA_OBJECT && d.u.obj)
	return mocha_RawDatumToString(mc, d, atomp);
    if (d.tag != MOCHA_STRING)
	return MOCHA_FALSE;
    *atomp = mocha_HoldAtom(mc, d.u.atom);
    return MOCHA_TRUE;
}

static MochaAtom *
CatStrings(MochaContext *mc, MochaAtom *atom1, MochaAtom *atom2)
{
    const char *s1, *s2;
    char *s;

    s1 = atom_name(atom1), s2 = atom_name(atom2);
    s = (char *)alloca(strlen(s1) + strlen(s2) + 1);
    strcat(strcpy(s, s1), s2);
    return mocha_Atomize(mc, s, ATOM_STRING);
}

/*
** Call() is not stack-invariant: it pushes missing formal arguments and
** predeclared local variables, calls the function at (sp->ptr - (argc + 1)),
** pops all variables and arguments, and pushes the return value.
*/
static MochaBoolean
Call(MochaContext *mc, unsigned argc)
{
    MochaDatum *vp, aval;
    MochaBoolean ok, no_parent;
    MochaFunction *fun;
    MochaObject *obj;
    MochaStackFrame frame;
    int missing, nslots;
    uint16 accum, taint;
    unsigned i;
    MochaObjectStack *save;

    /* Locate the function to call under the arguments on the current stack. */
    vp = mc->stack.ptr - (argc + 1);
    aval = *vp;
    if (!mocha_ResolveSymbol(mc, &aval, MLF_GET))
	return MOCHA_FALSE;

    /* Resolve aval to a held function, and determine its 'this' object. */
    if (!mocha_DatumToFunction(mc, aval, &fun))
	return MOCHA_FALSE;
    if (fun->bound)
	obj = MOCHA_HoldObject(mc, fun->object.parent);
    else if (aval.tag == MOCHA_SYMBOL)
	obj = MOCHA_HoldObject(mc, aval.u.pair.obj);
    else
	obj = MOCHA_HoldObject(mc, mc->staticLink);

    /* Make vp refer to fun, which is already held so it can be popped. */
    if (vp->taint != MOCHA_TAINT_IDENTITY)
	(*mocha_HoldTaint)(mc, vp->taint);
    mocha_DropRef(mc, vp);
    MOCHA_INIT_DATUM(mc, vp, MOCHA_FUNCTION, u.fun, fun);

    /* Initialize a stack frame for the function. */
    frame.fun = fun;
    frame.thisp = obj;
    frame.argc = argc;
    frame.argv = mc->stack.ptr - argc;
    frame.nvars = fun->object.scope->freeslot;
    frame.vars = mc->stack.ptr;
    frame.down = mc->stack.frame;
    frame.rval = MOCHA_void;

    /* Resolve args to values (call-by-value). */
    accum = mc->taintInfo->accum;
    for (vp = frame.argv; vp < frame.vars; vp++) {
	aval = *vp;
	if (!mocha_ResolveValue(mc, &aval)) {
	    MOCHA_DropObject(mc, obj);
	    return MOCHA_FALSE;
	}
	mocha_HoldRef(mc, &aval);
	mocha_DropRef(mc, vp);
	*vp = aval;
    }
    mc->taintInfo->accum = accum;

    /* Now that we're done resolving args, push frame. */
    mc->stack.frame = &frame;

    /* Prepare to push missing argument and predeclared variable slots. */
    missing = (argc < fun->nargs) ? fun->nargs - argc : 0;
    frame.vars += missing;
    if (frame.vars + frame.nvars > mc->stack.limit) {
	ReportStackOverflow(mc);
	MOCHA_DropObject(mc, obj);
	return MOCHA_FALSE;
    }

    /* Save number of missing args in nslots for post-call Pop() loop. */
    nslots = missing;
    missing += frame.nvars;
    while (--missing >= 0) {
	MOCHA_INIT_FULL_DATUM(mc, &aval, MOCHA_UNDEF, 0, accum, u.ptr, 0);
	Push(mc, aval);
    }

    /* Call the function, which is either native or interpreted. */
    if (fun->call) {
	ok = (*fun->call)(mc, obj, argc, frame.argv, &frame.rval);
	taint = frame.argv[-1].taint;
	for (i = 0; i < argc; i++)
	    MOCHA_MIX_TAINT(mc, taint, frame.argv[i].taint);
	MOCHA_MIX_TAINT(mc, frame.rval.taint, taint);
	mocha_HoldRef(mc, &frame.rval);
    } else if (fun->script) {
	save = mc->objectStack;
	mc->objectStack = 0;
	no_parent = (fun->object.parent == 0);
	if (no_parent)
	    fun->object.parent = mc->globalObject;
	ok = mocha_Interpret(mc, &fun->object, fun->script, &aval);
	if (ok)
	    mocha_DropRef(mc, &aval);
	if (no_parent)
	    fun->object.parent = 0;
	mc->objectStack = save;
    } else {
	/* fun might be onerror trying to report a syntax error in itself. */
	ok = MOCHA_TRUE;
    }

    /* Restore stack pointer, taking care to pop dynamic variables too. */
    nslots += 1 + argc + frame.nvars;
    while (--nslots >= 0)
	(void) Pop(mc, MOCHA_TRUE);
    PR_ASSERT(mc->stack.ptr == frame.argv - 1);

    /* Pop stack frame and drop the method's object. */
    mc->stack.frame = frame.down;
    MOCHA_DropObject(mc, obj);

    /* Push return value, *then* drop any object reference held by it. */
    Push(mc, frame.rval);
    mocha_DropRef(mc, &frame.rval);
    return ok;
}

/*
** Assign is not stack-invariant: it pops two operands, taking care not to
** lose the last reference to the right hand one, stores the left hand side,
** and pushes the result expression.
*/
static MochaBoolean
Assign(MochaContext *mc, uint16 *taintp)
{
    MochaDatum *vp, lval, rval, aval, aval2;
    MochaBoolean ok;
    MochaScope *scope;
    MochaObject *slink, *obj, *assignObj;
    MochaSymbol *sym, *assignSym;
    MochaProperty *prop;
    MochaAtom *atom;

    /* Resolve the right hand side to a value. */
    aval = rval = Pop(mc, MOCHA_FALSE);
    ok = mocha_ResolveValue(mc, &rval);
    if (!ok) {
	aval2 = MOCHA_void;
	goto out;
    }

    /* Resolve the left hand side to a symbol. */
    aval2 = lval = Pop(mc, MOCHA_FALSE);
    ok = mocha_ResolveSymbol(mc, &lval, MLF_SET);
    if (!ok)
	goto out;
    if (lval.tag != MOCHA_SYMBOL) {
	if (lval.tag != MOCHA_ATOM)
	    goto fail;

	/* Try to define a new global variable for the current static link. */
	slink = mc->staticLink;
	do {
	    scope = slink->scope;
	} while ((slink = slink->parent) != 0);
	sym = mocha_DefineSymbol(mc, scope, lval.u.atom, SYM_VARIABLE, 0);
	if (!sym) {
	    ok = MOCHA_FALSE;
	    goto out;
	}
	vp = NewVariable(mc, sym);
    } else {
	/* Set an argument, variable, or property to rval. */
	obj = lval.u.pair.obj;
	sym = lval.u.pair.sym;
	scope = sym->scope;

	/* If this symbol's value is an object, try its assign method. */
	vp = sym_datum(sym);
	if (vp && vp->tag == MOCHA_OBJECT && (assignObj = vp->u.obj)) {
	    ok = mocha_LookupSymbol(mc, assignObj->scope, mocha_assignAtom,
				    MLF_GET, &assignSym);
	    if (!ok) goto out;
	    if (assignSym) {
		PushSymbol(mc, assignObj, assignSym);
		Push(mc, rval);
		ok = Call(mc, 1);

		/* Don't reset taint accumulator on return from function. */
		*taintp = mc->taintInfo->accum;
		goto out;
	    }
	}

	/* No assign method, just mutate the symbol's value. */
	switch (sym->type) {
	  case SYM_ARGUMENT:
	  case SYM_VARIABLE:
	    if (!vp) {
		vp = mocha_ResolveVariable(mc, sym);
		if (!vp)
		    goto fail;
	    }
	    break;

	  case SYM_UNDEF:
	    sym = mocha_SetProperty(mc, scope, sym_atom(sym), scope->minslot-1,
				    MOCHA_null);
	    if (!sym) {
		ok = MOCHA_FALSE;
		goto out;
	    }
	    /* FALL THROUGH */

	  case SYM_PROPERTY:
	    prop = sym_property(sym);
	    ok = (*prop->setter)(mc, obj, sym->slot, &rval);
	    if (!ok)
		goto out;
	    vp = &prop->datum;
	    break;

	  default:
	    goto fail;
	}
    }

    /* Don't set readonly properties; do report an error. */
    MOCHA_ASSERT_VALID_DATUM_FLAGS(vp);
    if (vp->flags & MDF_READONLY)
	goto fail;
    vp->flags |= MDF_ENUMERATE;

    /* Hold rval before dropping the old value in case they're the same. */
    mocha_HoldRef(mc, &rval);
    mocha_DropRef(mc, vp);

    /* Don't store a reference to a finalizing object. */
    if (rval.tag == MOCHA_OBJECT &&
	rval.u.obj && rval.u.obj->nrefs == MOCHA_FINALIZING) {
	rval.u.obj = 0;
    }

    /* Store rval, taking care not to smash vp->nrefs and vp->flags. */
    MOCHA_INIT_FULL_DATUM(mc, vp, rval.tag, vp->flags, rval.taint,
			  u, rval.u);

    /* Push the return value. */
    Push(mc, rval);

out:
    /* Finally, drop any refs held by the left and old right hand sides. */
    mocha_DropRef(mc, &aval);
    mocha_DropRef(mc, &aval2);
    return ok;

fail:
    if (mocha_RawDatumToString(mc, lval, &atom)) {
	MOCHA_ReportError(mc, "%s can't be set by assignment",
			  atom_name(atom));
	mocha_DropAtom(mc, atom);
    }
    ok = MOCHA_FALSE;
    goto out;
}

MochaBoolean
mocha_Call(MochaContext *mc, MochaDatum fd,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    unsigned i;
    MochaBoolean ok;

    if (mc->stack.ptr + argc >= mc->stack.limit) {
	ReportStackOverflow(mc);
	return MOCHA_FALSE;
    }
    Push(mc, fd);
    for (i = 0; i < argc; i++)
	Push(mc, argv[i]);
    ok = Call(mc, argc);
    if (ok)
	*rval = Pop(mc, MOCHA_FALSE);
    else
	(void) Pop(mc, MOCHA_TRUE);
    return ok;
}

MochaBoolean
mocha_TypeOfDatum(MochaContext *mc, MochaDatum d, MochaAtom **atomp)
{
    while (d.tag == MOCHA_ATOM || d.tag == MOCHA_SYMBOL) {
	if (!mocha_ResolveSymbol(mc, &d, MLF_GET))
	    return MOCHA_FALSE;
	if (d.tag == MOCHA_ATOM || d.tag == MOCHA_INTERNAL)
	    break;
	if (!mocha_ResolveValue(mc, &d))
	    return MOCHA_FALSE;
    }
    /* XXX hide these implementation tags */
    if (d.tag == MOCHA_ATOM || d.tag == MOCHA_INTERNAL)
	d.tag = MOCHA_UNDEF;
    *atomp = mocha_typeAtoms[d.tag];
    return MOCHA_TRUE;
}

MochaBoolean
mocha_Interpret(MochaContext *mc, MochaObject *slink, MochaScript *script,
		MochaDatum *result)
{
    MochaObject *oldslink;
    MochaCode *oldpc, *pc, *end;
    MochaScript *oldscript;
    MochaBranchCallback onBranch;
    MochaBoolean ok, bval, valid;
    MochaStack *sp;
    MochaDatum *oldtos, *bottom;
    uint16 taint;
    int len, argc;
    MochaOp op;
    MochaCodeSpec *cs;
    MochaDatum *vp, lval, rval, aval, aval2;
    MochaObject *obj, *obj2, *prototype;
    MochaObjectStack *top;
    MochaProperty *prop;
    MochaInt ival, ival2;
    MochaFloat fval, fval2;
    MochaSymbol *sym;
    MochaAtom *atom, *atom2, *atom3;
    MochaFunction *fun;
    MochaSlot slot;

    *result = MOCHA_void;

    oldslink = mc->staticLink;
    mc->staticLink = slink;
    oldpc = mc->pc;
    oldscript = mc->script;
    mc->script = script;
    onBranch = mc->branchCallback;
    ok = MOCHA_TRUE;

#define CHECK_BRANCH() {                                                      \
    if (onBranch && !(*onBranch)(mc, script)) {                               \
	ok = MOCHA_FALSE;                                                     \
	goto out;                                                             \
    }                                                                         \
}

    sp = &mc->stack;
    oldtos = sp->ptr;
    if (oldtos + script->depth >= sp->limit) {
	ReportStackOverflow(mc);
	return MOCHA_FALSE;
    }

    pc = script->code;
    end = pc + script->length;

    while (pc < end) {
	taint = mc->taintInfo->accum;
	mc->pc = pc;
	op = *pc;
	cs = &mocha_CodeSpec[op];
	len = cs->length;
#ifdef DEBUG
	if (mc->tracefp) {
	    int nuses, n;
	    MochaDatum d;

	    fprintf(mc->tracefp, "%4u: ", mocha_PCtoLineNumber(script, pc));
	    mocha_Disassemble1(mc, script, pc, pc - script->code, mc->tracefp);
	    nuses = cs->nuses;
	    if (nuses) {
		for (n = nuses; n > 0; n--) {
		    d = sp->ptr[-n];
		    if (mocha_RawDatumToString(mc, d, &atom)) {
			fprintf(mc->tracefp, "%s %s",
				(n == nuses) ? "  inputs:" : ",",
				atom_name(atom));
			mocha_DropAtom(mc, atom);
		    }
		}
		putc('\n', mc->tracefp);
	    }
	}
#endif

	switch (op) {
	  case MOP_NOP:
	    ALLOCA_GC();
	    break;

	  case MOP_PUSH:
	    Push(mc, MOCHA_void);	/* no need to taint (yet) */
	    break;

	  case MOP_POP:
	    aval = rval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_ResolveValue(mc, &rval);
	    if (rval.tag != MOCHA_PROPERTY) {
		PR_ASSERT(rval.tag != MOCHA_OBJECTSTACK);
		mocha_HoldRef(mc, &rval);
		mocha_DropRef(mc, result);
		*result = rval;
	    }
	    mocha_DropRef(mc, &aval);
	    if (!ok)
		goto out;
	    break;

	  case MOP_ENTER:
	    rval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_DatumToObject(mc, rval, &obj);
	    mocha_DropRef(mc, &rval);
	    if (!obj) {
		if (mocha_RawDatumToString(mc, rval, &atom)) {
		    MOCHA_ReportError(mc,
				      "%s can't be used in a with statement",
				      atom_name(atom));
		    mocha_DropAtom(mc, atom);
		}
		ok = MOCHA_FALSE;
	    }
	    if (!ok) goto out;
	    ok = mocha_PushObject(mc, obj, &top);
	    if (!ok) goto out;
	    MOCHA_INIT_FULL_DATUM(mc, &rval, MOCHA_OBJECTSTACK,
				  0, MOCHA_TAINT_IDENTITY,
				  u.ptr, top);
	    MOCHA_DropObject(mc, obj);
	    Push(mc, rval);
	    break;

	  case MOP_LEAVE:
	    PR_ASSERT(sp->ptr[-1].tag == MOCHA_OBJECTSTACK);
	    (void) Pop(mc, MOCHA_TRUE);
	    break;

	  case MOP_RETURN:
	    CHECK_BRANCH();
	    aval = rval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_ResolveValue(mc, &rval);
	    mocha_HoldRef(mc, &rval);
	    sp->frame->rval = rval;
	    mocha_DropRef(mc, &aval);
	    goto out;

	  case MOP_GOTO:
	    CHECK_BRANCH();
	    len = GET_JUMP_OFFSET(pc);
	    break;

	  case MOP_IFEQ:
	    CHECK_BRANCH();
	    if (!(ok = PopBoolean(mc, &bval)))
		goto out;
	    if (bval == MOCHA_FALSE)
		len = GET_JUMP_OFFSET(pc);
	    taint = mc->taintInfo->accum;
	    break;

	  case MOP_IFNE:
	    CHECK_BRANCH();
	    if (!(ok = PopBoolean(mc, &bval)))
		goto out;
	    if (bval != MOCHA_FALSE)
		len = GET_JUMP_OFFSET(pc);
	    taint = mc->taintInfo->accum;
	    break;

	  case MOP_IN:
	    aval = rval = Pop(mc, MOCHA_FALSE);
	    lval = Pop(mc, MOCHA_TRUE);

	    /* If the thing to the right of 'in' isn't an object, break. */
	    ok = mocha_DatumToObject(mc, rval, &obj);
	    mocha_DropRef(mc, &aval);
	    if (!ok) goto out;
	    if (!obj) {
		PushBoolean(mc, MOCHA_FALSE);
		break;
	    }

	    /* Save obj held by obj2 to suppress clone-parent properties. */
	    obj2 = MOCHA_HoldObject(mc, obj);
	  again:
	    prototype = obj->prototype;

	    /*
	    ** Don't hold a property reference here, there is no way yet for
	    ** Mocha users to remove properties (XXX).
	    */
	    vp = sp->ptr - 1;
	    if (vp->tag == MOCHA_UNDEF) {
		/* Let lazy reflectors be eager so for-in works for them. */
		ok = OBJ_LIST_PROPERTIES(mc, obj);
		if (!ok) {
		    MOCHA_DropObject(mc, obj);
		    MOCHA_DropObject(mc, obj2);
		    goto out;
		}

		/* Set the iterator to point to the first property. */
		prop = obj->scope->props;

		/* Rewrite the iterator tag so we know to do the next case. */
		vp->tag = MOCHA_PROPERTY;
		vp->u.pair.obj = MOCHA_HoldObject(mc, obj);
	    } else {
		/* Use the iterator to find the next property. */
		PR_ASSERT(vp->tag == MOCHA_PROPERTY);
		prop = (MochaProperty *)vp->u.pair.sym;	/* XXX type me please */

		/* If we're enumerating a prototype, reset obj and prototype. */
		if (obj != vp->u.pair.obj) {
		    MOCHA_DropObject(mc, obj);
		    obj = MOCHA_HoldObject(mc, vp->u.pair.obj);
		    prototype = obj->prototype;
		}
		PR_ASSERT(!prop || prop->lastsym->scope == obj->scope);
	    }
	    MOCHA_DropObject(mc, obj);

	    /* Skip pre-defined properties for backward compatibility. */
	    while (prop) {
		if (prop->datum.flags & MDF_ENUMERATE) {
		    /* Have we already enumerated a clone of this property? */
		    atom = sym_atom(prop->lastsym);
		    mocha_LookupSymbol(mc, obj2->scope, atom, MLF_GET, &sym);
		    if (sym && sym->entry.value == prop)
			break;
		}
		prop = prop->next;
	    }

	    if (!prop) {
		/* Enumerate prototype properties, if there are any. */
		if (prototype) {
		    obj = MOCHA_HoldObject(mc, prototype);
		    MOCHA_DropObject(mc, vp->u.pair.obj);
		    vp->u.pair.obj = MOCHA_HoldObject(mc, obj);
		    vp->u.pair.sym = (MochaSymbol *)obj->scope->props;
		    goto again;
		}

		/* End of property list -- terminate this loop. */
		PushBoolean(mc, MOCHA_FALSE);
		MOCHA_DropObject(mc, obj2);
		break;
	    }
	    MOCHA_DropObject(mc, obj2);

	    /* Make a string for the iterator name and assign it to lval. */
	    atom = sym_atom(prop->lastsym);
	    vp->u.pair.sym = (MochaSymbol *)prop->next;
	    Push(mc, lval);
	    PushString(mc, atom);
#ifdef DEBUG_brendan
	    mc->pc = pc;
#endif
	    ok = Assign(mc, &taint);
	    if (!ok)
		goto out;

	    /* Throw away Assign()'s result and push true to keep looping. */
	    (void) Pop(mc, MOCHA_TRUE);
	    PushBoolean(mc, MOCHA_TRUE);
	    break;

	  case MOP_DUP:
	    PR_ASSERT(sp->ptr > sp->base);
	    Push(mc, sp->ptr[-1]);
	    break;

	  case MOP_ASSIGN:
#ifdef DEBUG_brendan
	    mc->pc = pc;
#endif
	    ok = Assign(mc, &taint);
	    if (!ok)
		goto out;
	    break;

#define INTEGEROP(OP, EXTRA_CODE, LEFT_CAST) {                                \
    valid = MOCHA_TRUE;                                                       \
    if (!(ok = PopInt(mc,&ival2,&valid)) || !(ok = PopInt(mc,&ival,&valid)))  \
	goto out;                                                             \
    EXTRA_CODE                                                                \
    if (valid)                                                                \
	PushNumber(mc, LEFT_CAST ival OP ival2);                              \
    else                                                                      \
	PushNumber(mc, MOCHA_NaN.u.fval);                                     \
}

#define BITWISEOP(OP)		INTEGEROP(OP, (void) 0;, (MochaInt))
#define SIGNEDSHIFT(OP)		INTEGEROP(OP, ival2 &= 31;, (MochaInt))
#define UNSIGNEDSHIFT(OP)	INTEGEROP(OP, ival2 &= 31;, (MochaUint))

	  case MOP_BITOR:
	    BITWISEOP(|);
	    break;

	  case MOP_BITXOR:
	    BITWISEOP(^);
	    break;

	  case MOP_BITAND:
	    BITWISEOP(&);
	    break;

#ifdef XP_PC
#define COMPARE_FLOATS(LVAL, OP, RVAL)                                        \
    ((MOCHA_FLOAT_IS_NaN(LVAL) || MOCHA_FLOAT_IS_NaN(RVAL))                   \
     ? MOCHA_FALSE                                                            \
     : (LVAL) OP (RVAL))
#else
#define COMPARE_FLOATS(LVAL, OP, RVAL) ((LVAL) OP (RVAL))
#endif

#define COMPARISON(OP, EXTRA_CODE) {                                          \
    aval = rval = Pop(mc, MOCHA_FALSE);                                       \
    aval2 = lval = Pop(mc, MOCHA_FALSE);                                      \
    ok = mocha_ResolveValue(mc, &lval) && mocha_ResolveValue(mc, &rval);      \
    if (ok) {                                                                 \
	atom = 0;                                                             \
	EXTRA_CODE                                                            \
	if (ResolveString(mc,lval,&atom) && ResolveString(mc,rval,&atom2)) {  \
	    bval = strcoll(atom_name(atom), atom_name(atom2)) OP 0;           \
	    mocha_DropAtom(mc, atom);                                         \
	    mocha_DropAtom(mc, atom2);                                        \
	} else {                                                              \
	    if (atom) mocha_DropAtom(mc, atom);                               \
	    ok = mocha_DatumToNumber(mc, lval, &fval) &&                      \
		 mocha_DatumToNumber(mc, rval, &fval2);                       \
	    if (ok)                                                           \
		bval = COMPARE_FLOATS(fval, OP, fval2);                       \
	}                                                                     \
    }                                                                         \
    mocha_DropRef(mc, &aval);                                                 \
    mocha_DropRef(mc, &aval2);                                                \
    if (!ok)                                                                  \
	goto out;                                                             \
    PushBoolean(mc, bval);                                                    \
}

#define EQUALITYOP(OP) {                                                      \
    COMPARISON(OP,                                                            \
	if ((lval.tag == MOCHA_FUNCTION || lval.tag == MOCHA_OBJECT) &&       \
	    (rval.tag == MOCHA_FUNCTION || rval.tag == MOCHA_OBJECT)) {       \
	    bval = lval.u.obj OP rval.u.obj;                                  \
	} else if (MOCHA_DATUM_IS_NULL(lval) || MOCHA_DATUM_IS_NULL(rval)) {  \
	    obj = obj2 = 0;                                                   \
	    ok = mocha_DatumToObject(mc, lval, &obj) &&                       \
		 mocha_DatumToObject(mc, rval, &obj2);                        \
	    if (ok)                                                           \
		bval = obj OP obj2;                                           \
	    if (obj)  MOCHA_DropObject(mc, obj);                              \
	    if (obj2) MOCHA_DropObject(mc, obj2);                             \
	} else                                                                \
    )                                                                         \
}

#define RELATIONAL(OP)	COMPARISON(OP, (void) 0;)

	  case MOP_EQ:
	    EQUALITYOP(==);
	    break;

	  case MOP_NE:
	    EQUALITYOP(!=);
	    break;

	  case MOP_LT:
	    RELATIONAL(<);
	    break;

	  case MOP_LE:
	    RELATIONAL(<=);
	    break;

	  case MOP_GT:
	    RELATIONAL(>);
	    break;

	  case MOP_GE:
	    RELATIONAL(>=);
	    break;

#undef COMPARISON
#undef EQUALITYOP
#undef RELATIONAL

	  case MOP_LSH:
	    SIGNEDSHIFT(<<);
	    break;

	  case MOP_RSH:
	    SIGNEDSHIFT(>>);
	    break;

	  case MOP_URSH:
	    UNSIGNEDSHIFT(>>);
	    break;

#undef INTEGEROP
#undef BITWISEOP
#undef SIGNEDSHIFT
#undef UNSIGNEDSHIFT

	  case MOP_ADD:
	    rval = Pop(mc, MOCHA_FALSE);
	    lval = Pop(mc, MOCHA_FALSE);
	    atom = atom2 = 0;
	    if (ResolveString(mc,lval,&atom) || ResolveString(mc,rval,&atom2)) {
		ok = atom ? mocha_DatumToString(mc, rval, &atom2)
			  : mocha_DatumToString(mc, lval, &atom);
		if (ok) {
		    if (atom == MOCHA_empty.u.atom)
			atom3 = atom2;
		    else if (atom2 == MOCHA_empty.u.atom)
			atom3 = atom;
		    else
			atom3 = CatStrings(mc, atom, atom2);
		    if (!atom3)
			ok = MOCHA_FALSE;
		}
		if (ok)
		    PushString(mc, atom3);
		if (atom)  mocha_DropAtom(mc, atom);
		if (atom2) mocha_DropAtom(mc, atom2);
	    } else {
		ok = mocha_DatumToNumber(mc, lval, &fval) &&
		     mocha_DatumToNumber(mc, rval, &fval2);
		if (ok)
		    PushNumber(mc, fval + fval2);
	    }
	    mocha_DropRef(mc, &lval);
	    mocha_DropRef(mc, &rval);
	    if (!ok)
		goto out;
	    break;

#define BINARYOP(OP) {                                                        \
    if (!(ok = PopNumber(mc, &fval2)) || !(ok = PopNumber(mc, &fval)))        \
	goto out;                                                             \
    PushNumber(mc, fval OP fval2);                                            \
}

	  case MOP_SUB:
	    BINARYOP(-);
	    break;

	  case MOP_MUL:
	    BINARYOP(*);
	    break;

	  case MOP_DIV:
	  case MOP_MOD:
	    if (!(ok = PopNumber(mc, &fval2)) || !(ok = PopNumber(mc, &fval)))
		goto out;
	    if (fval2 == 0)
		PushNumber(mc, MOCHA_NaN.u.fval);
	    else if (op == MOP_DIV)
		PushNumber(mc, fval / fval2);
	    else
		PushNumber(mc, fmod(fval, fval2));
	    break;

	  case MOP_NOT:
	    if (!(ok = PopBoolean(mc, &bval)))
		goto out;
	    PushBoolean(mc, !bval);
	    break;

	  case MOP_BITNOT:
	    valid = MOCHA_TRUE;
	    if (!(ok = PopInt(mc, &ival, &valid)))
		goto out;
	    if (!valid)
		PushNumber(mc, MOCHA_NaN.u.fval);
	    else
		PushNumber(mc, ~ival);
	    break;

	  case MOP_NEG:
	    if (!(ok = PopNumber(mc, &fval)))
		goto out;
	    PushNumber(mc, -fval);
	    break;

	  case MOP_NEW:
	    CHECK_BRANCH();

	    /* Get argc from immediate and find the constructor function. */
	    argc = pc[1];
	    vp = sp->ptr - (argc + 1);
	    PR_ASSERT(vp >= sp->base);
	    ok = mocha_DatumToFunction(mc, *vp, &fun);
	    if (!ok)
		goto out;

	    /* Find the constructor name in order to name its new scope. */
	    lval = *vp;
	    ok = mocha_ResolveSymbol(mc, &lval, MLF_GET);
	    if (!ok) {
		MOCHA_DropObject(mc, &fun->object);
		goto out;
	    }

	    /* Get the prototype object for this constructor function. */
	    ok = mocha_LookupSymbol(mc, fun->object.scope, mocha_prototypeAtom,
				    MLF_GET, &sym);
	    if (!ok) {
		MOCHA_DropObject(mc, &fun->object);
		goto out;
	    }

	    if (!sym ||
		sym->type != SYM_PROPERTY ||
		(prop = sym_property(sym))->datum.tag != MOCHA_OBJECT ||
		!(prototype = prop->datum.u.obj)) {
		prototype = mocha_NewObjectByClass(mc, &mocha_ObjectClass);
		if (!prototype) {
		    MOCHA_DropObject(mc, &fun->object);
		    ok = MOCHA_FALSE;
		    goto out;
		}
		if (!mocha_GetMutableScope(mc, prototype) ||
		    !mocha_SetPrototype(mc, fun, prototype)) {
		    MOCHA_DestroyObject(mc, prototype);
		    MOCHA_DropObject(mc, &fun->object);
		    ok = MOCHA_FALSE;
		    goto out;
		}
	    }

	    /* Create a new user-allocated object. */
	    obj = mocha_NewObjectByPrototype(mc, prototype);
	    if (!obj) {
                MOCHA_DropObject(mc, &fun->object);
		ok = MOCHA_FALSE;
		goto out;
	    }
	    obj = MOCHA_HoldObject(mc, obj);

	    /* Find the constructor property in obj's (prototype's) scope. */
	    ok = mocha_LookupSymbol(mc, obj->scope, mocha_constructorAtom,
				    MLF_GET, &sym);
	    if (!ok) {
		obj->clazz = &mocha_ObjectClass;
		MOCHA_DropObject(mc, obj);
                MOCHA_DropObject(mc, &fun->object);
		goto out;
	    }

	    /* Mutate the function reference at vp into a symbol ref. */
	    mocha_DropRef(mc, vp);
	    vp->tag = MOCHA_SYMBOL;
	    vp->u.pair.obj = obj;
	    vp->u.pair.sym = sym;
	    mocha_HoldRef(mc, vp);

	    /* Now we have an object with a constructor method -- call it. */
#ifdef DEBUG_brendan
	    mc->pc = pc;
#endif
	    ok = Call(mc, argc);
            MOCHA_DropObject(mc, &fun->object);
	    if (!ok) {
		obj->clazz = &mocha_ObjectClass;
		MOCHA_DropObject(mc, obj);
		goto out;
	    }

	    /* Don't reset taint accumulator on return from function. */
	    taint = mc->taintInfo->accum;

	    /* Pop the return value, taking care not to drop prematurely. */
	    rval = Pop(mc, MOCHA_FALSE);
	    if (rval.tag == MOCHA_OBJECT && rval.u.obj != obj) {
		obj->clazz = &mocha_ObjectClass;
		MOCHA_DropObject(mc, obj);
		obj = MOCHA_HoldObject(mc, rval.u.obj);
	    }
	    mocha_DropRef(mc, &rval);

	    /* Then push the newly constructed object. */
	    PushObject(mc, obj);

	    /* Finally, drop obj -- it may have been the return value(!). */
	    MOCHA_DropObject(mc, obj);
	    break;

	  case MOP_TYPEOF:
	    lval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_TypeOfDatum(mc, lval, &atom);
	    mocha_DropRef(mc, &lval);
	    if (!ok) goto out;
	    PushString(mc, atom ? atom : MOCHA_empty.u.atom);
	    break;

	  case MOP_VOID:
	    (void) Pop(mc, MOCHA_TRUE);
	    Push(mc, MOCHA_void);
	    break;

	  case MOP_INC:
	  case MOP_DEC:
	    /* The operand must contain a number. */
	    aval = lval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_DatumToNumber(mc, lval, &fval);
	    if (!ok) {
		mocha_DropRef(mc, &aval);
		goto out;
	    }

	    /* Push the post- or pre-incremented value. */
	    if (op == MOP_INC)
		PushNumber(mc, pc[1] ? fval++ : ++fval);
	    else
		PushNumber(mc, pc[1] ? fval-- : --fval);

	    /* XXX Need two stack slots to call Assign(). */
	    ok = (sp->ptr + 2 < sp->limit);
	    if (!ok) {
		ReportStackOverflow(mc);
		mocha_DropRef(mc, &aval);
		goto out;
	    }

	    /* Assign the resulting number to lval. */
	    Push(mc, lval);
	    PushNumber(mc, fval);
	    mocha_DropRef(mc, &aval);
#ifdef DEBUG_brendan
	    mc->pc = pc;
#endif
	    ok = Assign(mc, &taint);
	    if (!ok)
		goto out;
	    (void) Pop(mc, MOCHA_TRUE);
	    break;

	  case MOP_MEMBER:
	  case MOP_LMEMBER:
	    /* Pop an atom (held by an atom map) naming the member. */
	    rval = Pop(mc, MOCHA_TRUE);
	    PR_ASSERT(rval.tag == MOCHA_ATOM);

	    /* Pop the left part and resolve it to an object. */
	    lval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_DatumToObject(mc, lval, &obj);
	    mocha_DropRef(mc, &lval);
	    if (!ok) goto out;
	    if (!obj) {
		if (mocha_RawDatumToString(mc, lval, &atom)) {
		    MOCHA_ReportError(mc, "%s has no property named '%s'",
				      atom_name(atom), atom_name(rval.u.atom));
		    mocha_DropAtom(mc, atom);
		}
		ok = MOCHA_FALSE;
		goto out;
	    }

	    /* Lookup atom in object scope, push undef symbol if not found. */
	    atom = rval.u.atom;
	    sym = 0;
	    if (op == MOP_LMEMBER)
		ok = mocha_GetMutableScope(mc, obj);
	    if (ok) {
		ok = mocha_LookupSymbol(mc, obj->scope, atom,
					(op == MOP_LMEMBER) ? MLF_SET : MLF_GET,
					&sym);
		if (ok && !sym &&
		    (op == MOP_LMEMBER ||
		     (ok = mocha_GetMutableScope(mc, obj)))) {	/* XXXhertme! */
		    /* Create a new undefined symbol in a mutable scope. */
		    sym = mocha_DefineSymbol(mc, obj->scope, atom,
					     SYM_UNDEF, 0);
		    ok = (sym != 0);
		}
	    }
	    if (sym)
		PushSymbol(mc, obj, sym);
	    MOCHA_DropObject(mc, obj);
	    if (!ok) goto out;
	    break;

	  case MOP_INDEX:
	  case MOP_LINDEX:
	    /* Pop the index (without dropping it!) and resolve it. */
	    aval = rval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_ResolveValue(mc, &rval) &&
		 mocha_RawDatumToString(mc, rval, &atom);
	    mocha_DropRef(mc, &aval);
	    if (!ok)
		goto out;

	    /* Pop the array and resolve it to an object. */
	    lval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_DatumToObject(mc, lval, &obj);
	    mocha_DropRef(mc, &lval);
	    if (!ok || !obj) {
		if (ok && mocha_RawDatumToString(mc, lval, &atom2)) {
		    MOCHA_ReportError(mc,
				      "%s has no property indexed by '%s'",
				      atom_name(atom2), atom_name(atom));
		    mocha_DropAtom(mc, atom2);
		}
		mocha_DropAtom(mc, atom);
		ok = MOCHA_FALSE;
		goto out;
	    }

	    /* If rval is a nonnegative integer, treat it as a slot number. */
	    slot = -1;
	    if (mocha_RawDatumToNumber(mc, rval, &fval)) {
		ival = (MochaInt)fval;
		if (ival >= 0 && (MochaFloat)ival == fval)
		    slot = ival;
	    }

	    /* Lookup the indexed symbol, defining a new one if not found. */
	    sym = 0;
	    if (op == MOP_LINDEX)
		ok = mocha_GetMutableScope(mc, obj);
	    if (ok) {
		ok = mocha_LookupSymbol(mc, obj->scope, atom,
					(op == MOP_LINDEX) ? MLF_SET : MLF_GET,
					&sym);
		if (ok && !sym &&
		    (op == MOP_LINDEX ||
		     (ok = mocha_GetMutableScope(mc, obj)))) {	/* XXXhertme! */
		    /*
		    ** Create a new undefined symbol in a mutable scope.
		    ** XXX want a way to distinguish o[0x10] from o["16"]
		    */
		    sym = (slot < 0)
			? mocha_DefineSymbol(mc, obj->scope, atom, SYM_UNDEF, 0)
			: mocha_SetProperty(mc, obj->scope, atom, slot,
					    MOCHA_null);
		    ok = (sym != 0);
		}
	    }
	    mocha_DropAtom(mc, atom);
	    if (sym)
		PushSymbol(mc, obj, sym);
	    MOCHA_DropObject(mc, obj);
	    if (!ok) goto out;
	    break;

	  case MOP_CALL:
	    CHECK_BRANCH();

	    /* Resolve *vp to a function and call it. */
	    argc = pc[1];
#ifdef DEBUG_brendan
	    mc->pc = pc;
#endif
	    ok = Call(mc, argc);
	    if (!ok)
		goto out;

	    /* Don't reset taint accumulator on return from function. */
	    taint = mc->taintInfo->accum;
	    break;

	  case MOP_NAME:
	    MOCHA_INIT_FULL_DATUM(mc, &lval, MOCHA_ATOM,
				  0, MOCHA_TAINT_IDENTITY,
				  u.atom, GET_CONST_ATOM(mc, script, pc));
	    Push(mc, lval);
	    break;

	  case MOP_NUMBER:
	    atom = GET_CONST_ATOM(mc, script, pc);
	    PushNumber(mc, atom->fval);
	    break;

	  case MOP_STRING:
	    atom = GET_CONST_ATOM(mc, script, pc);
	    PushString(mc, atom);
	    break;

	  case MOP_ZERO:
	    PushNumber(mc, 0);
	    break;

	  case MOP_ONE:
	    PushNumber(mc, 1);
	    break;

	  case MOP_NULL:
	    PushObject(mc, 0);
	    break;

	  case MOP_THIS:
	    PushObject(mc, sp->frame ? sp->frame->thisp : slink);
	    break;

	  case MOP_FALSE:
	  case MOP_TRUE:
	    PushBoolean(mc, (op == MOP_TRUE) ? MOCHA_TRUE : MOCHA_FALSE);
	    break;

#ifdef MOCHA_HAS_DELETE_OPERATOR
	  case MOP_DELETE:
	    /* Delete the operand, which must be an object but may be null. */
	    lval = rval = Pop(mc, MOCHA_FALSE);
	    ok = mocha_DatumToObject(mc, rval, &obj);
	    mocha_DropRef(mc, &rval);
	    if (!ok) goto out;
	    ok = mocha_ResolveSymbol(mc, &lval, MLF_SET);
	    if (!ok) goto out;
	    if (lval.tag != MOCHA_SYMBOL) {
		if (mocha_RawDatumToString(mc, rval, &atom)) {
		    MOCHA_ReportError(mc, "%s can't be deleted",
				      atom_name(atom));
		    mocha_DropAtom(mc, atom);
		}
		goto out;
	    }
	    Push(mc, lval);
	    PushObject(mc, 0);
#ifdef DEBUG_brendan
	    mc->pc = pc;
#endif
	    ok = Assign(mc, &taint);
	    if (!ok)
		goto out;
	    break;
#endif /* MOCHA_HAS_DELETE_OPERATOR */

	  default:
	    MOCHA_ReportError(mc, "unimplemented Mocha bytecode %d", op);
	    break;
	}

	pc += len;
	mc->taintInfo->accum = taint;

#ifdef DEBUG
	if (mc->tracefp) {
	    int ndefs, n;
	    MochaDatum d;

	    ndefs = cs->ndefs;
	    if (ndefs) {
		for (n = ndefs; n > 0; n--) {
		    d = sp->ptr[-n];
		    if (mocha_RawDatumToString(mc, d, &atom)) {
			fprintf(mc->tracefp, "%s %s",
				(n == ndefs) ? "  output:" : ",",
				atom_name(atom));
			mocha_DropAtom(mc, atom);
		    }
		}
		putc('\n', mc->tracefp);
	    }
	}
#endif
    }

out:
    /*
    ** Pop anything left by an exception on the stack, taking care not to pop
    ** new variables created by eval("var x = ...").
    */
    if (sp->frame) {
	bottom = sp->frame->vars + sp->frame->nvars;
	if (oldtos < bottom)
	    oldtos = bottom;
    }
    while (sp->ptr > oldtos)
	Pop(mc, MOCHA_TRUE);

    /*
    ** Restore the previous frame's execution state.
    */
    mc->staticLink = oldslink;
    mc->pc = oldpc;
    mc->script = oldscript;
    ALLOCA_GC();

    /*
    ** Drop result if there was an error.
    */
    if (!ok) {
	mocha_DropRef(mc, result);
	*result = MOCHA_void;
    }
    return ok;
}
