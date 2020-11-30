

/*
** NSPR time code for Mocha
*/
#ifdef SOLARIS
#define _REENTRANT 1
#endif
#include "prosdep.h"
#include "prmjtime.h"
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
#endif

#ifdef XP_UNIX

#ifdef SOLARIS
extern int gettimeofday(struct timeval *tv);
#endif

#include <sys/time.h>

#endif /* XP_UNIX */

#ifdef XP_MAC
extern UnsignedWide		dstLocalBaseMicroseconds;
extern unsigned long		gJanuaryFirst1970Seconds;
extern void MyReadLocation(MachineLocation* l);
#endif

#define IS_LEAP(year) \
   (year != 0 && ((((year & 0x3) == 0) &&  \
		   ((year - ((year/100) * 100)) != 0)) || \
		  (year - ((year/400) * 400)) == 0))

#define PR_HOUR_SECONDS  3600L
#define PR_DAY_SECONDS  (24L * PR_HOUR_SECONDS)
#define PR_YEAR_SECONDS (PR_DAY_SECONDS * 365L)
#define PR_MAX_UNIX_TIMET 2145859200L /*time_t value equiv. to 12/31/2037 */
/* function prototypes */
static void MJ_PR_basetime(int64 tsecs, PRTime *prtm);
/*
** get the difference in seconds between this time zone and UTC (GMT)
*/
PR_PUBLIC_API(time_t) MJ_PR_LocalGMTDifference()
{
#if defined(XP_MAC)
    static time_t zone = -1L;
#endif
#if defined(XP_UNIX) || defined(XP_PC)
    struct tm ltime;
    /* get the difference between this time zone and GMT */
    memset((char *)&ltime,0,sizeof(ltime));
    ltime.tm_mday = 2;
    ltime.tm_year = 70;
#ifdef SUNOS4
    ltime.tm_zone = 0;
    ltime.tm_gmtoff = 0;
    return timelocal(&ltime) - (24 * 3600);
#else
    return mktime(&ltime) - (24L * 3600L);
#endif
#endif
#if defined(XP_MAC)
    MachineLocation  machineLocation;
    uint64	     gmtOffsetSeconds;
    uint64	     gmtDelta;
    uint64	     dlsOffset;
    int32	     offset;
	
    /* difference has been set no need to recalculate */
    if(zone != -1)
	return zone;

    /* Get the information about the local machine, including
    ** its GMT offset and its daylight savings time info.
    ** Convert each into wides that we can add to 
    ** startupTimeMicroSeconds.
    */
	
    MyReadLocation(&machineLocation);
	
    /* Mask off top eight bits of gmtDelta, sign extend lower three. */
	
    if ((machineLocation.u.gmtDelta & 0x00800000) != 0) {
	gmtOffsetSeconds.lo = (machineLocation.u.gmtDelta & 0x00FFFFFF) | 0xFF000000;
	gmtOffsetSeconds.hi = 0xFFFFFFFF;
	LL_UI2L(gmtDelta,0);
    }
    else {
	
	gmtOffsetSeconds.lo = (machineLocation.u.gmtDelta & 0x00FFFFFF);
	gmtOffsetSeconds.hi = 0;
	LL_UI2L(gmtDelta,PR_DAY_SECONDS);
    }
    /* normalize time to be positive if you are behind GMT. gmtDelta will always
    ** be positive.
    */
    LL_SUB(gmtDelta,gmtDelta,gmtOffsetSeconds);
	
    /* Is Daylight Savings On?  If so, we need to add an hour to the offset. */
    if (machineLocation.u.dlsDelta != 0) {
	LL_UI2L(dlsOffset, PR_HOUR_SECONDS);
    }
    else 
	LL_I2L(dlsOffset, 0);

    LL_ADD(gmtDelta,gmtDelta, dlsOffset);
    LL_L2I(offset,gmtDelta);

    zone = offset;
    return (time_t)offset;
#endif
}

/*
** get information about the DST status of this time zone
*/
PR_PUBLIC_API(void)
MJ_PR_setDST(PRTime *prtm)
{
#ifdef XP_MAC
    MachineLocation machineLocation;

    if(prtm->tm_isdst < 0){	
	MyReadLocation(&machineLocation);
	/* Figure out daylight savings time. */
	prtm->tm_isdst = (machineLocation.u.dlsDelta != 0);
    }
#else
    struct tm time;

    if(prtm->tm_isdst < 0){	
	if(prtm->tm_year >= 1970 && prtm->tm_year <= 2037){
	    time.tm_sec  = prtm->tm_sec ;
	    time.tm_min  = prtm->tm_min ;
	    time.tm_hour = prtm->tm_hour;
	    time.tm_mday = prtm->tm_mday;
	    time.tm_mon  = prtm->tm_mon ;
	    time.tm_wday = prtm->tm_wday;
	    time.tm_year = prtm->tm_year-1900;
	    time.tm_yday = prtm->tm_yday;
	    time.tm_isdst = -1;
	    mktime(&time);
	    prtm->tm_isdst = time.tm_isdst;
	}
	else {
	    prtm->tm_isdst = 0;
	}
    }
#endif /* XP_MAC */
}

/* Constants for GMT offset from 1970 */
#define G1970GMTMICROHI        0x00dcdcad /* micro secs to 1970 hi */
#define G1970GMTMICROLOW       0x8b3fa000 /* micro secs to 1970 low */

#define G2037GMTMICROHI        0x00e45fab /* micro secs to 2037 high */
#define G2037GMTMICROLOW       0x7a238000 /* micro secs to 2037 low */
/* Convert from extended time to base time (time since Jan 1 1970) it
*  truncates dates if time is before 1970 and after 2037.
*/

PR_PUBLIC_API(int32)
MJ_PR_ToBaseTime(int64 time)
{
    int64 g1970GMTMicroSeconds;
    int64 g2037GMTMicroSeconds;
    int64 low;
    int32 result;

    LL_UI2L(g1970GMTMicroSeconds,G1970GMTMICROHI);
    LL_UI2L(low,G1970GMTMICROLOW);
#ifndef HAVE_LONG_LONG
    LL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,16);
    LL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,16);
#else
    LL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,32);
#endif
    LL_ADD(g1970GMTMicroSeconds,g1970GMTMicroSeconds,low);

    LL_UI2L(g2037GMTMicroSeconds,G2037GMTMICROHI);
    LL_UI2L(low,G2037GMTMICROLOW);
#ifndef HAVE_LONG_LONG
    LL_SHL(g2037GMTMicroSeconds,g2037GMTMicroSeconds,16);
    LL_SHL(g2037GMTMicroSeconds,g2037GMTMicroSeconds,16);
#else
    LL_SHL(g2037GMTMicroSeconds,g2037GMTMicroSeconds,32);
#endif

    LL_ADD(g2037GMTMicroSeconds,g2037GMTMicroSeconds,low);
    
    
    if(LL_CMP(time, <, g1970GMTMicroSeconds) ||
       LL_CMP(time, >, g2037GMTMicroSeconds)){
	return -1;
    }
    
    LL_SUB(time,time,g1970GMTMicroSeconds);
    LL_L2I(result,time);
    return result;
}

/* Convert from base time to extended time */
PR_PUBLIC_API(int64)
MJ_PR_ToExtendedTime(int32 time)
{
    int64 exttime;
    int64 g1970GMTMicroSeconds;
    int64 low;
    time_t diff;
    int64  tmp;
    int64  tmp1;

    diff = MJ_PR_LocalGMTDifference();
    LL_UI2L(tmp, PR_USEC_PER_SEC);
    LL_I2L(tmp1,diff);
    LL_MUL(tmp,tmp,tmp1);
	
    LL_UI2L(g1970GMTMicroSeconds,G1970GMTMICROHI);
    LL_UI2L(low,G1970GMTMICROLOW);
#ifndef HAVE_LONG_LONG
    LL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,16);
    LL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,16);
#else
    LL_SHL(g1970GMTMicroSeconds,g1970GMTMicroSeconds,32);
#endif
    LL_ADD(g1970GMTMicroSeconds,g1970GMTMicroSeconds,low);

    LL_I2L(exttime,time);
    LL_ADD(exttime,exttime,g1970GMTMicroSeconds);
    LL_SUB(exttime,exttime,tmp);
    return exttime;
}

PR_PUBLIC_API(int64)
MJ_PR_Now(void)
{
#ifdef XP_PC
    int64 s, us, ms2us, s2us;
    struct timeb b;
#endif /* XP_PC */
#ifdef XP_UNIX
    struct timeval tv;
    int64 s, us, s2us;
#endif /* XP_UNIX */
#ifdef XP_MAC
    UnsignedWide upTime;
    int64	 localTime;
    int64       gmtOffset;
    int64    dstOffset;
    time_t       gmtDiff;
    int64	 s2us;
#endif /* XP_MAC */

#ifdef XP_PC
    ftime(&b);
    LL_UI2L(ms2us, PR_USEC_PER_MSEC);
    LL_UI2L(s2us, PR_USEC_PER_SEC);
    LL_UI2L(s, b.time);
    LL_UI2L(us, b.millitm);
    LL_MUL(us, us, ms2us);
    LL_MUL(s, s, s2us);
    LL_ADD(s, s, us);
    return s;
#endif

#ifdef XP_UNIX
#if defined(SOLARIS)
    gettimeofday(&tv);
#else
    gettimeofday(&tv, 0);
#endif /* SOLARIS */
    LL_UI2L(s2us, PR_USEC_PER_SEC);
    LL_UI2L(s, tv.tv_sec);
    LL_UI2L(us, tv.tv_usec);
    LL_MUL(s, s, s2us);
    LL_ADD(s, s, us);
    return s;
#endif /* XP_UNIX */
#ifdef XP_MAC
    LL_UI2L(localTime,0);
    gmtDiff = MJ_PR_LocalGMTDifference();
    LL_I2L(gmtOffset,gmtDiff);
    LL_UI2L(s2us, PR_USEC_PER_SEC);
    LL_MUL(gmtOffset,gmtOffset,s2us);
    LL_UI2L(dstOffset,0);
    dstOffset = MJ_PR_DSTOffset(dstOffset);
    LL_SUB(gmtOffset,gmtOffset,dstOffset);
    /* don't adjust for DST since it sets ctime and gmtime off on the MAC */
    Microseconds(&upTime);
    LL_ADD(localTime,localTime,gmtOffset);
    LL_ADD(localTime,localTime, *((uint64 *)&dstLocalBaseMicroseconds));
    LL_ADD(localTime,localTime, *((uint64 *)&upTime));
	
    return *((uint64 *)&localTime);
#endif /* XP_MAC */
}
/*
** Return the current local time in milli-seconds.
*/
PR_PUBLIC_API(int64) 
MJ_PR_NowMS(void)
{
    int64 us, us2ms;

    us = MJ_PR_Now();
    LL_UI2L(us2ms, PR_USEC_PER_MSEC);
    LL_DIV(us, us, us2ms);
    
    return us;
}

/*
** Return the current local time in seconds.
*/
PR_PUBLIC_API(int64)
MJ_PR_NowS(void)
{
    int64 us, us2s;

    us = MJ_PR_Now();
    LL_UI2L(us2s, PR_USEC_PER_SEC);
    LL_DIV(us, us, us2s);
    return us;
}

/* Get the DST timezone offset for the time passed in 
*/
PR_PUBLIC_API(int64)
MJ_PR_DSTOffset(int64 time)
{
    int64 us2s;
#ifdef XP_MAC
    MachineLocation  machineLocation;
    int64 dlsOffset;
    /*	Get the information about the local machine, including
    **	its GMT offset and its daylight savings time info.
    **	Convert each into wides that we can add to 
    **	startupTimeMicroSeconds.
    */
    MyReadLocation(&machineLocation);
    /* Is Daylight Savings On?  If so, we need to add an hour to the offset. */
    if (machineLocation.u.dlsDelta != 0) {
	LL_UI2L(us2s, PR_USEC_PER_SEC); /* seconds in a microseconds */
	LL_UI2L(dlsOffset, PR_HOUR_SECONDS);  /* seconds in one hour       */
	LL_MUL(dlsOffset, dlsOffset, us2s);
    }
    else 
	LL_I2L(dlsOffset, 0);
    return(dlsOffset);
#else
    time_t local;
    int32 diff;
    int64  maxtimet;
    struct tm tm;
#ifdef XP_PC
    struct tm *ptm;
#endif
    PRTime prtm;


    LL_UI2L(us2s, PR_USEC_PER_SEC);
    LL_DIV(time, time, us2s);
    /* get the maximum of time_t value */
    LL_UI2L(maxtimet,PR_MAX_UNIX_TIMET);

    if(LL_CMP(time,>,maxtimet)){
      LL_UI2L(time,PR_MAX_UNIX_TIMET);
    } else if(!LL_GE_ZERO(time)){
      /*go ahead a day to make localtime work (does not work with 0) */
      LL_UI2L(time,PR_DAY_SECONDS);
    }
    LL_L2UI(local,time);
    MJ_PR_basetime(time,&prtm);
#ifdef XP_PC
    ptm = localtime(&local);
    if(!ptm){
      return LL_ZERO;
    }
    tm = *ptm;
#else
    localtime_r(&local,&tm); /* get dst information */
#endif

    diff = ((tm.tm_hour - prtm.tm_hour) * PR_HOUR_SECONDS) +
	((tm.tm_min - prtm.tm_min) * 60);

    if(diff < 0){
	diff += PR_DAY_SECONDS;
    }

    LL_UI2L(time,diff);
	
    LL_MUL(time,time,us2s);
    
    return(time);
#endif
}

/*
** This function gets the current time and adjusts it to be local time
** (this involves taking into account DST which PR_Now does not do)
*/
PR_PUBLIC_API(int64)
MJ_PR_NowLocal(void)
{
    int64 now;
#ifdef NEVER
    int64 dstOffset;
#endif

    now = MJ_PR_Now();
#ifdef NEVER
#ifndef XP_MAC
    dstOffset = MJ_PR_DSTOffset(now);
#else
    LL_UI2L(dstOffset,0);
#endif /* XP_MAC */
    LL_ADD(now,now,dstOffset);
#endif

    return(now);
}


/* Convert a local time value into a GMT time value */
PR_PUBLIC_API(int64)
MJ_PR_ToGMT(int64 time)
{	
    time_t gmtDiff;
    int64 s2us, diff;
#ifdef XP_MAC
    int64 dstdiff;
#endif /* XP_MAC */

    gmtDiff = MJ_PR_LocalGMTDifference();
    LL_I2L(diff, gmtDiff);
    LL_UI2L(s2us, PR_USEC_PER_SEC);
    LL_MUL(diff,diff,s2us);
#ifdef NEVER
#ifdef XP_MAC
    dstdiff = MJ_PR_DSTOffset(time);
    LL_SUB(diff,diff,dstdiff);
#endif /* XP_MAC */
#endif /* NEVER */
    LL_ADD(time,time,diff);
    return time;
}

/* Convert a GMT time value into a local time value */
PR_PUBLIC_API(int64)
MJ_PR_ToLocal(int64 time)
{	
    time_t gmtDiff;
    int64 s2us, diff, dstdiff;

    gmtDiff = MJ_PR_LocalGMTDifference();
    
    dstdiff = MJ_PR_DSTOffset(time);
    LL_I2L(diff, gmtDiff);
    LL_UI2L(s2us, PR_USEC_PER_SEC);
    LL_MUL(diff,diff,s2us);
    LL_SUB(time,time,diff);
    LL_ADD(time,time,dstdiff);
    return time;
}


/* Explode a 64 bit time value into its components */
PR_PUBLIC_API(void)
MJ_PR_ExplodeTime(PRTime *to, int64 time)
{
    int64 s, us2s, us;
    
    /* Convert back to seconds since 0 */
    LL_UI2L(us2s, PR_USEC_PER_SEC);
    LL_DIV(s, time, us2s);
    LL_MOD(us, time, us2s);
    MJ_PR_localtime(s,to);
    LL_L2I(to->tm_usec, us);
}

/* Compute the 64 bit time value from the components */
PR_PUBLIC_API(int64)
MJ_PR_ComputeTime(PRTime *prtm)
{
    int64 s, us, s2us;

    s = MJ_PR_mktime(prtm);

    LL_UI2L(s2us, PR_USEC_PER_SEC);

    LL_UI2L(us, prtm->tm_usec);
    LL_MUL(s, s, s2us);
    LL_ADD(s, s, us);
    return s;
}

/* table for number of days in a month */
static int mtab[] = {
  /* jan, feb,mar,apr,may,jun */
  31,28,31,30,31,30,
  /* july,aug,sep,oct,nov,dec */
  31,31,30,31,30,31
};

/* 
** This function replaces the mktime on each platform. mktime unfortunately
** only handles time from January 1st 1970 until January 1st 2038. This
** is not sufficient for most applications. This application will produce
** time in seconds for any date.
** XXX We also need to account for leap seconds...
*/
PR_PUBLIC_API(int64)
MJ_PR_mktime(PRTime *prtm)
{
  int64 seconds;
  int64 result;
  int64 result1;
  int64 result2;
  int64 base;
  time_t secs;
  int32   year = prtm->tm_year;
  int32   month = prtm->tm_mon;
  int32   day   = prtm->tm_mday;
  int32   isleap = IS_LEAP(year);
  struct tm time;
#ifdef XP_MAC
  int64  dstdiff;
  int64  s2us;
  int32  dstOffset;
#endif

  /* if between years we support just use standard mktime */
  if(year >= 1970 && year <= 2037){
      time.tm_sec  = prtm->tm_sec ;
      time.tm_min  = prtm->tm_min ;
      time.tm_hour = prtm->tm_hour;
      time.tm_mday = prtm->tm_mday;
      time.tm_mon  = prtm->tm_mon ;
      time.tm_wday = prtm->tm_wday;
      time.tm_year = prtm->tm_year-1900;
      time.tm_yday = prtm->tm_yday;
      time.tm_isdst = prtm->tm_isdst;
      if((secs = mktime(&time)) < 0){
	/* out of range use extended time */
	goto extended_time;
      }
#ifdef XP_MAC
      /* adjust MAC time to make relative to UNIX epoch */
      secs -= gJanuaryFirst1970Seconds;
      secs += MJ_PR_LocalGMTDifference();
      LL_UI2L(dstdiff,0)
      dstdiff = MJ_PR_DSTOffset(dstdiff);
      LL_UI2L(s2us,  PR_USEC_PER_SEC);
      LL_DIV(dstdiff,dstdiff,s2us);
      LL_L2I(dstOffset,dstdiff);
      secs -= dstOffset;
      MJ_PR_setDST(prtm);
#else
      prtm->tm_isdst = time.tm_isdst;
#endif
      prtm->tm_mday = time.tm_mday;
      prtm->tm_wday = time.tm_wday;
      prtm->tm_yday = time.tm_yday;
      prtm->tm_hour = time.tm_hour;
      prtm->tm_min  = time.tm_min;
      prtm->tm_mon  = time.tm_mon;

      LL_UI2L(seconds,secs);
      return(seconds);
  }
  
extended_time:
  LL_UI2L(seconds,0);
  LL_UI2L(result,0);
  LL_UI2L(result1,0);
  LL_UI2L(result2,0);

  /* calculate seconds in years */
  if(year > 0){
    LL_UI2L(result,year);
    LL_UI2L(result1,(365L * 24L));
    LL_MUL(result,result,result1);
    LL_UI2L(result1,PR_HOUR_SECONDS);
    LL_MUL(result,result,result1);
    LL_UI2L(result1,((year-1)/4 - (year-1)/100 + (year-1)/400));
    LL_UI2L(result2,PR_DAY_SECONDS);
    LL_MUL(result1,result1,result2);
    LL_ADD(seconds,result,result1);
  }

  /* calculate seconds in months */
  month--;

  for(;month >= 0; month--){
    LL_UI2L(result,(PR_DAY_SECONDS * mtab[month]));
    LL_ADD(seconds,seconds,result);
    /* it's  a Feb */
    if(month == 1 && isleap != 0){
      LL_UI2L(result,PR_DAY_SECONDS);
      LL_ADD(seconds,seconds,result);
    }
  }

  /* get the base time via UTC */
  base = MJ_PR_ToExtendedTime(0);
  LL_UI2L(result,  PR_USEC_PER_SEC);
  LL_DIV(base,base,result);

  /* calculate seconds for days */
  LL_UI2L(result,((day-1) * PR_DAY_SECONDS));
  LL_ADD(seconds,seconds,result);
  /* calculate seconds for hours, minutes and seconds */
  LL_UI2L(result, (prtm->tm_hour * PR_HOUR_SECONDS + prtm->tm_min * 60 +
		   prtm->tm_sec));
  LL_ADD(seconds,seconds,result);
  /* normalize to time base on positive for 1970, - for before that period */
  LL_SUB(seconds,seconds,base);

  /* set dst information */
  if(prtm->tm_isdst < 0)
      prtm->tm_isdst = 0;
  return seconds;
}

/*
** basic time calculation functionality for localtime and gmtime
** setups up prtm argument with correct values based upon input number
** of seconds.
*/
static void
MJ_PR_basetime(int64 tsecs, PRTime *prtm)
{
    /* convert tsecs back to year,month,day,hour,secs */
    int32 year    = 0;
    int32 month   = 0;
    int32 yday    = 0;
    int32 mday    = 0;
    int32 wday    = 6; /* start on a Sunday */
    int32 days    = 0;
    int32 seconds = 0;
    int32 minutes = 0;
    int32 hours   = 0;
    int32 isleap  = 0;
    int64 result;
    int64	result1;
    int64	result2;
    int64 base;

    LL_UI2L(result,0);
    LL_UI2L(result1,0);
    LL_UI2L(result2,0);

    /* get the base time via UTC */
    base = MJ_PR_ToExtendedTime(0);
    LL_UI2L(result,  PR_USEC_PER_SEC);
    LL_DIV(base,base,result);
    LL_ADD(tsecs,tsecs,base);

    LL_UI2L(result, PR_YEAR_SECONDS);
    LL_UI2L(result1,PR_DAY_SECONDS);
    LL_ADD(result2,result,result1);

  /* get the year */
    while((isleap == 0 && (LL_CMP(tsecs, >,result) || LL_EQ(tsecs,result))) ||
	  (isleap != 0 && (LL_CMP(tsecs, >,result2) || LL_EQ(tsecs,result2)))){
	/* subtract a year from tsecs */
	LL_SUB(tsecs,tsecs,result);
	days += 365;
	/* is it a leap year ? */
	if(IS_LEAP(year)){
	    LL_SUB(tsecs,tsecs,result1);
	    days++;
	}
	year++;
	isleap = IS_LEAP(year);
    }

    LL_UI2L(result1,PR_DAY_SECONDS);

    LL_DIV(result,tsecs,result1);
    LL_L2I(mday,result);
  
  /* let's find the month */
    while(((month == 1 && isleap) ? 
	   (mday >= mtab[month] + 1) :
	   (mday >= mtab[month]))){
	yday += mtab[month];
	days += mtab[month];

	mday -= mtab[month];

    /* it's a Feb, check if this is a leap year */
	if(month == 1 && isleap != 0){
	    yday++;
	    days++;
	    mday--;
	}
	month++;
    }
  
    /* now adjust tsecs */
    LL_MUL(result,result,result1);
    LL_SUB(tsecs,tsecs,result);

    mday++; /* day of month always start with 1 */
    days += mday;
    wday = (days + wday) % 7;

    yday += mday;

    /* get the hours */
    LL_UI2L(result1,PR_HOUR_SECONDS);
    LL_DIV(result,tsecs,result1);
    LL_L2I(hours,result);
    LL_MUL(result,result,result1);
    LL_SUB(tsecs,tsecs,result);


    /* get minutes */
    LL_UI2L(result1,60);
    LL_DIV(result,tsecs,result1);
    LL_L2I(minutes,result);
    LL_MUL(result,result,result1);
    LL_SUB(tsecs,tsecs,result);

    LL_L2I(seconds,tsecs);

    prtm->tm_usec  = 0L;
    prtm->tm_sec   = seconds;
    prtm->tm_min   = minutes;
    prtm->tm_hour = hours;
    prtm->tm_mday  = mday;
    prtm->tm_mon   = month;
    prtm->tm_wday  = wday;
    prtm->tm_year  = year;
    prtm->tm_yday  = yday;
}

/* 
** This function replaces the localtime on each platform. localtime
** unfortunately only handles time from January 1st 1970 until January 1st
** 2038. This is not sufficient for most applications. This application will
** produce time in seconds for any date.
** TO Fix:
** We also need to account for leap seconds...
*/
PR_PUBLIC_API(void)
MJ_PR_localtime(int64 tsecs,PRTime *prtm)
{
    time_t seconds;
    int32    year;
    struct tm lt;
#ifdef XP_MAC
    int64  dstdiff;
    int64  s2us;
    int32  dstOffset;
#endif
#ifdef XP_MAC
    LL_UI2L(dstdiff,0);
    dstdiff = MJ_PR_DSTOffset(dstdiff);
    LL_UI2L(s2us,  PR_USEC_PER_SEC);
    LL_DIV(dstdiff,dstdiff,s2us);
    LL_L2I(dstOffset,dstdiff);
    LL_ADD(tsecs,tsecs,dstdiff);
#endif
    MJ_PR_basetime(tsecs,prtm);
    
    /* Adjust DST information in prtm
     * possible for us now only between 1970 and 2037
     */
    if((year = prtm->tm_year) >= 1970 && year <= 2037){
	LL_L2I(seconds,tsecs);
#ifdef XP_MAC
	/* adjust to the UNIX epoch  and add DST*/
	seconds += gJanuaryFirst1970Seconds;
	seconds -= MJ_PR_LocalGMTDifference();
	seconds += dstOffset;
#endif
#if defined(XP_PC) || defined(XP_MAC)
	lt = *localtime(&seconds);
#else
	localtime_r(&seconds,&lt);
#endif
#ifdef XP_MAC
	MJ_PR_setDST(prtm);
#else
	prtm->tm_isdst = lt.tm_isdst;
#endif
	prtm->tm_mday  = lt.tm_mday;
	prtm->tm_wday  = lt.tm_wday;
	prtm->tm_yday  = lt.tm_yday;
	prtm->tm_hour  = lt.tm_hour;
	prtm->tm_min   = lt.tm_min;
	prtm->tm_mon   = lt.tm_mon;
	prtm->tm_sec   = lt.tm_sec;
	prtm->tm_usec  = 0;
    }
    else
	prtm->tm_isdst = 0;
}



/* 
** This function takes the local time unadjusted for DST and returns
** the GMT time.
*/
PR_PUBLIC_API(void)
MJ_PR_gmtime(int64 tsecs,PRTime *prtm)
{
    int64 s2us;

    LL_UI2L(s2us, PR_USEC_PER_SEC);
    LL_MUL(tsecs,tsecs,s2us);

    tsecs = MJ_PR_ToGMT(tsecs);

    LL_DIV(tsecs,tsecs,s2us);
    MJ_PR_basetime(tsecs,prtm);
}    
