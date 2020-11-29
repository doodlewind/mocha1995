/*
** Standard Mocha math functions.
**
** Brendan Eich, 11/15/95
*/
#include <math.h>
#include <stdlib.h>
#include "prlong.h"
#include "prtime.h"
#include "mo_cntxt.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

enum math_slot {
    MATH_E,
    MATH_LOG2E,
    MATH_LOG10E,
    MATH_LN2,
    MATH_LN10,
    MATH_PI,
    MATH_SQRT2,
    MATH_SQRT1_2,
    MATH_MAX
};

#ifndef M_E
#define M_E		2.7182818284590452354
#define M_LOG2E		1.4426950408889634074
#define M_LOG10E	0.43429448190325182765
#define M_LN2		0.69314718055994530942
#define M_LN10		2.30258509299404568402
#define M_PI		3.14159265358979323846
#define M_SQRT2		1.41421356237309504880
#define M_SQRT1_2	0.70710678118654752440
#endif

static MochaBoolean
math_get_property(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		  MochaDatum *dp)
{
    switch (slot) {
      case MATH_E:
	dp->tag = MOCHA_NUMBER;
	dp->u.fval = M_E;
	break;
      case MATH_LOG2E:
	dp->tag = MOCHA_NUMBER;
	dp->u.fval = M_LOG2E;
	break;
      case MATH_LOG10E:
	dp->tag = MOCHA_NUMBER;
	dp->u.fval = M_LOG10E;
	break;
      case MATH_LN2:
	dp->tag = MOCHA_NUMBER;
	dp->u.fval = M_LN2;
	break;
      case MATH_LN10:
	dp->tag = MOCHA_NUMBER;
	dp->u.fval = M_LN10;
	break;
      case MATH_PI:
	dp->tag = MOCHA_NUMBER;
	dp->u.fval = M_PI;
	break;
      case MATH_SQRT2:
	dp->tag = MOCHA_NUMBER;
	dp->u.fval = M_SQRT2;
	break;
      case MATH_SQRT1_2:
	dp->tag = MOCHA_NUMBER;
	dp->u.fval = M_SQRT1_2;
	break;
    }
    return MOCHA_TRUE;
}

static MochaPropertySpec math_static_props[] = {
    {"E",       MATH_E,       MDF_READONLY, math_get_property},
    {"LOG2E",   MATH_LOG2E,   MDF_READONLY, math_get_property},
    {"LOG10E",  MATH_LOG10E,  MDF_READONLY, math_get_property},
    {"LN2",     MATH_LN2,     MDF_READONLY, math_get_property},
    {"LN10",    MATH_LN10,    MDF_READONLY, math_get_property},
    {"PI",      MATH_PI,      MDF_READONLY, math_get_property},
    {"SQRT2",   MATH_SQRT2,   MDF_READONLY, math_get_property},
    {"SQRT1_2", MATH_SQRT1_2, MDF_READONLY, math_get_property},
    {0}
};

static MochaClass math_class = {
    "Math",
    math_get_property, math_get_property, MOCHA_ListPropStub,
    MOCHA_ResolveStub, MOCHA_ConvertStub, MOCHA_FinalizeStub
};

static MochaBoolean
math_abs(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = (x < 0) ? -x : x;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_acos(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = acos(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_asin(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = asin(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_atan(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = atan(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_atan2(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, y, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    if (!mocha_DatumToNumber(mc, argv[1], &y))
	return MOCHA_FALSE;
    z = atan2(x, y);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_ceil(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = ceil(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_cos(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = cos(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_exp(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = exp(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_floor(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = floor(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_log(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = log(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_max(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, y, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    if (!mocha_DatumToNumber(mc, argv[1], &y))
	return MOCHA_FALSE;
    z = (x > y) ? x : y;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_min(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, y, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    if (!mocha_DatumToNumber(mc, argv[1], &y))
	return MOCHA_FALSE;
    z = (x < y) ? x : y;
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_pow(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, y, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    if (!mocha_DatumToNumber(mc, argv[1], &y))
	return MOCHA_FALSE;
    z = pow(x, y);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

/*
** Math.random() support, lifted from classsrc/java/util/Random.java in the
** ns/sun-java tree.
*/
static MochaBoolean random_initialized;
static int64        random_multiplier;
static int64        random_addend;
static int64        random_mask;
static int64        random_seed;
static MochaFloat   random_dscale;

static void
random_setSeed(int64 seed)
{
    int64 tmp;

    LL_I2L(tmp, 1000);
    LL_DIV(seed, seed, tmp);
    LL_XOR(tmp, seed, random_multiplier);
    LL_AND(random_seed, tmp, random_mask);
}

static void
random_init(void)
{
    int64 tmp, tmp2;

    /* Do at most once. */
    if (random_initialized)
	return;
    random_initialized = MOCHA_TRUE;

    /* random_multiplier = 0x5DEECE66DL */
    LL_ISHL(tmp, 0x5D, 32);
    LL_UI2L(tmp2, 0xEECE66DL);
    LL_OR(random_multiplier, tmp, tmp2);

    /* random_addend = 0xBL */
    LL_I2L(random_addend, 0xBL);

    /* random_mask = (1L << 48) - 1 */
    LL_I2L(tmp, 1);
    LL_SHL(tmp2, tmp, 48);
    LL_SUB(random_mask, tmp2, tmp);

    /* random_dscale = (MochaFloat)(1L << 54) */
    LL_SHL(tmp2, tmp, 54);
    LL_L2D(random_dscale, tmp2);

    /* Finally, set the seed from current time. */
    random_setSeed(PR_Now());
}

static uint32
random_next(int bits)
{
    int64 nextseed, tmp;
    uint32 retval;

    LL_MUL(nextseed, random_seed, random_multiplier);
    LL_ADD(nextseed, nextseed, random_addend);
    LL_AND(nextseed, nextseed, random_mask);
    random_seed = nextseed;
    LL_USHR(tmp, nextseed, 48 - bits);
    LL_L2I(retval, tmp);
    return retval;
}

static MochaFloat
random_nextDouble(void)
{
    int64 tmp, tmp2;
    MochaFloat fval;

    LL_ISHL(tmp, random_next(27), 27);
    LL_UI2L(tmp2, random_next(27));
    LL_ADD(tmp, tmp, tmp2);
    LL_L2D(fval, tmp);
    return fval / random_dscale;
}

static MochaBoolean
math_random(MochaContext *mc, MochaObject *obj,
	    unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat z;

    random_init();
    z = random_nextDouble();
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_round(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = floor(x + 0.5);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_sin(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = sin(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_sqrt(MochaContext *mc, MochaObject *obj,
	  unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = sqrt(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaBoolean
math_tan(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaFloat x, z;

    if (!mocha_DatumToNumber(mc, argv[0], &x))
	return MOCHA_FALSE;
    z = tan(x);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, z);
    return MOCHA_TRUE;
}

static MochaFunctionSpec math_static_methods[] = {
    {"abs",		math_abs,		1},
    {"acos",		math_acos,		1},
    {"asin",		math_asin,		1},
    {"atan",		math_atan,		1},
    {"atan2",		math_atan2,		2},
    {"ceil",		math_ceil,		1},
    {"cos",		math_cos,		1},
    {"exp",		math_exp,		1},
    {"floor",		math_floor,		1},
    {"log",		math_log,		1},
    {"max",		math_max,		2},
    {"min",		math_min,		2},
    {"pow",		math_pow,		2},
    {"random",		math_random,		0},
    {"round",		math_round,		1},
    {"sin",		math_sin,		1},
    {"sqrt",		math_sqrt,		1},
    {"tan",		math_tan,		1},
    {0}
};

static MochaBoolean
Math(MochaContext *mc, MochaObject *obj,
     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    return MOCHA_TRUE;
}

MochaObject *
mocha_InitMathClass(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_InitClass(mc, obj, &math_class, 0, Math, 0,
			   0, 0, math_static_props, math_static_methods);
}
