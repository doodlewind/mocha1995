#ifndef prsync_h___
#define prsync_h___

/*
** API to NSPR synchronization primitives
**
** Monitors are not primitive -- they are composed of condvars and mutexes.
** Here are the real raw materials.
*/

#include "prmacros.h"

NSPR_BEGIN_EXTERN_C

#define PR_CAS(new, old, oldp)	(*(oldp) = (new), (old))

NSPR_END_EXTERN_C

#endif /* prsync_h___ */
