/*
** Mocha number class.
**
** Brendan Eich, 10/20/95
*/
#if defined IRIX ||                                                           \
    (defined SunOS && defined SVR4) ||                                        \
    defined SCO ||                                                            \
    defined UNIXWARE
#define IEEEFP
#endif
#ifdef IEEEFP
#include <ieeefp.h>
#endif
#ifdef XP_PC
#include <float.h>
#endif
#include <limits.h>
#include <math.h>	/* for strtod() old-style declaration on SunOS4 */
#include <stdlib.h>
#include <string.h>
#include "prprf.h"
#include "prdtoa.h"
#include "mo_atom.h"
#include "mo_cntxt.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

MochaBoolean
mocha_RawDatumToNumber(MochaContext *mc, MochaDatum d, MochaFloat *fvalp)
{
    MochaDatum rval;
    MochaFloat fval;
    const char *str;
    char *endptr;
    MochaObject *obj;

    switch (d.tag) {
      case MOCHA_NUMBER:
	*fvalp = d.u.fval;
	return MOCHA_TRUE;
      case MOCHA_BOOLEAN:
	*fvalp = d.u.bval ? 1 : 0;
	return MOCHA_TRUE;
      case MOCHA_STRING:
	str = atom_name(d.u.atom);
	fval = PR_strtod(str, &endptr);
	if (*endptr != '\0')
	    return MOCHA_FALSE;
	*fvalp = fval;
	return MOCHA_TRUE;
      case MOCHA_OBJECT:
	obj = d.u.obj;
	if (!obj) {
	    *fvalp = 0;
	    return MOCHA_TRUE;
	}
	rval = MOCHA_void;
	if (OBJ_CONVERT(mc, obj, MOCHA_NUMBER, &rval) &&
	    rval.tag == MOCHA_NUMBER) {
	    *fvalp = rval.u.fval;
	    MOCHA_MIX_TAINT(mc, mc->taintInfo->accum, rval.taint);
	    return MOCHA_TRUE;
	}
	/* FALL THROUGH */
      default:
	return MOCHA_FALSE;
    }
}

MochaBoolean
mocha_DatumToNumber(MochaContext *mc, MochaDatum d, MochaFloat *fvalp)
{
    MochaDatum aval;
    MochaAtom *atom;

    aval = d;
    if (!mocha_ResolvePrimitiveValue(mc, &d))
	return MOCHA_FALSE;
    if (!mocha_RawDatumToNumber(mc, d, fvalp)) {
	if (mocha_RawDatumToString(mc, aval, &atom)) {
	    MOCHA_ReportError(mc, "%s is not a number", atom_name(atom));
	    mocha_DropAtom(mc, atom);
	}
	return MOCHA_FALSE;
    }
    return MOCHA_TRUE;
}

static MochaAtom *
number_to_atom(MochaContext *mc, MochaFloat fval)
{
    MochaInt ival;
    char buf[50];
    MochaAtom *atom;

    ival = (MochaInt)fval;
    if (!MOCHA_FLOAT_IS_NaN(fval) && (MochaFloat)ival == fval)
	PR_snprintf(buf, sizeof buf, "%ld", (long)ival);
    else
	PR_cnvtf(buf, sizeof buf, 20, fval);
    atom = mocha_Atomize(mc, buf, ATOM_NUMBER);
    if (atom)
	atom->fval = fval;
    return atom;
}

enum num_slot {
    NUM_POSITIVE_INFINITY = -1,
    NUM_NEGATIVE_INFINITY = -2,
    NUM_NaN               = -3,
    NUM_MAX_VALUE         = -4,
    NUM_MIN_VALUE         = -5
};

#ifndef DBL_MAX
#define	DBL_MAX		1.7976931348623157E+308
#endif
#ifndef DBL_MIN
#define	DBL_MIN		2.2250738585072014E-308
#endif

static MochaBoolean
num_get_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		 MochaDatum *dp)
{
    switch (slot) {
      case NUM_POSITIVE_INFINITY:
#ifndef XP_PC
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval, 1.0 / 0);
#endif
	break;
      case NUM_NEGATIVE_INFINITY:
#ifndef XP_PC
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval, -1.0 / 0);
#endif
	break;
      case NUM_NaN:
	*dp = MOCHA_NaN;
	break;
      case NUM_MAX_VALUE:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval, DBL_MAX);
	break;
      case NUM_MIN_VALUE:
	MOCHA_INIT_DATUM(mc, dp, MOCHA_NUMBER, u.fval, DBL_MIN);
	break;
      default:;
    }
    return MOCHA_TRUE;
}

static MochaPropertySpec number_static_props[] = {
    {"POSITIVE_INFINITY",   NUM_POSITIVE_INFINITY, MDF_READONLY,
			    num_get_property,      num_get_property},
    {"NEGATIVE_INFINITY",   NUM_NEGATIVE_INFINITY, MDF_READONLY,
			    num_get_property,      num_get_property},
    {"NaN",                 NUM_NaN,               MDF_READONLY,
			    num_get_property,      num_get_property},
    {"MAX_VALUE",           NUM_MAX_VALUE,         MDF_READONLY,
			    num_get_property,      num_get_property},
    {"MIN_VALUE",           NUM_MIN_VALUE,         MDF_READONLY,
			    num_get_property,      num_get_property},
    {0}
};

static void
num_finalize(MochaContext *mc, MochaObject *obj)
{
    MochaAtom *atom;

    atom = obj->data;
    mocha_DropAtom(mc, atom);
}

static MochaClass number_class = {
    "Number",
    MOCHA_PropertyStub, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub, MOCHA_ConvertStub, num_finalize
};

static MochaBoolean
num_to_string(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    if (!MOCHA_InstanceOf(mc, obj, &number_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    if (argc >= 1) {
	MochaFloat radix;
	MochaInt base, ival, dval;
	char *bp, buf[32];

	if (!mocha_DatumToNumber(mc, argv[0], &radix))
	    return MOCHA_FALSE;
	base = (MochaInt)radix;
	if (base <= 1 || base > 16) {
	    MOCHA_ReportError(mc, "illegal radix %d", base);
	    return MOCHA_FALSE;
	}
	ival = (MochaInt)atom->fval;
	bp = buf + sizeof buf;
	for (*--bp = '\0'; ival != 0 && --bp >= buf; ival /= base) {
	    dval = ival % base;
	    *bp = (dval > 10) ? 'a' - 10 + dval : '0' + dval;
	}
	if (*bp == '\0')
	    *--bp = '0';
	atom = mocha_Atomize(mc, bp, MOCHA_STRING);
	if (!atom)
	    return MOCHA_FALSE;
    }
    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    return MOCHA_TRUE;
}

static MochaBoolean
num_value_of(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;

    if (!MOCHA_InstanceOf(mc, obj, &number_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    atom = obj->data;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, atom->fval);
    return MOCHA_TRUE;
}

static MochaFunctionSpec number_methods[] = {
    {mocha_toStringStr,		num_to_string,		0},
    {mocha_valueOfStr,		num_value_of,		0},
    {0}
};

static MochaBoolean
Number(MochaContext *mc, MochaObject *obj,
       unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat fval;
    MochaAtom *atom;

    if (argc != 0) {
	if (!mocha_DatumToNumber(mc, argv[0], &fval))
	    return MOCHA_FALSE;
    } else {
	fval = MOCHA_zero.u.fval;
    }
    if (obj->clazz != &number_class) {
	MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, fval);
	return MOCHA_TRUE;
    }
    atom = number_to_atom(mc, fval);
    if (!atom)
	return MOCHA_FALSE;
    obj->data = mocha_HoldAtom(mc, atom);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_OBJECT, u.obj, obj);
    return MOCHA_TRUE;
}

#ifndef IEEEFP
#define isnand(x)	MOCHA_FLOAT_IS_NaN(x)
#endif

#ifdef IEEEFP
MochaBoolean
num_is_finite(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    double x;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_BOOLEAN, u.bval, finite(x));
    return MOCHA_TRUE;
}

MochaBoolean
num_is_infinite(MochaContext *mc, MochaObject *obj,
		unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    double x;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_BOOLEAN, u.bval, !isnand(x) && !finite(x));
    return MOCHA_TRUE;
}

MochaBoolean
num_unordered(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    double x, y;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    if (!mocha_DatumToNumber(mc, argv[1], &y))
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_BOOLEAN, u.bval, unordered(x, y));
    return MOCHA_TRUE;
}
#endif /* IEEEFP */

MochaBoolean
num_is_nan(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    double x;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_BOOLEAN, u.bval, isnand(x));
    return MOCHA_TRUE;
}

MochaBoolean
num_parse_float(MochaContext *mc, MochaObject *obj,
		unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;
    const char *str;
    char *endptr;
    MochaFloat value;

    if (!mocha_DatumToString(mc, argv[0], &atom))
	return MOCHA_FALSE;
    str = atom_name(atom);
    value = PR_strtod(str, &endptr);
    if (value == 0 && str == endptr)
	value = MOCHA_NaN.u.fval;
    mocha_DropAtom(mc, atom);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, value);
    return MOCHA_TRUE;
}

MochaBoolean
num_parse_int(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;
    const char *str;
    char *endptr;
    int radix;
    MochaFloat value;

    if (!mocha_DatumToString(mc, argv[0], &atom))
	return MOCHA_FALSE;
    if (argc > 1) {
	if (!mocha_DatumToNumber(mc, argv[1], &value)) {
	    mocha_DropAtom(mc, atom);
	    return MOCHA_FALSE;
	}
	radix = (int)value;
    } else {
	radix = 0;
    }
    str = atom_name(atom);
    value = (MochaFloat)strtol(str, &endptr, radix);
    if (value == 0 && str == endptr)
	value = MOCHA_NaN.u.fval;
    mocha_DropAtom(mc, atom);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, value);
    return MOCHA_TRUE;
}

static MochaFunctionSpec number_functions[] = {
#ifdef IEEEFP
    {"isFinite",	num_is_finite,		1},
    {"isInfinite",	num_is_infinite,	1},
    {"unordered",	num_unordered,		2},
#endif /* IEEEFP */
    {"isNaN",		num_is_nan,		1},
    {"parseFloat",	num_parse_float,	1},
    {"parseInt",	num_parse_int,		2},
    {0}
};

MochaObject *
mocha_InitNumberClass(MochaContext *mc, MochaObject *obj)
{
#ifdef XP_PC
    union {
    	struct {
	    uint32 lo, hi;	/* XXX little-endian ONLY */
    	} s;
    	MochaFloat f;
    } u;

    unsigned int old = _control87(MCW_EM, MCW_EM); 

    u.s.hi = 0x7fffffff;	/* quiet NaN */
    u.s.lo = 0xffffffff;
    MOCHA_NaN.u.fval = u.f;
#else
    MOCHA_NaN.u.fval = 0.0 / 0.0;
#endif

    if (!MOCHA_DefineFunctions(mc, obj, number_functions))
	return MOCHA_FALSE;

    return MOCHA_InitClass(mc, obj, &number_class, 0, Number, 1,
			   0, number_methods, number_static_props, 0);
}

MochaBoolean
mocha_NumberToString(MochaContext *mc, MochaFloat fval, MochaAtom **atomp)
{
    MochaAtom *atom;

    atom = number_to_atom(mc, fval);
    if (!atom)
	return MOCHA_FALSE;
    *atomp = mocha_HoldAtom(mc, atom);
    return MOCHA_TRUE;
}

MochaObject *
mocha_NumberToObject(MochaContext *mc, MochaFloat fval)
{
    MochaObject *obj;
    MochaAtom *atom;

    obj = mocha_NewObjectByClass(mc, &number_class);
    if (!obj)
	return 0;
    atom = number_to_atom(mc, fval);
    if (!atom) {
	mocha_DestroyObject(mc, obj);
	return 0;
    }
    obj->data = mocha_HoldAtom(mc, atom);
    return obj;
}
