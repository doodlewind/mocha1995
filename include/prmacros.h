#ifndef prmacros_h___
#define prmacros_h___

/*
** Fundamental NSPR macros, used nearly everywhere.
*/

/*
** Macro body brackets so that macros with compound statement definitions
** behave syntactically more like functions when called.
*/
#define NSPR_BEGIN_MACRO	do {
#define NSPR_END_MACRO		} while (0)

/*
** Macro shorthands for conditional C++ extern block delimiters.
*/
#ifdef __cplusplus
#define NSPR_BEGIN_EXTERN_C	extern "C" {
#define NSPR_END_EXTERN_C	}
#else
#define NSPR_BEGIN_EXTERN_C
#define NSPR_END_EXTERN_C
#endif

/*
** Bit masking macros.  XXX n must be <= 31 to be portable
*/
#define PR_BIT(n)	((uprword_t)1 << (n))
#define PR_BITMASK(n)	(PR_BIT(n) - 1)

/* Commonly used macros */
#define PR_ROUNDUP(x,y) ((((x)+((y)-1))/(y))*(y))
#define PR_MIN(x,y)     ((x)<(y)?(x):(y))
#define PR_MAX(x,y)     ((x)>(y)?(x):(y))

/************************************************************************/

/*
** Prototypes and macros used to make up for deficiencies in ANSI environments
** that we have found.
**
** Since we do not wrap <stdlib.h> and all the other standard headers, authors
** of portable code will not know in general that they need these definitions.
** Instead of requiring these authors to find the dependent uses in their code
** and take the following steps only in those C files, we take steps once here
** for all C files.
*/
#ifdef SUNOS4
# include "sunos4.h"
#endif

#define IS_LITTLE_ENDIAN __IS_LITTLE_ENDIAN__

#endif /* prmacros_h___ */
