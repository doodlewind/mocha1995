#ifndef _mocha_h_
#define _mocha_h_
/*
** Mocha virtual machine definitions.
**
** Brendan Eich, 6/17/95.
*/
#include "prmacros.h"
#include "mo_atom.h"
#include "mo_prvtd.h"
#include "mo_pubtd.h"
#include "mo_scope.h"

NSPR_BEGIN_EXTERN_C

/*
** A descriptor for Mocha compiled bytecode.
** XXX fns live in mo_emit.[ch]
*/
struct MochaScript {
    MochaCode           *code;          /* Mocha bytecode */
    unsigned            length;         /* code length in bytes */
    MochaAtomMap        atomMap;        /* maps immediate index to literal */
    unsigned            depth;          /* maximum stack depth */
    char                *filename;      /* source filename or null */
    unsigned            lineno;         /* base line number of script */
    void                *notes;         /* decompiling source notes */
    MochaSymbol         *args;          /* formal argument symbols */
};

/*
** Mocha function descriptor, extends MochaObject.
*/
struct MochaFunction {
    MochaObject         object;         /* base class state */
    MochaNativeCall     call;           /* if non-null, native function ptr. */
    uint8               nargs;          /* minimum number of arguments */
    PRPackedBool        bound;          /* is a method bound to its parent */
    uint16              spare;          /* reserved for future use */
    MochaAtom           *atom;          /* held name atom for diagnostics */
    MochaScript         *script;        /* Mocha bytecode */
};

/*
** Mocha stack frame, allocated on the C runtime stack, one per native or
** interpreted Mocha function activation.
*/
struct MochaStackFrame {
    MochaFunction       *fun;           /* function being called */
    MochaObject         *thisp;         /* "this" pointer if in method */
    unsigned            argc;           /* actual argument count */
    MochaDatum          *argv;          /* base of argument stack slots */
    unsigned            nvars;          /* local variable count */
    MochaDatum          *vars;          /* base of variable stack slots */
    MochaStackFrame     *down;          /* previous frame */
    MochaDatum          rval;           /* function return value */
};

/*
** Mocha uses a single fixed-size array of MochaDatum structs for its stack,
** to keep things simple and fast.
*/
struct MochaStack {
    MochaDatum          *base;          /* lowest address in stack */
    MochaDatum          *limit;         /* one beyond highest byte address */
    MochaStackFrame     *frame;         /* current frame pointer */
    MochaDatum          *ptr;           /* one beyond top of stack */
};

#define MOCHA_INIT_STACK(sp, space, nbytes)                                   \
    NSPR_BEGIN_MACRO                                                          \
	(sp)->base = (sp)->ptr = (MochaDatum *)(space);                       \
	(sp)->limit = (MochaDatum *)((char *)(space) + (nbytes));             \
	(sp)->frame = 0;                                                      \
    NSPR_END_MACRO

extern MochaTaintCounter mocha_HoldTaint;
extern MochaTaintCounter mocha_DropTaint;

/*
** Hold and release atom and object references from a stack datum, a global
** variable, a property, or a stack frame's return value datum.
*/
extern void
mocha_HoldRef(MochaContext *mc, MochaDatum *dp);

extern void
mocha_DropRef(MochaContext *mc, MochaDatum *dp);

/*
** Resolve the variable or property described by sym into the address of its
** MochaDatum storage.  Return null if sym can't be varied.
*/
extern MochaDatum *
mocha_ResolveVariable(MochaContext *mc, MochaSymbol *sym);

/*
** Try to resolve *dp from an unknown (MOCHA_ATOM) or an lvalue (MOCHA_SYMBOL)
** into an lvalue.  Return false on error, true with dp->tag telling whether
** the atom was resolved to a symbol.
*/
extern MochaBoolean
mocha_ResolveSymbol(MochaContext *mc, MochaDatum *dp, MochaLookupFlag flag);

/*
** Try to resolve *dp from an unknown (MOCHA_ATOM) or an lvalue (MOCHA_SYMBOL)
** into an rvalue.  Return true normally, false on exception.
*/
extern MochaBoolean
mocha_ResolveValue(MochaContext *mc, MochaDatum *dp);

/*
** Try to resolve *dp to an rvalue, and then, if the result is an object ref,
** to a primitive type value by calling the valueOf() method.
*/
extern MochaBoolean
mocha_ResolvePrimitiveValue(MochaContext *mc, MochaDatum *dp);

/*
** Call the function described by fd with the given arguments.
*/
extern MochaBoolean
mocha_Call(MochaContext *mc, MochaDatum fd,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval);

/*
** Return an atom naming the type of d.
*/
extern MochaBoolean
mocha_TypeOfDatum(MochaContext *mc, MochaDatum d, MochaAtom **atomp);

/*
** Interpret script in the given context and static link.
*/
extern MochaBoolean
mocha_Interpret(MochaContext *mc, MochaObject *slink, MochaScript *script,
		MochaDatum *result);

NSPR_END_EXTERN_C

#endif /* _mocha_h_ */
