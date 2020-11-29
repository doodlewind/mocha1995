#ifndef _mo_cntxt_h_
#define _mo_cntxt_h_
/*
** Mocha per-thread data structures and operations.
**
** Brendan Eich, 6/24/95.
*/
#include <stddef.h>
#include "prarena.h"
#include "prclist.h"
#include "prmacros.h"
#include "mo_prvtd.h"
#include "mocha.h"
#include "mochaapi.h"

NSPR_BEGIN_EXTERN_C

/*
** Mocha compile-and-go context.  Contains what would otherwise be library-
** global variables.  Bundling these into a struct enables several threads
** sharing the same address space to compile and execute independent Mocha
** scripts concurrently.
*/
struct MochaContext {
    PRCList                 links;

    /* Allocation arena pools (see mo_atom.c, mo_parse.c, mo_scan.c). */
    PRArenaPool             codePool;
    PRArenaPool             tempPool;

    /* Static and dynamic scope stacks (see mo_cntxt.c, mo_scope.c). */
    MochaObject             *staticLink;
    MochaObjectStack        *objectStack;
 
    /* Top-level object for this context. */
    /* XXX weak link; not necessarily reachable from static link. */
    MochaObject             *globalObject;

    /* Context taint code and current taint accumulator. */
    MochaTaintInfo          *taintInfo;
    MochaTaintInfo          defaultTaintInfo;

    /* Exception pc, script, last message, and trace file. */
    MochaCode               *pc;
    MochaScript             *script;
    char                    *lastMessage;
#ifdef DEBUG
    FILE                    *tracefp;
#endif

    /* Per-context optional user callbacks. */
    MochaBranchCallback     branchCallback;
    MochaErrorReporter      errorReporter;

    /* Java environment to use for java calls */
    void                    *javaEnv;
    void                    *mochaErrors;	/* saved mocha error state */

    /* I18N character filtering. */
    MochaCharFilter         charFilter;
    void                    *charFilterArg;

    /* Stack descriptor and base of stack data array. */
    MochaStack              stack;
    MochaDatum              stackBase[1];	/* NB: must be last */
};

/*
** mocha_NewContext(stackSize) constructs a new Mocha execution context.
** mocha_DestroyContext(mc) destroys mc.
*/
extern MochaContext *
mocha_NewContext(size_t stackSize);

extern void
mocha_DestroyContext(MochaContext *mc);

extern MochaContext *
mocha_ContextIterator(MochaContext **iterp);

extern void
mocha_InitTaintInfo(MochaContext *mc);

/*
** this is a hook for mo_java to clean up its part of the context
*/
extern void
mocha_DestroyJavaContext(MochaContext *mc);

/*
** Report an exception, which is currently realized as a printf-style format
** string and its arguments.
*/
#ifdef va_start
extern void
mocha_ReportErrorVA(MochaContext *mc, const char *format, va_list ap);
#endif

/*
** Report an exception using a previously composed MochaErrorReport.
*/
void
mocha_ReportErrorAgain(MochaContext *mc, const char *message,
                       MochaErrorReport *reportp);

/*
** Push and pop the Mocha context's object stack.
*/
extern MochaBoolean
mocha_PushObject(MochaContext *mc, MochaObject *obj, MochaObjectStack **topp);

extern void
mocha_PopObject(MochaContext *mc, MochaObjectStack *top);

NSPR_END_EXTERN_C

#endif /* _mo_cntxt_h_ */
