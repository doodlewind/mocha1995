/* -*- Mode: C; tab-width: 4; -*- */

#ifndef prglobal_h___
#define prglobal_h___

#include "prmacros.h"
#include "prtypes.h"

NSPR_BEGIN_EXTERN_C

extern PR_PUBLIC_API(void) PR_Init(char *name, int priority, int maxCpus, int flags);
extern PR_PUBLIC_API(void) PR_Shutdown(void);
extern PR_PUBLIC_API(void) PR_InitPageStuff(void);

extern PR_PUBLIC_API(int32) PR_CeilingLog2(uint32 n);

extern PR_PUBLIC_API(PRBool) PR_Initialized(void);

#ifdef XP_UNIX
/*
** UNIX-specific: When a process forks, its itimer gets reset to zero. 
** This re-starts the itimer so pre-emption works. usec is the number of
** microseconds between successive clock ticks.
*/
extern void PR_StartEvents(int usec);
#endif

extern prword_t pr_pageSize, pr_pageShift;

extern PR_PUBLIC_API(void)
PR_Abort(void);

#ifdef DEBUG

#include <stdio.h>
extern FILE *pr_gcTraceFP;

#define PR_ASSERT(EX)				((EX)?((void)0):_PR_Assert( # EX , __FILE__, __LINE__))
#define PR_NOT_REACHED(reasonStr)	PR_ASSERT(!reasonStr)

#else /* !DEBUG */

#define PR_ASSERT(EX)				((void) 0)
#define PR_NOT_REACHED(reasonStr)

#endif /* !DEBUG */

/* private */
extern PR_PUBLIC_API(void)
_PR_Assert(const char *ex, const char *file, int line);

NSPR_END_EXTERN_C

#endif /* prglobal_h___ */
