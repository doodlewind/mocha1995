#ifndef prosdep_h___
#define prosdep_h___

#define XP_UNIX

/* Get OS specific header information */
#ifdef XP_PC
#include "prpcos.h"
#ifdef _WIN32
#include "os/win32.h"
#else
#include "os/win16.h"
#endif
#endif /* XP_PC */

#ifdef XP_MAC
#include "prmacos.h"
#endif

#ifdef XP_UNIX
// #include "prunixos.h"

/* Get endian-ness */
// #include "prcpucfg.h"

/*
** Hack alert!
*/
extern void PR_SetPollHook(int fd, int (*func)(int));

/* 
 * Get OS specific header information. Replaces CPU-based scheme because
 * many OSes share CPUs but not most of the defs found in these .h files --Rob
 */
#if defined(AIXV3)
#include "os/aix.h"

#elif defined(BSDI)
#include "os/bsdi.h"

#elif defined(HPUX)
#include "os/hpux.h"

#elif defined(IRIX)
#include "os/irix.h"

#elif defined(LINUX)
#include "os/linux.h"

#elif defined(OSF1)
#include "os/osf1.h"

#elif defined(SCO)
#include "os/scoos.h"

#elif defined(SOLARIS)
#include "os/solaris.h"

#elif defined(SUNOS4)
#include "os/sunos.h"

#elif defined(UNIXWARE)
#include "os/unixware.h"

#elif defined(NEC)
#include "os/nec.h"

#elif defined(SONY)
#include "os/sony.h"

#elif defined(NCR)
#include "os/ncr.h"

#elif defined(SNI)
#include "os/reliantunix.h"
#endif

#endif /* XP_UNIX */

#endif /* prosdep_h___ */
