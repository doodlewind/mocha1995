#ifndef _mo_java_h_
#define _mo_java_h_

/*
** Java <-> JavaScript reflection
*/

#ifdef JAVA

#include "mochaapi.h"

/* XXX change everything to JRI style */
#ifndef _OOBJ_H_
typedef struct HObject HObject;
typedef struct ClassClass ClassClass;
typedef struct ExecEnv ExecEnv;
#endif

/* initialization */
extern MochaBoolean MOCHA_JavaGlueEnabled(void);

extern MochaObject *
MOCHA_ReflectJObjectToMObject(MochaContext *mc, HObject *jo);
extern MochaObject *
MOCHA_ReflectJClassToMObject(MochaContext *mc, ClassClass *clazz);
extern HObject *
MOCHA_ReflectMObjectToJObject(MochaContext *mc, MochaObject *mo);

/* internal, exported for use by JSObject native methods */
extern MochaBoolean
mocha_convertMDatumToJObject(HObject **objp, MochaContext *mc,
                             MochaDatum *arg, char *sig, ClassClass *fromClass,
                             MochaBoolean checkOnly, int *cost);
extern MochaBoolean
mocha_convertJObjectToMDatum(MochaContext *mc, MochaDatum *dp,
			     HObject *jo);

/* this mocha error reporter saves the error for MErrorToJException */
extern void
mocha_js_ErrorReporter(MochaContext *mc, const char *message,
                       MochaErrorReport *error);
/* if there was an error in mocha, turn it into a JSException */
extern void
mocha_MErrorToJException(MochaContext *mc, ExecEnv *ee);

extern MochaBoolean mocha_isFunction(MochaObject *mo);
extern void mocha_removeReflection(MochaObject *mo);

#endif /* defined(JAVA) */


#endif /* _mo_java_h_ */
