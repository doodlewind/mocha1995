#ifndef prprf_h___
#define prprf_h___

#include "prmacros.h"
#include "prarena.h"
#include <stdio.h>
#include <stdarg.h>

typedef int (*PR_vsxprintf_callback)(void *arg, const char *s, size_t slen);
typedef int (*PR_sxprintf_callback) (void *arg, const char *s, size_t len);

NSPR_BEGIN_EXTERN_C

/*
** sprintf into a fixed size buffer. Guarantees that a NUL is at the end
** of the buffer. Returns the length of the written output, NOT including
** the NUL, or (size_t)-1 if an error occurs.
*/
extern PR_PUBLIC_API(size_t) PR_snprintf(char *out, size_t outlen, 
                                        const char *fmt, ...);

/*
** sprintf into a PR_malloc'd buffer. Return a pointer to the malloc'd
** buffer on success, NULL on failure.
*/
extern PR_PUBLIC_API(char *) PR_smprintf(const char *fmt, ...);

#if 0
/* NOT YET IMPLEMENTED */
/*
** sprintf into an arena buffer. Return a pointer into the arena where
** the result is stored.
*/
extern PR_PUBLIC_API(char *) PR_saprintf(PRArenaPool *p, const char *fmt, ...);
#endif

/*
** sprintf into a function. The function "stuff" is called with a string
** to place into the output. "arg" is an opaque pointer used by the stuff
** function to hold any state needed to do the storage of the output
** data. The return value is a count of the number of characters fed to
** the stuff function, or (size_t)-1 if an error occurs.
*/
extern PR_PUBLIC_API(size_t) PR_sxprintf(PR_sxprintf_callback stuff,
			  void *arg, const char *fmt, ...);

/*
** "append" sprintf into a malloc'd buffer. "last" is the last value of
** the malloc'd buffer. sprintf will append data to the end of last,
** growing it as necessary using realloc. If last is NULL, PR_ssprintf
** will allocate the initial string. The return value is the new value of
** last for subsequent calls, or NULL if there is a malloc failure.
*/
extern PR_PUBLIC_API(char *)PR_sprintf_append(char *last, const char *fmt, ...);

/*
** Variable-argument-list forms of the above.
*/
extern PR_PUBLIC_API(size_t) PR_vsnprintf(char *out, size_t outlen, 
                                         const char *fmt, va_list ap);

extern PR_PUBLIC_API(char *) PR_vsmprintf(const char *fmt, va_list ap);

extern PR_PUBLIC_API(size_t) PR_vsxprintf(PR_vsxprintf_callback stuff,
			   void *arg, const char *fmt, va_list ap);

extern PR_PUBLIC_API(char *) PR_vsprintf_append(char *last, const char *fmt, 
                                              va_list ap);

NSPR_END_EXTERN_C

#endif /* prprf_h___ */
