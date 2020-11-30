#ifndef prmjtime_h___
#define prmjtime_h___

/*
** NSPR date stuff for mocha and java. Placed here temporarily not to break
** Navigator and localize changes to mocha.
*/
#include "prtime.h"
#include <time.h>

NSPR_BEGIN_EXTERN_C

/* Return the current local time in micro-seconds */
extern PR_PUBLIC_API(int64) MJ_PR_Now(void);

/* Return the current local time in micro-seconds */
extern PR_PUBLIC_API(int64) MJ_PR_NowLocal(void);

/* Return the current local time, in milliseconds */
extern PR_PUBLIC_API(int64) MJ_PR_NowMS(void);

/* Return the current local time in seconds */
extern PR_PUBLIC_API(int64) MJ_PR_NowS(void);

/* Convert a local time value into a GMT time value */
extern PR_PUBLIC_API(int64) MJ_PR_ToGMT(int64 time);

/* Convert a GMT time value into a lcoal time value */
extern PR_PUBLIC_API(int64) MJ_PR_ToLocal(int64 time);

/* get the difference between this time zone and  gmt timezone in seconds */
extern PR_PUBLIC_API(time_t) MJ_PR_LocalGMTDifference(void);

/* Explode a 64 bit time value into its components */
extern PR_PUBLIC_API(void) MJ_PR_ExplodeTime(PRTime *to, int64 time);

/* Compute the 64 bit time value from the components */
extern PR_PUBLIC_API(int64) MJ_PR_ComputeTime(PRTime *tm);

/* Format a time value into a buffer. Same semantics as strftime() */
extern PR_PUBLIC_API(size_t) MJ_PR_FormatTime(char *buf, int buflen, char *fmt, 
					   PRTime *tm);
/* Format a time value into a buffer. Time is always in US English format, regardless
 * of locale setting.
 */
extern PR_PUBLIC_API(size_t)
MJ_PR_FormatTimeUSEnglish( char* buf, size_t bufSize,
                        const char* format, const PRTime* time );

/* Convert prtm structure into seconds since 1st January, 0 A.D. */
extern PR_PUBLIC_API(int64) MJ_PR_mktime(PRTime *prtm);

/* Get the Local time into prtime from tsecs */
extern PR_PUBLIC_API(void) MJ_PR_localtime(int64 tsecs,PRTime *prtm);

/* Get the gmt time into prtime from tsecs */
extern PR_PUBLIC_API(void) MJ_PR_gmtime(int64 tsecs,PRTime *prtm);

/* Get the DST offset for the local time passed in 
*/
extern PR_PUBLIC_API(int64) MJ_PR_DSTOffset(int64 time);

NSPR_END_EXTERN_C

#endif /* prmjtime_h___ */

