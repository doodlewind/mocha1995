#ifndef prtime_h___
#define prtime_h___

/*
** API to NSPR time functions. NSPR time is stored in a 64 bit integer
** and is relative to Jan 1st, 1970 GMT. Also, the units are in
** microseconds, so you need to divide it by 1,000,000 to convert to
** seconds.
*/
#include "prlong.h"
#include "prmacros.h"

NSPR_BEGIN_EXTERN_C

/*
** Broken down form of 64 bit time value.
*/
struct PRTimeStr {
    int32 tm_usec;		/* microseconds of second (0-999999) */
    int8 tm_sec;		/* seconds of minute (0-59) */
    int8 tm_min;		/* minutes of hour (0-59) */
    int8 tm_hour;		/* hour of day (0-23) */
    int8 tm_mday;		/* day of month (1-31) */
    int8 tm_mon;		/* month of year (0-11) */
    int8 tm_wday;		/* 0=sunday, 1=monday, ... */
    int16 tm_year;		/* absolute year, AD */
    int16 tm_yday;		/* day of year (0 to 365) */
    int8 tm_isdst;		/* non-zero if DST in effect */
};

/* Some handy constants */
#define PR_MSEC_PER_SEC		1000
#define PR_USEC_PER_SEC		1000000L
#define PR_NSEC_PER_SEC		1000000000L
#define PR_USEC_PER_MSEC	1000
#define PR_NSEC_PER_MSEC	1000000L

/* Return the current local time in micro-seconds */
extern PR_PUBLIC_API(int64) PR_Now(void);

/* Return the current local time, in milliseconds */
extern PR_PUBLIC_API(int64) PR_NowMS(void);

/* Return the current local time in seconds */
extern PR_PUBLIC_API(int64) PR_NowS(void);

/* Convert a local time value into a GMT time value */
extern PR_PUBLIC_API(int64) PR_ToGMT(int64 time);

/* Explode a 64 bit time value into its components */
extern PR_PUBLIC_API(void) PR_ExplodeTime(PRTime *to, int64 time);

/* Compute the 64 bit time value from the components */
extern PR_PUBLIC_API(int64) PR_ComputeTime(PRTime *tm);

/* Format a time value into a buffer. Same semantics as strftime() */
extern PR_PUBLIC_API(size_t) PR_FormatTime(char *buf, int buflen, char *fmt, 
                                           PRTime *tm);

/* Format a time value into a buffer. Time is always in US English format, regardless
 * of locale setting.
 */
extern PR_PUBLIC_API(size_t)
PR_FormatTimeUSEnglish( char* buf, size_t bufSize,
                        const char* format, const PRTime* time );

#if defined(NEED_TIME_R) && defined(XP_UNIX)
#include <time.h> /* jwz */
struct tm *gmtime_r(const time_t *, struct tm *);
struct tm *localtime_r(const time_t *, struct tm *);
#endif

/************************************************************************
 * The following routines are taken from NSPR2.0.  The data types are
 * modified for this version.
 ************************************************************************/

/*
 * Adjust exploded time to normalize field overflows after manipulation.
 * Note that the following fields of PRExplodedTime should not be
 * manipulated:
 *   - tm_month and tm_year: because the number of days in a month and
 *     number of days in a year are not constant, it is ambiguous to
 *     manipulate the month and year fields, although one may be tempted
 *     to.  For example, what does "a month from January 31st" mean?
 *   - tm_wday and tm_yday: these fields are calculated by NSPR.  Users
 *     should treat them as "read-only".
 */

extern PR_PUBLIC_API (void)
PR_NormalizeTime(PRTime *time, int8 hourOffset, int8 minOffset);

/* Converse from a exploded GMT time to a 64 bit GMT time value */
extern PR_PUBLIC_API(int64)
PR_ImplodeTime(PRTime *exploded, int8 hourOffset, int8 minOffset);

/* Explode a 64 bit GMT time value into its components.  The resulting
 * exploded time is still GMT time.
 */
extern PR_PUBLIC_API(void) PR_ExplodeGMTTime(PRTime *to, int64 time);

NSPR_END_EXTERN_C

#endif /* prtime_h___ */
