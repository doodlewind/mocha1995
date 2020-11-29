#ifdef XP_MAC
#include <ConditionalMacros.h>
#if GENERATINGPOWERPC
#define alloca __alloca
#endif
#endif

#ifndef alloca

/* "Normal" configuration for alloca.  */

#ifdef __GNUC__
#define alloca __builtin_alloca
#else /* not __GNUC__ */
#if defined alpha || defined linux || defined mips || defined sparc
#include <alloca.h>
#else
#if __STDC__
#include <stddef.h>
void *alloca (size_t);
#else
char *alloca ();
#endif
#define ALLOCA_GC()	alloca(0)
#endif /* various CPU-types equated with OSes */
#endif /* not __GNUC__ */

#endif /* not alloca */

#ifndef ALLOCA_GC
#define ALLOCA_GC()
#endif
