/*
** Mocha Boolean class.
**
** Brendan Eich, 10/20/95
*/
#include "mo_cntxt.h"
#include "mochaapi.h"
#include "mochalib.h"

MochaBoolean
mocha_BooleanToString(MochaContext *mc, MochaBoolean bval, MochaAtom **atomp)
{
    *atomp = mocha_HoldAtom(mc, mocha_booleanAtoms[bval ? 1 : 0]);
    return MOCHA_TRUE;
}

MochaBoolean
mocha_DatumToBoolean(MochaContext *mc, MochaDatum d, MochaBoolean *bvalp)
{
    MochaDatum aval, rval;
    MochaObject *obj;

    aval = d;
    if (!mocha_ResolvePrimitiveValue(mc, &d))
	return MOCHA_FALSE;
    switch (d.tag) {
      case MOCHA_INTERNAL:
	*bvalp = d.u.ptr ? MOCHA_TRUE : MOCHA_FALSE;
	return MOCHA_TRUE;
      case MOCHA_FUNCTION:
      case MOCHA_OBJECT:
	obj = d.u.obj;
	if (!obj) {
	    *bvalp = MOCHA_FALSE;
	} else {
	    rval = MOCHA_void;
	    if (!OBJ_CONVERT(mc, obj, MOCHA_BOOLEAN, &rval))
		return MOCHA_FALSE;
	    *bvalp = (rval.tag == MOCHA_BOOLEAN) ? rval.u.bval : MOCHA_TRUE;
	    MOCHA_MIX_TAINT(mc, mc->taintInfo->accum, rval.taint);
	}
	return MOCHA_TRUE;
      case MOCHA_NUMBER:
	*bvalp = d.u.fval ? MOCHA_TRUE : MOCHA_FALSE;
	return MOCHA_TRUE;
      case MOCHA_BOOLEAN:
	*bvalp = d.u.bval;
	return MOCHA_TRUE;
      case MOCHA_STRING:
	*bvalp = d.u.atom->length ? MOCHA_TRUE : MOCHA_FALSE;
	return MOCHA_TRUE;
      default:
	*bvalp = MOCHA_FALSE;
	return MOCHA_TRUE;
    }
}

static void
bool_finalize(MochaContext *mc, MochaObject *obj)
{
    MochaAtom *atom;

    atom = obj->data;
    mocha_DropAtom(mc, atom);
}

static MochaClass boolean_class = {
    "Boolean",
    MOCHA_PropertyStub, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub, MOCHA_ConvertStub, bool_finalize
};

static MochaBoolean
bool_to_string(MochaContext *mc, MochaObject *obj,
	       unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    if (!MOCHA_InstanceOf(mc, obj, &boolean_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
bool_value_of(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    if (!MOCHA_InstanceOf(mc, obj, &boolean_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_BOOLEAN, u.bval, atom->fval != 0);
    return MOCHA_TRUE;
}

static MochaFunctionSpec boolean_methods[] = {
    {mocha_toStringStr,	bool_to_string,		0},
    {mocha_valueOfStr,	bool_value_of,		0},
    {0}
};

#ifdef XP_MAC
#undef Boolean
#define Boolean metroworks_compiled_headers_suck_Boolean
#endif

static MochaBoolean
Boolean(MochaContext *mc, MochaObject *obj,
	unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaBoolean bval;
    MochaAtom *atom;

    if (argc != 0) {
	if (!mocha_DatumToBoolean(mc, argv[0], &bval))
	    return MOCHA_FALSE;
    } else {
	bval = MOCHA_false.u.bval;
    }
    if (obj->clazz != &boolean_class) {
	MOCHA_INIT_DATUM(mc, rval, MOCHA_BOOLEAN, u.bval, bval);
	return MOCHA_TRUE;
    }
    atom = mocha_booleanAtoms[(bval == MOCHA_TRUE) ? 1 : 0];
    obj->data = mocha_HoldAtom(mc, atom);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, obj);
    return MOCHA_TRUE;
}

MochaObject *
mocha_InitBooleanClass(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_InitClass(mc, obj, &boolean_class, 0, Boolean, 1,
			   0, boolean_methods, 0, 0);
}

MochaObject *
mocha_BooleanToObject(MochaContext *mc, MochaBoolean bval)
{
    MochaObject *obj;
    MochaAtom *atom;

    obj = mocha_NewObjectByClass(mc, &boolean_class);
    if (!obj)
	return 0;
    atom = mocha_booleanAtoms[(bval == MOCHA_TRUE) ? 1 : 0];
    obj->data = mocha_HoldAtom(mc, atom);
    return obj;
}
