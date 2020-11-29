#ifndef _mochalib_h_
#define _mochalib_h_
/*
** Mocha API.
**
** Brendan Eich, 10/20/95
*/
#include "prmacros.h"
#include "mochaapi.h"

NSPR_BEGIN_EXTERN_C

extern MochaBoolean
mocha_InitStandardLibrary(MochaContext *mc, MochaObject *obj);

/* XXX begin move me to mo_math.h */
extern MochaObject *
mocha_InitMathClass(MochaContext *mc, MochaObject *obj);
/* XXX end move me to mo_math.h */

/* XXX begin move me to mo_fun.h */
/*
** Function class declarations.
*/
extern MochaBoolean
mocha_FunctionToString(MochaContext *mc, MochaFunction *fun, MochaAtom **atomp);

extern MochaBoolean
mocha_DatumToFunction(MochaContext *mc, MochaDatum d, MochaFunction **funp);

extern MochaObject *
mocha_InitFunctionClass(MochaContext *mc, MochaObject *obj);

extern MochaObject *
mocha_FunctionToObject(MochaContext *mc, MochaFunction *fun);

extern MochaBoolean
mocha_InitFunctionObject(MochaObject *funobj, MochaFunction *fun);
/* XXX end move me to mo_fun.h */

/* XXX begin move me to mo_obj.h */
/*
** Object type and class operations.
*/
extern MochaClass mocha_ObjectClass;

extern MochaBoolean
mocha_DatumToObject(MochaContext *mc, MochaDatum d, MochaObject **objp);

extern MochaBoolean
mocha_RawObjectToString(MochaContext *mc, MochaObject *obj, MochaAtom **atomp);

extern MochaBoolean
mocha_ObjectToString(MochaContext *mc, MochaObject *obj, MochaAtom **atomp);

extern MochaBoolean
mocha_TryMethod(MochaContext *mc, MochaObject *obj, MochaAtom *atom,
		unsigned argc, MochaDatum *argv, MochaDatum *rval);

extern MochaObject *
mocha_InitObjectClass(MochaContext *mc, MochaObject *obj);

extern MochaBoolean
mocha_GetPrototype(MochaContext *mc, MochaClass *clazz, MochaObject **objp);

extern MochaBoolean
mocha_SetPrototype(MochaContext *mc, MochaFunction *fun, MochaObject *obj);

extern MochaBoolean
mocha_GetMutableScope(MochaContext *mc, MochaObject *obj);

extern MochaObject *
mocha_NewObjectByClass(MochaContext *mc, MochaClass *clazz);

extern MochaObject *
mocha_NewObjectByPrototype(MochaContext *mc, MochaObject *prototype);
/* XXX end move me to mo_obj.h */

/* XXX begin move me to mo_num.h */
/*
** Number type and class declarations.
*/
extern MochaBoolean
mocha_NumberToString(MochaContext *mc, MochaFloat fval, MochaAtom **atomp);

extern MochaBoolean
mocha_RawDatumToNumber(MochaContext *mc, MochaDatum d, MochaFloat *fvalp);

extern MochaBoolean
mocha_DatumToNumber(MochaContext *mc, MochaDatum d, MochaFloat *fvalp);

extern MochaObject *
mocha_InitNumberClass(MochaContext *mc, MochaObject *obj);

extern MochaObject *
mocha_NumberToObject(MochaContext *mc, MochaFloat fval);
/* XXX end move me to mo_num.h */

/* XXX begin move me to mo_bool.h */
/*
** Boolean type and class declarations.
*/
extern MochaBoolean
mocha_BooleanToString(MochaContext *mc, MochaBoolean bval, MochaAtom **atomp);

extern MochaBoolean
mocha_DatumToBoolean(MochaContext *mc, MochaDatum d, MochaBoolean *bvalp);

extern MochaObject *
mocha_InitBooleanClass(MochaContext *mc, MochaObject *obj);

extern MochaObject *
mocha_BooleanToObject(MochaContext *mc, MochaBoolean bval);
/* XXX end move me to mo_bool.h */

/* XXX begin move me to mo_str.h */
/*
** String type and class declarations.
*/
extern MochaBoolean
mocha_RawDatumToString(MochaContext *mc, MochaDatum d, MochaAtom **atomp);

extern MochaBoolean
mocha_DatumToString(MochaContext *mc, MochaDatum d, MochaAtom **atomp);

extern MochaObject *
mocha_InitStringClass(MochaContext *mc, MochaObject *obj);

extern MochaObject *
mocha_StringToObject(MochaContext *mc, MochaAtom *atom);
/* XXX end move me to mo_str.h */

/* XXX begin move me to mo_date.h */
/*
** Mocha Date class initializer.
*/
extern MochaObject *
mocha_InitDateClass(MochaContext *mc, MochaObject *obj);
/* XXX end move me to mo_date.h */

/* XXX begin move me to mo_array.h */
/*
** Array class declarations.
*/
extern MochaObject *
mocha_InitArrayClass(MochaContext *mc, MochaObject *obj);

extern MochaObject *
mocha_NewArrayObject(MochaContext *mc, unsigned length, MochaDatum *base);
/* XXX end move me to mo_array.h */

/*
** Java glue declaration
*/
#ifdef JAVA
extern MochaBoolean
mocha_InitJava(MochaContext *mc, MochaObject *obj);
#endif

NSPR_END_EXTERN_C

#endif /* _mochalib_h_ */
