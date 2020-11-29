#ifndef _mo_prvtd_h_
#define _mo_prvtd_h_
/*
** Mocha private type definitions.
**
** This header is included only in other .h files, for convenience and for
** simplicity of type naming.  The alternative for structures is to use tags,
** which I have named the same as their typedef names (legal in C, and less
** noisy than suffixing the typedef name with "Struct" or "Str").  Instead,
** all .h files that include this file may use the same typename, whether
** declaring a pointer to struct type, or defining a member of struct type.
**
** A few fundamental scalar types are defined here too.  Neither the scalar
** nor the struct typedefs should change much, therefore the nearly-global
** make dependency induced by this file should not prove burdensome.
**
** Brendan Eich, 6/14/95
*/

#include "mo_pubtd.h"

/* Private typedefs. */
typedef struct CodeGenerator    CodeGenerator;
typedef uint16                  SourceNote;
typedef struct MochaToken       MochaToken;
typedef struct MochaTokenStream MochaTokenStream;

/* Friend "Advanced API" typedefs. */
typedef int32                   MochaInt;
typedef uint32                  MochaUint;

#define MOCHA_INT_MAX           (((MochaUint)1 << 31) - 1)
#define MOCHA_INT_MIN           ((MochaInt)1 << 31)

typedef struct MochaAtomMap     MochaAtomMap;
typedef uint32                  MochaAtomNumber;
typedef struct MochaAtomState   MochaAtomState;
typedef struct MochaCodeSpec    MochaCodeSpec;
typedef struct MochaObjectStack MochaObjectStack;
typedef struct MochaPrinter     MochaPrinter;
typedef struct MochaProperty    MochaProperty;
typedef struct MochaStackFrame  MochaStackFrame;
typedef struct MochaStack       MochaStack;

#endif /* _mo_prvtd_h_ */
