#ifndef prdtoa_h___
#define prdtoa_h___

#include "prarena.h"
#include <stdio.h>
#include <stdarg.h>


NSPR_BEGIN_EXTERN_C

/*
** PR_strtod() returns as a double-precision floating-point number
** the  value represented by the character string pointed to by
** s00.  The string is scanned up to  the  first  unrecognized
** character.
** If the value of se is not (char **)NULL,  a  pointer  to
** the  character terminating the scan is returned in the location pointed
** to by se.  If no number can be  formed, se is set to s00r, and
** zero is returned.
*/
extern PR_PUBLIC_API(double) PR_strtod(const char *s00, char **se);

/*
** PR_dtoa() converts double to a string.
*/
extern PR_PUBLIC_API(char *) PR_dtoa(double d, int mode, int ndigits,
				     int *decpt, int *sign, char **rve);

/*
** PR_cnvtf()
** conversion routines for floating point
** prcsn - number of digits of precision to generate floating
** point value.
*/
extern PR_PUBLIC_API(void)
PR_cnvtf(char *buf,int bufsz, int prcsn,double fval);

NSPR_END_EXTERN_C

#endif /* prdtoa_h___ */
