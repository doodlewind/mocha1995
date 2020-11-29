/*
** Mocha standard library.
**
** Brendan Eich, 10/20/95
*/
#include <string.h>
#include "mo_cntxt.h"
#include "mochaapi.h"
#include "mochalib.h"

static MochaBoolean
mocha_taint(MochaContext *mc, MochaObject *obj,
	    unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    uint16 *taintp;

    /* If no argument was given, mix our taint into our accumulator. */
    *rval = argv[0];
    taintp = (argc == 0) ? &mc->taintInfo->accum : &rval->taint;
    MOCHA_MIX_TAINT(mc, *taintp, mc->taintInfo->taint);
    return MOCHA_TRUE;
}

static MochaBoolean
mocha_untaint(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    uint16 *taintp;

    /* If no argument was given, remove our taint from our accumulator. */
    taintp = (argc == 0) ? &mc->taintInfo->accum : &argv[0].taint;
    if (*taintp == mc->taintInfo->taint) {
	/* Drop the argument's taint so Call won't propagate it to rval. */
	if (argc != 0)
	    (*mocha_DropTaint)(mc, *taintp);
	*taintp = MOCHA_TAINT_IDENTITY;
    }
    *rval = argv[0];
    return MOCHA_TRUE;
}

static MochaFunctionSpec standard_functions[] = {
    {"taint",           mocha_taint,    1},
    {"untaint",         mocha_untaint,  1},
    {0}
};

MochaBoolean
mocha_InitStandardLibrary(MochaContext *mc, MochaObject *obj)
{
    MochaObject *fun_proto, *obj_proto;

    /* Initialize the function class first so constructors can be made. */
    fun_proto = mocha_InitFunctionClass(mc, obj);
    if (!fun_proto)
	return MOCHA_FALSE;

    /* Initialize the object class next so Object.prototype works. */
    obj_proto = mocha_InitObjectClass(mc, obj);
    if (!obj_proto)
	return MOCHA_FALSE;

    /* Link the global object and Function.prototype to Object.prototype. */
    if (!obj->prototype)
	obj->prototype = MOCHA_HoldObject(mc, obj_proto);
    if (!fun_proto->prototype)
	fun_proto->prototype = MOCHA_HoldObject(mc, obj_proto);

    /* Initialize the rest of the standard objects and functions. */
    return MOCHA_DefineFunctions(mc, obj, standard_functions) &&
	   mocha_InitArrayClass(mc, obj) != 0 &&
	   mocha_InitBooleanClass(mc, obj) != 0 &&
	   mocha_InitMathClass(mc, obj) != 0 &&
	   mocha_InitNumberClass(mc, obj) != 0 &&
	   mocha_InitStringClass(mc, obj) != 0 &&
#ifdef JAVA
	   mocha_InitJava(mc, obj) &&
#endif
	   mocha_InitDateClass(mc, obj) != 0;
}
