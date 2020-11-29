/*
** Mocha per-thread context and context operations.
**
** Brendan Eich, 6/24/95.
*/
#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "prarena.h"
#include "prlog.h"
#include "prmem.h"
#include "prprf.h"
#include "mo_atom.h"
#include "mo_cntxt.h"
#include "mo_emit.h"
#include "mo_scan.h"
#include "mo_scope.h"
#include "mocha.h"

static PRCList mocha_context_list = PR_INIT_STATIC_CLIST(&mocha_context_list);

MochaContext *
mocha_NewContext(size_t stackSize)
{
    MochaContext *mc;

    mc = malloc(sizeof *mc - sizeof mc->stackBase + stackSize);
    if (!mc)
	return 0;
    memset(mc, 0, sizeof *mc);

    if (!mocha_InitAtomState(mc)) {
	free(mc);
	return 0;
    }
    if (!mocha_InitScanner(mc)) {
	mocha_FreeAtomState(mc);
	free(mc);
	return 0;
    }

    PR_APPEND_LINK(&mc->links, &mocha_context_list);
    PR_InitArenaPool(&mc->codePool, "code", 1024, sizeof(double));
    PR_InitArenaPool(&mc->tempPool, "temp", 1024, sizeof(double));
    mocha_InitTaintInfo(mc);
    MOCHA_INIT_STACK(&mc->stack, mc->stackBase, stackSize);
    return mc;
}

void
mocha_DestroyContext(MochaContext *mc)
{
#ifdef JAVA
    mocha_DestroyJavaContext(mc);
#endif
    mocha_FreeAtomState(mc);
    PR_FinishArenaPool(&mc->codePool);
    PR_FinishArenaPool(&mc->tempPool);
    PR_FREEIF(mc->lastMessage);
    PR_REMOVE_LINK(&mc->links);
    free(mc);
}

MochaContext *
mocha_ContextIterator(MochaContext **iterp)
{
    MochaContext *mc = *iterp;

    if (!mc)
	mc = (MochaContext *)mocha_context_list.next;
    if ((void *)mc == &mocha_context_list)
	return 0;
    *iterp = (MochaContext *)mc->links.next;
    return mc;
}

void
mocha_InitTaintInfo(MochaContext *mc)
{
    MochaTaintInfo *info = &mc->defaultTaintInfo;

    mc->taintInfo = info;
    info->taint = info->accum = MOCHA_TAINT_IDENTITY;
    info->data = 0;
}

void
mocha_ReportErrorAgain(MochaContext *mc, const char *message,
                       MochaErrorReport *reportp)
{
    MochaErrorReporter onError;

    if (!message) return;
    PR_FREEIF(mc->lastMessage);
    mc->lastMessage = strdup(message);
    onError = mc->errorReporter;
    if (onError)
	(*onError)(mc, mc->lastMessage, reportp);
}

void
mocha_ReportErrorVA(MochaContext *mc, const char *format, va_list ap)
{
    MochaErrorReport report, *reportp;
    char *last;

    if (mc->pc && mc->script) {
	report.filename = mc->script->filename;
	report.lineno = mocha_PCtoLineNumber(mc->script, mc->pc);
	/* XXX should fetch line somehow */
	report.linebuf = 0;
	report.tokenptr = 0;
	reportp = &report;
    } else {
	reportp = 0;
    }
    last = PR_vsmprintf(format, ap);
    if (!last) return;

    mocha_ReportErrorAgain(mc, last, reportp);
    free(last);
}

MochaBoolean
mocha_PushObject(MochaContext *mc, MochaObject *obj, MochaObjectStack **topp)
{
    MochaObjectStack *top;

    if (mc->objectStack && mc->objectStack->object == obj)
	return MOCHA_TRUE;
    top = MOCHA_malloc(mc, sizeof *top);
    if (!top)
	return MOCHA_FALSE;
    top->object = MOCHA_HoldObject(mc, obj);
    top->down = mc->objectStack;
    mc->objectStack = top;
    *topp = top;
    return MOCHA_TRUE;
}

void
mocha_PopObject(MochaContext *mc, MochaObjectStack *top)
{
    if (!top)
	return;
    PR_ASSERT(mc->objectStack == top);
    if (mc->objectStack == top)
	mc->objectStack = top->down;
    MOCHA_DropObject(mc, top->object);
    MOCHA_free(mc, top);
}

#ifdef DEBUG
/* For gdb usage. */
void mocha_traceon(MochaContext *mc)  { mc->tracefp = stderr; }
void mocha_traceoff(MochaContext *mc) { mc->tracefp = 0; }
#endif
