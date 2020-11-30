#ifndef prmem_h___
#define prmem_h___

/*
** API to NSPR memory systems. NSPR supports a thread safe malloc heap
** and a thread safe garbage collected heap (see prgc.h for the garbage
** collector's API).
*/
#include "prmacros.h"
#include "prtypes.h"
#include <stdio.h>
#include <stdlib.h>

NSPR_BEGIN_EXTERN_C

/*
** Thread safe memory allocation (NOT gc memory).
**
** NOTE: nspr wraps up malloc, free, calloc, realloc so they are already
** thread safe.
*/

#define PR_NEW(_struct) ((_struct *) malloc(sizeof(_struct)))

#define PR_NEWZAP(_struct) ((_struct *) calloc(1, sizeof(_struct)))

#define PR_DELETE(_ptr) free(_ptr)

#define PR_FREEIF(_ptr)	if (_ptr) free(_ptr)

NSPR_END_EXTERN_C

#endif /* prmem_h___ */
