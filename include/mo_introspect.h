/*
** Mocha introspection hook.
**
** A minimal, always-compiled instrumentation point for the bytecode
** interpreter, used by the web playground to capture a structured
** execution trace.  When `mocha_TraceHook` is non-null, the interpreter
** calls it once per bytecode instruction, just before the instruction is
** dispatched.  This lets an embedder snapshot the program counter, the
** value stack and the active call frame without patching the interpreter
** beyond a single call.
*/
#ifndef mo_introspect_h___
#define mo_introspect_h___

#include "mo_pubtd.h"
#include "mocha.h"

typedef void (*MochaTraceHook)(MochaContext *mc, MochaScript *script,
                               MochaCode *pc, struct MochaStack *sp);

extern MochaTraceHook mocha_TraceHook;

/*
** Compile and run `source`, returning a freshly malloc'd JSON string that
** describes the token stream, the disassembled bytecode of every executed
** script, a per-instruction execution trace and the program output.
** The caller owns the returned string and must free() it.
*/
extern char *mocha_run_json(const char *source);

#endif /* mo_introspect_h___ */
