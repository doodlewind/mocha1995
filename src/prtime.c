#ifdef SOLARIS
#define _REENTRANT 1
#endif
#include "prosdep.h"
#include "prtime.h"
#include "prprf.h"

#include <string.h>
#include <time.h>

#ifdef XP_PC
#include <sys/timeb.h>
#endif

#ifdef XP_MAC
#include <OSUtils.h>
#include <TextUtils.h>
#include <Resources.h>
#include <Timer.h>
extern void MyReadLocation(MachineLocation * loc);
#endif

#ifdef XP_UNIX

#ifdef SOLARIS
extern int gettimeofday(struct timeval *tv);
#endif

#include <sys/time.h>

#ifdef NEED_TIME_R
/* Awful hack, but... */
struct tm *gmtime_r(const time_t *a, struct tm *b)
{
    *b = *gmtime(a);
    return b;
}

struct tm *localtime_r(const time_t *a, struct tm *b)
{
    *b = *localtime(a);
    return b;
}
#endif /* NEED_TIME_R */

#endif /* XP_UNIX */

#ifdef XP_MAC
extern UnsignedWide			dstLocalBaseMicroseconds;
extern unsigned long		gJanuaryFirst1970Seconds;
#endif

/*
** Return the current local time in micro-seconds. Use the highest
** resolution available time source.
*/
PR_PUBLIC_API(int64) PR_Now(void)
{
#ifdef XP_PC
    int64 s, us, ms2us, s2us;
    struct timeb b;

    ftime(&b);
    LL_I2L(ms2us, PR_USEC_PER_MSEC);
    LL_I2L(s2us, PR_USEC_PER_SEC);
    LL_I2L(s, b.time);
    LL_I2L(us, b.millitm);
    LL_MUL(us, us, ms2us);
    LL_MUL(s, s, s2us);
    LL_ADD(s, s, us);
    return s;
#endif
#ifdef XP_UNIX
    struct timeval tv;
    int64 s, us, s2us;

#if defined(SOLARIS)
    gettimeofday(&tv);
#else
    gettimeofday(&tv, 0);
#endif
    LL_I2L(s2us, PR_USEC_PER_SEC);
    LL_I2L(s, tv.tv_sec);
    LL_I2L(us, tv.tv_usec);
    LL_MUL(s, s, s2us);
    LL_ADD(s, s, us);
    return s;
#endif
#ifdef XP_MAC
	UnsignedWide	upTime;
	uint64			localTime;

	Microseconds(&upTime);
	
	LL_ADD(localTime, *((uint64 *)&dstLocalBaseMicroseconds), *((uint64 *)&upTime));
	
	return *((uint64 *)&localTime);
#endif
}

/*
** Return the current local time in milli-seconds.
*/
PR_PUBLIC_API(int64) PR_NowMS(void)
{
    int64 us, us2ms;

    us = PR_Now();
    LL_I2L(us2ms, PR_USEC_PER_MSEC);
    LL_DIV(us, us, us2ms);
    
    return us;
}

/*
** Return the current local time in seconds.
*/
PR_PUBLIC_API(int64) PR_NowS(void)
{
    int64 us, us2s;

    us = PR_Now();
    LL_I2L(us2s, PR_USEC_PER_SEC);
    LL_DIV(us, us, us2s);
    return us;
}


/* Convert a local time value into a GMT time value */
PR_PUBLIC_API(int64) PR_ToGMT(int64 time)
{
#if defined(XP_UNIX) || defined(XP_PC)
    struct tm a;
    time_t t;
    int64 s, us, s2us;
    PRTime e;

    /* Convert from PRTime to struct tm */
    PR_ExplodeTime(&e, time);
    a.tm_sec = e.tm_sec;
    a.tm_min = e.tm_min;
    a.tm_hour = e.tm_hour;
    a.tm_mday = e.tm_mday;
    a.tm_mon = e.tm_mon;
    a.tm_wday = e.tm_wday;
    a.tm_year = e.tm_year - 1900;
    a.tm_yday = e.tm_yday;
    a.tm_isdst = e.tm_isdst;
#ifdef SUNOS4
    a.tm_zone = 0;
    a.tm_gmtoff = 0;
    t = timelocal(&a);
#else
    t = mktime(&a);
#endif

    /* Get GMT version of the time */
#ifdef XP_PC
    a = *gmtime(&t);
#else
    gmtime_r(&t, &a);
#endif

    a.tm_isdst = e.tm_isdst;

#ifdef SUNOS4
    t = timelocal(&a);
#else
    t = mktime(&a);
#endif

    /* Convert GMT to int64 */
    LL_I2L(s2us, PR_USEC_PER_SEC);
    LL_I2L(s, t);
    LL_I2L(us, e.tm_usec);
    LL_MUL(s, s, s2us);
    LL_ADD(s, s, us);
    return s;
#endif /* XP_UNIX || XP_PC */
#ifdef XP_MAC
	MachineLocation			machineLocation;
	int64					gmtOffsetSeconds,
							gmtOffsetMicroSeconds,
							gmtConvertedTime,
							secondsToMicroSeconds,
							dlsOffset;
	

	//	Get the information about the local machine, including
	//	its GMT offset and its daylight savings time info.
	//	Convert each into wides that we can add to 
	//	startupTimeMicroSeconds.
	
	MyReadLocation(&machineLocation);
	
	//	Mask off top eight bits of gmtDelta, sign extend lower three.
	
	if ((machineLocation.u.gmtDelta & 0x00800000) != 0) {
		gmtOffsetSeconds.lo = (machineLocation.u.gmtDelta & 0x00FFFFFF) | 0xFF000000;
		gmtOffsetSeconds.hi = 0xFFFFFFFF;
	}
	else {
	
		gmtOffsetSeconds.lo = (machineLocation.u.gmtDelta & 0x00FFFFFF);
		gmtOffsetSeconds.hi = 0;

	}
	
	LL_I2L(secondsToMicroSeconds, PR_USEC_PER_SEC);		// microseconds/second

	// Is Daylight Savings On?  If so, we need to add an hour to the offset.
	if (machineLocation.u.dlsDelta != 0) {
		uint64		sixty;
		
		LL_I2L(sixty, 60);
		LL_MUL(dlsOffset, secondsToMicroSeconds, sixty);		// 60 sec/min
		LL_MUL(dlsOffset, dlsOffset, sixty);					// 60 min/hour
	}
	else 
		LL_I2L(dlsOffset, 0);

	LL_MUL(gmtOffsetMicroSeconds, gmtOffsetSeconds, secondsToMicroSeconds);
	LL_SUB(gmtConvertedTime, time, gmtOffsetMicroSeconds);
	LL_ADD(gmtConvertedTime, gmtConvertedTime, dlsOffset);
		
	return gmtConvertedTime;
#endif
}

/* Explode a 64 bit time value into its components */
PR_PUBLIC_API(void) PR_ExplodeTime(PRTime *to, int64 time)
{
#if defined(XP_UNIX) || defined(XP_PC)
    struct tm a;
    int64 s, us2s, us;
    time_t t;
    
    /* Convert back to seconds since 1970 */
    LL_I2L(us2s, PR_USEC_PER_SEC);
    LL_DIV(s, time, us2s);
    LL_MOD(us, time, us2s);
    LL_L2I(t, s);
#ifdef XP_PC
    a = *localtime(&t);
#else
    localtime_r(&t, &a);
#endif  /* XP_PC */
    LL_L2I(to->tm_usec, us);
    to->tm_sec = a.tm_sec;
    to->tm_min = a.tm_min;
    to->tm_hour = a.tm_hour;
    to->tm_mday = a.tm_mday;
    to->tm_mon = a.tm_mon;
    to->tm_wday = a.tm_wday;
    to->tm_year = a.tm_year + 1900;
    to->tm_yday = a.tm_yday;
    to->tm_isdst = a.tm_isdst;
#endif
#ifdef XP_MAC
	DateTimeRec				timeRec;
	MachineLocation			machineLocation;
	uint32					timeSeconds,
							firstOfYearSeconds;
	uint64					microSecondsToSeconds,
							timeSecondsLong;

	LL_I2L(microSecondsToSeconds, PR_USEC_PER_SEC);
	LL_DIV(timeSecondsLong, time, microSecondsToSeconds);
	LL_L2I(timeSeconds, timeSecondsLong);
	
	timeSeconds += gJanuaryFirst1970Seconds;
	
	SecondsToDate(timeSeconds, &timeRec);
	
	to->tm_sec = timeRec.second;
	to->tm_min = timeRec.minute;
	to->tm_hour = timeRec.hour;
	to->tm_mday = timeRec.day;
 	to->tm_mon = timeRec.month - 1;
  	to->tm_wday = timeRec.dayOfWeek - 1;
	to->tm_year = timeRec.year;

	MyReadLocation(&machineLocation);

	//	Figure out daylight savings time.

	to->tm_isdst = (machineLocation.u.dlsDelta != 0);

	//	Figure out the day of the year.
	
	timeRec.day = 1;
 	timeRec.month = 1;

	DateToSeconds(&timeRec, &firstOfYearSeconds);
	
	to->tm_yday = ((timeSeconds - firstOfYearSeconds) / (60L * 60L * 24L)) + 1;
	
#endif
}

/* Compute the 64 bit time value from the components */
PR_PUBLIC_API(int64) PR_ComputeTime(PRTime *prtm)
{
#if defined(XP_UNIX) || defined(XP_PC)
    struct tm a;
    int64 s, us, s2us;
    time_t t;

    a.tm_sec = prtm->tm_sec;
    a.tm_min = prtm->tm_min;
    a.tm_hour = prtm->tm_hour;
    a.tm_mday = prtm->tm_mday;
    a.tm_mon = prtm->tm_mon;
    a.tm_wday = prtm->tm_wday;
    a.tm_year = prtm->tm_year - 1900;
    a.tm_yday = prtm->tm_yday;
    a.tm_isdst = prtm->tm_isdst;
#ifdef XP_PC
    t = mktime(&a);
#endif
#ifdef SUNOS4
    a.tm_zone = 0;
    a.tm_gmtoff = 0;
    t = timelocal(&a);
#else
    t = mktime(&a);
#endif
    LL_I2L(s2us, PR_USEC_PER_SEC);
    LL_I2L(s, t);
    LL_I2L(us, prtm->tm_usec);
    LL_MUL(s, s, s2us);
    LL_ADD(s, s, us);
    return s;
#endif
#ifdef XP_MAC
	DateTimeRec				timeRec;
	uint32					convertedTimeSeconds;
	int64					result;
	int64					microSecondsToSeconds;

	timeRec.year = prtm->tm_year;
	timeRec.month = prtm->tm_mon + 1;
	timeRec.day = prtm->tm_mday + 1;
	timeRec.hour = prtm->tm_hour;
	timeRec.minute = prtm->tm_min;
	timeRec.second = prtm->tm_sec;
	timeRec.dayOfWeek = 0;

	DateToSeconds(&timeRec, &convertedTimeSeconds);

	convertedTimeSeconds -= gJanuaryFirst1970Seconds;
		
	LL_I2L(microSecondsToSeconds, PR_USEC_PER_SEC);
	LL_I2L(result, convertedTimeSeconds);
	LL_MUL(result, result, microSecondsToSeconds);
	
	return result;
#endif
}

/* Format a time value into a buffer. Same semantics as strftime() */
PR_PUBLIC_API(size_t) PR_FormatTime(char *buf, int buflen, char *fmt, PRTime *prtm)
{
#if defined(XP_UNIX) || defined(XP_PC) || defined(XP_MAC)
    struct tm a;
    a.tm_sec = prtm->tm_sec;
    a.tm_min = prtm->tm_min;
    a.tm_hour = prtm->tm_hour;
    a.tm_mday = prtm->tm_mday;
    a.tm_mon = prtm->tm_mon;
    a.tm_wday = prtm->tm_wday;
    a.tm_year = prtm->tm_year - 1900;
    a.tm_yday = prtm->tm_yday;
    a.tm_isdst = prtm->tm_isdst;
#ifdef SUNOS4
{
    time_t now;
    struct tm *lt;

    now = time((time_t *)0);
    lt = localtime(&now);
    if (lt == 0) {
	PR_snprintf(buf, buflen, "can't get timezone");
	return 0;
    }
    a.tm_zone = lt->tm_zone;
    a.tm_gmtoff = lt->tm_gmtoff;
}
#endif
    return strftime(buf, buflen, fmt, &a);
#endif
}

/* The following string arrays and macros are used by PR_FormatTimeUSEnglish().
 */

static const char* abbrevDays[] =
{
   "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
};

static const char* days[] =
{
   "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};

static const char* abbrevMonths[] =
{
   "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static const char* months[] =
 { 
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};


/* Add a single character to the given buffer, incrementing the buffer pointer
 * and decrementing the buffer size. Return 0 on error.
 */
#define ADDCHAR( buf, bufSize, ch )             \
do                                              \
{                                               \
   if( bufSize < 1 )                            \
   {                                            \
      *(--buf) = '\0';                          \
      return 0;                                 \
   }                                            \
   *buf++ = ch;                                 \
   bufSize--;                                   \
}                                               \
while(0)


/* Add a string to the given buffer, incrementing the buffer pointer and decrementing
 * the buffer size appropriately. Return 0 on error.
 */
#define ADDSTR( buf, bufSize, str )             \
do                                              \
{                                               \
   size_t strSize = strlen( str );              \
   if( strSize > bufSize )                      \
   {                                            \
      if( bufSize==0 )                          \
         *(--buf) = '\0';                       \
      else                                      \
         *buf = '\0';                           \
      return 0;                                 \
   }                                            \
   memcpy(buf, str, strSize);                   \
   buf += strSize;                              \
   bufSize -= strSize;                          \
}                                               \
while(0)

/* Needed by PR_FormatTimeUSEnglish() */
static unsigned int  pr_WeekOfYear(const PRTime* time, unsigned int firstDayOfWeek);


/***********************************************************************************
 *
 * Description:
 *  This is a dumbed down version of strftime that will format the date in US
 *  English regardless of the setting of the global locale.  This functionality is
 *  needed to write things like MIME headers which must always be in US English.
 *
 **********************************************************************************/
             
PR_PUBLIC_API(size_t)
PR_FormatTimeUSEnglish( char* buf, size_t bufSize,
                        const char* format, const PRTime* time )
{
   char*         bufPtr = buf;
   const char*   fmtPtr;
   char          tmpBuf[ 40 ];        
   const int     tmpBufSize = sizeof( tmpBuf );

   
   for( fmtPtr=format; *fmtPtr != '\0'; fmtPtr++ )
   {
      if( *fmtPtr != '%' )
      {
         ADDCHAR( bufPtr, bufSize, *fmtPtr );
      }
      else
      {
         switch( *(++fmtPtr) )
         {
         case '%':
            /* escaped '%' character */
            ADDCHAR( bufPtr, bufSize, '%' );
            break;
            
         case 'a':
            /* abbreviated weekday name */
            ADDSTR( bufPtr, bufSize, abbrevDays[ time->tm_wday ] );
            break;
               
         case 'A':
            /* full weekday name */
            ADDSTR( bufPtr, bufSize, days[ time->tm_wday ] );
            break;
        
         case 'b':
            /* abbreviated month name */
            ADDSTR( bufPtr, bufSize, abbrevMonths[ time->tm_mon ] );
            break;
        
         case 'B':
            /* full month name */
            ADDSTR(bufPtr, bufSize,  months[ time->tm_mon ] );
            break;
        
         case 'c':
            /* Date and time. */
            PR_FormatTimeUSEnglish( tmpBuf, tmpBufSize, "%a %b %d %H:%M:%S %Y", time );
            ADDSTR( bufPtr, bufSize, tmpBuf );
            break;
        
         case 'd':
            /* day of month ( 01 - 31 ) */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d",time->tm_mday );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;

         case 'H':
            /* hour ( 00 - 23 ) */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d",time->tm_hour );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
        
         case 'I':
            /* hour ( 01 - 12 ) */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d",
                        (time->tm_hour%12) ? time->tm_hour%12 : 12 );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
        
         case 'j':
            /* day number of year ( 001 - 366 ) */
            PR_snprintf(tmpBuf,tmpBufSize,"%.3d",time->tm_yday + 1);
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
        
         case 'm':
            /* month number ( 01 - 12 ) */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d",time->tm_mon+1);
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
        
         case 'M':
            /* minute ( 00 - 59 ) */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d",time->tm_min );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
       
         case 'p':
            /* locale's equivalent of either AM or PM */
            ADDSTR( bufPtr, bufSize, (time->tm_hour<12)?"AM":"PM" ); 
            break;
        
         case 'S':
            /* seconds ( 00 - 61 ), allows for leap seconds */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d",time->tm_sec );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
     
         case 'U':
            /* week number of year ( 00 - 53  ),  Sunday  is  the first day of week 1 */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d", pr_WeekOfYear( time, 0 ) );
            ADDSTR( bufPtr, bufSize, tmpBuf );
            break;
        
         case 'w':
            /* weekday number ( 0 - 6 ), Sunday = 0 */
            PR_snprintf(tmpBuf,tmpBufSize,"%d",time->tm_wday );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
        
         case 'W':
            /* Week number of year ( 00 - 53  ),  Monday  is  the first day of week 1 */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d", pr_WeekOfYear( time, 1 ) );
            ADDSTR( bufPtr, bufSize, tmpBuf );
            break;
        
         case 'x':
            /* Date representation */
            PR_FormatTimeUSEnglish( tmpBuf, tmpBufSize, "%m/%d/%y", time );
            ADDSTR( bufPtr, bufSize, tmpBuf );
            break;
        
         case 'X':
            /* Time representation. */
            PR_FormatTimeUSEnglish( tmpBuf, tmpBufSize, "%H:%M:%S", time );
            ADDSTR( bufPtr, bufSize, tmpBuf );
            break;
        
         case 'y':
            /* year within century ( 00 - 99 ) */
            PR_snprintf(tmpBuf,tmpBufSize,"%.2d",time->tm_year % 100 );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
        
         case 'Y':
            /* year as ccyy ( for example 1986 ) */
            PR_snprintf(tmpBuf,tmpBufSize,"%.4d",time->tm_year );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;
        
         case 'Z':
            /* Time zone name or no characters if  no  time  zone exists.
             * Since time zone name is supposed to be independant of locale, we
             * defer to PR_FormatTime() for this option.
             */
            PR_FormatTime( tmpBuf, tmpBufSize, "%Z", (PRTime*)time );
            ADDSTR( bufPtr, bufSize, tmpBuf ); 
            break;

         default:
            /* Unknown format.  Simply copy format into output buffer. */
            ADDCHAR( bufPtr, bufSize, '%' );
            ADDCHAR( bufPtr, bufSize, *fmtPtr );
            break;
            
         }
      }
   }

   ADDCHAR( bufPtr, bufSize, '\0' );
   return (size_t)(bufPtr - buf - 1);
}



/***********************************************************************************
 *
 * Description:
 *  Returns the week number of the year (0-53) for the given time.  firstDayOfWeek
 *  is the day on which the week is considered to start (0=Sun, 1=Mon, ...).
 *  Week 1 starts the first time firstDayOfWeek occurs in the year.  In other words,
 *  a partial week at the start of the year is considered week 0.  
 *
 **********************************************************************************/

static unsigned int pr_WeekOfYear(const PRTime* time, unsigned int firstDayOfWeek)
{
   int dayOfWeek;
   int dayOfYear;

  /* Get the day of the year for the given time then adjust it to represent the
   * first day of the week containing the given time.
   */
  dayOfWeek = time->tm_wday - firstDayOfWeek;
  if (dayOfWeek < 0)
    dayOfWeek += 7;
  
  dayOfYear = time->tm_yday - dayOfWeek;


  if( dayOfYear <= 0 )
  {
     /* If dayOfYear is <= 0, it is in the first partial week of the year. */
     return 0;
  }
  else
  {
     /* Count the number of full weeks ( dayOfYear / 7 ) then add a week if there
      * are any days left over ( dayOfYear % 7 ).  Because we are only counting to
      * the first day of the week containing the given time, rather than to the
      * actual day representing the given time, any days in week 0 will be "absorbed"
      * as extra days in the given week.
      */
     return (dayOfYear / 7) + ( (dayOfYear % 7) == 0 ? 0 : 1 );
  }
}

/************************************************************************
 * The following routines are taken from NSPR2.0.  The data types are
 * modified for this version.
 ************************************************************************/

/*
 * Static variables used by functions in this file
 */

/*
 * The number of days in a month
 */

static int8 nDays[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
};

/*
 * The following array contains the day of year for the last day of
 * each month, where index 1 is January, and day 0 is January 1.
 */

static int lastDayOfMonth[2][13] = {
    {-1, 30, 58, 89, 119, 150, 180, 211, 242, 272, 303, 333, 364},
    {-1, 30, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365}
};

/*
 *-------------------------------------------------------------------------
 *
 * IsLeapYear --
 *
 *     Returns 1 if the year is a leap year, 0 otherwise.
 *
 *-------------------------------------------------------------------------
 */

static int IsLeapYear(int16 year)
{
    if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
	return 1;
    else
	return 0;
}

PR_PUBLIC_API (void)
PR_NormalizeTime(PRTime *time, int8 hourOffset, int8 minOffset)
{
    int daysInMonth;
    int32 fourYears;
    int32 remainder;
    int32 numDays;

    /* Get back to GMT */
    time->tm_hour -= hourOffset;
    time->tm_min -= minOffset;

    /* Now normalize GMT */

    if (time->tm_usec < 0 || time->tm_usec >= 1000000) {
        time->tm_sec +=  (int8)(time->tm_usec / 1000000); /* ? */
        time->tm_usec %= 1000000;
        if (time->tm_usec < 0) {
	    time->tm_usec += 1000000;
	    time->tm_sec--;
        }
    }

    /* Note that we do not count leap seconds in this implementation */
    if (time->tm_sec < 0 || time->tm_sec >= 60) {
        time->tm_min += time->tm_sec / 60;
        time->tm_sec %= 60;
        if (time->tm_sec < 0) {
	    time->tm_sec += 60;
	    time->tm_min--;
        }
    }

    if (time->tm_min < 0 || time->tm_min >= 60) {
        time->tm_hour += time->tm_min / 60;
        time->tm_min %= 60;
        if (time->tm_min < 0) {
	    time->tm_min += 60;
	    time->tm_hour--;
	}
    }

    if (time->tm_hour < 0 || time->tm_hour >= 24) {
        time->tm_mday += time->tm_hour / 24;
        time->tm_hour %= 24;
        if (time->tm_hour < 0) {
	    time->tm_hour += 24;
	    time->tm_mday--;
	}
    }

    /* Normalize month and year before mday */
    if (time->tm_mon < 0 || time->tm_mon >= 12) {
        time->tm_year += time->tm_mon / 12;
        time->tm_mon %= 12;
        if (time->tm_mon < 0) {
	    time->tm_mon += 12;
	    time->tm_year--;
	}
    }

    /* Now that month and year are in proper range, normalize mday */

    if (time->tm_mday < 1) {
	/* mday too small */
	do {
	    /* the previous month */
	    time->tm_mon--;
	    if (time->tm_mon < 0) {
		time->tm_mon = 11;
		time->tm_year--;
            }
	    time->tm_mday += nDays[IsLeapYear(time->tm_year)][time->tm_mon];
	} while (time->tm_mday < 1);
    } else {
	daysInMonth = nDays[IsLeapYear(time->tm_year)][time->tm_mon];
	while (time->tm_mday > daysInMonth) {
	    /* mday too large */
	    time->tm_mday -= daysInMonth;
	    time->tm_mon++;
	    if (time->tm_mon > 11) {
		time->tm_mon = 0;
		time->tm_year++;
	    }
	    daysInMonth = nDays[IsLeapYear(time->tm_year)][time->tm_mon];
	}
    }

    /* Recompute yday and wday */
    time->tm_yday = time->tm_mday +
	    lastDayOfMonth[IsLeapYear(time->tm_year)][time->tm_mon];
    fourYears = (time->tm_year - 1970) / 4;
    remainder = (time->tm_year - 1970) % 4;
    if (remainder < 0) {
	remainder += 4;
	fourYears--;
    }
    numDays = fourYears * (4 * 365 + 1);
    switch (remainder) {
	case 0:
	    break;
	case 1:
	    numDays += 365;  /* 1970 */
	    break;
	case 2:
	    numDays += 365 + 365;  /* 1970 and 1971 */
	    break;
	case 3:
	    numDays += 365 + 365 + 366; /* 1970-2 */
    }
    numDays += time->tm_yday;
    time->tm_wday = (numDays + 4) % 7;
    if (time->tm_wday < 0) {
	time->tm_wday += 7;
    }
}

/*
 *------------------------------------------------------------------------
 *
 * PR_ImplodeTime --
 *
 *     Cf. time_t mktime(struct tm *tp)
 *     Note that 1 year has < 2^25 seconds.  So an int32 is large enough.
 *
 *------------------------------------------------------------------------
 */

PR_PUBLIC_API(int64)
PR_ImplodeTime(PRTime *exploded, int8 hourOffset, int8 minOffset)
{
    PRTime copy;
    int64 retVal;
    int64 secPerDay, usecPerSec;
    int64 temp;
    int64 numSecs64;
    int32 fourYears;
    int32 remainder;
    int32 numDays;
    int32 numSecs;

    /* Normalize first.  Do this on our copy */
    copy = *exploded;
    PR_NormalizeTime(&copy, hourOffset, minOffset);

    fourYears = (copy.tm_year - 1970) / 4;
    remainder = (copy.tm_year - 1970) % 4;
    if (remainder < 0) {
	remainder += 4;
	fourYears--;
    }
    numDays = fourYears * (4 * 365 + 1);
    switch (remainder) {
	case 0:
	    break;
	case 1:  /* 1970 */
	    numDays += 365;
	    break;
	case 2:  /* 1970-1 */
	    numDays += 365 * 2;
	    break;
	case 3:  /* 1970-2 */
	    numDays += 365 * 3 + 1;
	    break;
    }

    numSecs = (int32)(copy.tm_yday * 86400 + copy.tm_hour * 3600
	    + copy.tm_min * 60 + copy.tm_sec);

    LL_I2L(temp, numDays);
    LL_I2L(secPerDay, 86400);
    LL_MUL(temp, temp, secPerDay);
    LL_I2L(numSecs64, numSecs);
    LL_ADD(numSecs64, numSecs64, temp);
    
    LL_I2L(usecPerSec, 1000000L);
    LL_MUL(temp, numSecs64, usecPerSec);
    LL_I2L(retVal, copy.tm_usec);
    LL_ADD(retVal, retVal, temp);

    return retVal;
}

/*
 *------------------------------------------------------------------------
 *
 * ComputeGMT --
 *
 *     Caveats:
 *     - we ignore leap seconds
 *     - our leap-year calculation is only correct for years 1901-2099
 * Note:
 *     This routine is taken from NSPR 2.0
 *------------------------------------------------------------------------
 */

static void
ComputeGMT(int64 time, PRTime *gmt)
{
    int32 tmp, rem;
    int32 numDays;
    int64 numDays64, rem64;
    int isLeap;
    int64 sec;
    int64 usec;
    int64 usecPerSec;
    int64 secPerDay;

    /*
     * We first do the usec, sec, min, hour thing so that we do not
     * have to do LL arithmetic.
     */

    LL_I2L(usecPerSec, 1000000L);
    LL_DIV(sec, time, usecPerSec);
    LL_MOD(usec, time, usecPerSec);
    LL_L2I(gmt->tm_usec, usec);
    /* Correct for weird mod semantics so the remainder is always positive */
    if (gmt->tm_usec < 0) {
	int64 one;

	LL_I2L(one, 1L);
	LL_SUB(sec, sec, one);
	gmt->tm_usec += 1000000L;
    }

    LL_I2L(secPerDay, 86400L);
    LL_DIV(numDays64, sec, secPerDay);
    LL_MOD(rem64, sec, secPerDay);
    /* We are sure both of these numbers can fit into int32 */
    LL_L2I(numDays, numDays64);
    LL_L2I(rem, rem64);
    if (rem < 0) {
	numDays--;
	rem += 86400L;
    }

    /* Compute day of week.  Epoch started on a Thursday. */

    gmt->tm_wday = (numDays + 4) % 7;
    if (gmt->tm_wday < 0) {
	gmt->tm_wday += 7;
    }

    /* Compute the time of day. */
    
    gmt->tm_hour = rem / 3600;
    rem %= 3600;
    gmt->tm_min = rem / 60;
    gmt->tm_sec = rem % 60;

    /* Compute the four-year span containing the specified time */

    tmp = numDays / (4 * 365 + 1);
    rem = numDays % (4 * 365 + 1);

    if (rem < 0) {
	tmp--;
	rem += (4 * 365 + 1);
    }

    /*
     * Compute the year after 1900 by taking the four-year span and
     * adjusting for the remainder.  This works because 2000 is a 
     * leap year, and 1900 and 2100 are out of the range.
     */
    
    tmp = (tmp * 4) + 1970;
    isLeap = 0;

    /*
     * 1970 has 365 days
     * 1971 has 365 days
     * 1972 has 366 days (leap year)
     * 1973 has 365 days
     */

    if (rem >= 365) {				/* 1971, etc. */
	tmp++;
	rem -= 365;
	if (rem >= 365) {			/* 1972, etc. */
	    tmp++;
	    rem -= 365;
	    if (rem >= 366) {			/* 1973, etc. */
		tmp++;
		rem -= 366;
            } else {
		isLeap = 1;
            }
        }
    }

    gmt->tm_year = tmp;
    gmt->tm_yday = rem;

    /* Compute the month and day of month. */

    for (tmp = 1; lastDayOfMonth[isLeap][tmp] < gmt->tm_yday; tmp++) {
    }
    gmt->tm_mon = --tmp;
    gmt->tm_mday = gmt->tm_yday - lastDayOfMonth[isLeap][tmp];
}


PR_PUBLIC_API(void) PR_ExplodeGMTTime(PRTime *to, int64 time)
{
    ComputeGMT(time, to);
}
