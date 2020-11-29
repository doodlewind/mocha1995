#ifndef _mo_pubtd_h_
#define _mo_pubtd_h_
/*
** Public Mocha API typedefs.
**
** This file currently depends on NSPR's prtypes.h for uint8, etc.
**
** Brendan Eich, 8/21/95
*/
#include "prtypes.h"

typedef struct MochaAtom         MochaAtom;
typedef struct MochaClass        MochaClass;
typedef uint8                    MochaCode;
typedef struct MochaContext      MochaContext;
typedef struct MochaDatum        MochaDatum;
typedef struct MochaErrorReport  MochaErrorReport;
typedef double                   MochaFloat;
typedef struct MochaFunction     MochaFunction;
typedef struct MochaFunctionSpec MochaFunctionSpec;
typedef struct MochaPropertySpec MochaPropertySpec;
typedef struct MochaObject       MochaObject;
typedef int32                    MochaRefCount;
typedef struct MochaScope        MochaScope;
typedef struct MochaScript       MochaScript;
typedef int32                    MochaSlot;
typedef struct MochaSymbol       MochaSymbol;
typedef struct MochaTaintInfo    MochaTaintInfo;

/* Special reference count value for objects being finalized. */
#define MOCHA_FINALIZING	((MochaRefCount)0xdeadbeef)

/* Mocha Boolean enumerated type. */
typedef PRBool MochaBoolean;
#define MOCHA_FALSE PR_FALSE
#define MOCHA_TRUE  PR_TRUE

/* Typedefs for user-supplied functions called by the Mocha VM. */
typedef MochaBoolean
(*MochaPropertyOp)(MochaContext *mc, MochaObject *obj, MochaSlot slot,
		   MochaDatum *dp);

typedef MochaBoolean
(*MochaNativeCall)(MochaContext *mc, MochaObject *obj,
		   unsigned argc, MochaDatum *argv, MochaDatum *rval);

typedef MochaBoolean
(*MochaBranchCallback)(MochaContext *mc, MochaScript *script);

typedef void
(*MochaErrorReporter)(MochaContext *mc, const char *message,
		      MochaErrorReport *report);

typedef void
(*MochaTaintCounter)(MochaContext *mc, uint16 taint);

typedef uint16
(*MochaTaintMixer)(MochaContext *mc, uint16 accum, uint16 taint);

#endif /* _mo_pubtd_h_ */
