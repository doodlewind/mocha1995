/*
** Mocha date methods.
**
** Ken Smith and Brendan Eich, 12/8/95
*/

#include <ctype.h>
#include <string.h>
#include "prprf.h"
#include "prmjtime.h"
#include "mo_atom.h"
#include "mo_cntxt.h"
#include "mocha.h"
#include "mochaapi.h"
#include "mochalib.h"

/***********************************************************************
**
**                  M o c h a   D A T E   O b j e c t
**
**
** The mocha 'Date' object is patterned after the java 'Date'
** object. Here is some example mocha code:
**
**    today = new Date();
**
**    print(today.toLocaleString());
**
**    weekDay = today.getDay();
**
**
** These (java) methods are supported:
**
**     UTC
**     getDate
**     getDay
**     getHours
**     getMinutes
**     getMonth
**     getSeconds
**     getTime
**     getTimezoneOffset
**     getYear
**     parse
**     setDate
**     setHours
**     setMinutes
**     setMonth
**     setSeconds
**     setTime
**     setYear
**     toGMTString
**     toLocaleString
**     toString
**
**
** These (java) methods are not supported
**
**     setDay
**     before
**     after
**     equals
**     hashCode
**
**
** How is a mocha date like a java date?
**
** 1. same name -- Date
** 2. share underlying date code -- from nspr
** 3. time is milliseconds from an epoch
** 4. methods that are supported, work the same way
** 5. parse code is a direct conversion from java
**
**
** How is a mocha date different from a java date.
**
** 1. numeric form of date (such as return from getTime()) is
**    a C 'double' instead of java's 'int64'.
** 2. not all java methods are supported
*/



/***********************************************************************
** Support routines and definitions
*/

#define DATE_ERROR 0
#define MAX_YEAR   3000

typedef struct DateObject {
    int64 value; /* microseconds, like nspr */
    PRTime split;
    MochaBoolean valid;
} DateObject;

static void
date_finalize(MochaContext *mc, MochaObject *obj)
{
    DateObject *dateObj;

    dateObj = obj->data;
    MOCHA_free(mc, dateObj);
}

static MochaClass date_class = {
    "Date",
    MOCHA_PropertyStub, MOCHA_PropertyStub, MOCHA_ListPropStub,
    MOCHA_ResolveStub, MOCHA_ConvertStub, date_finalize
};

static void date_implode(DateObject* dateObj);

static char* wtb[] = {
    "am", "pm",
    "monday", "tuesday", "wednesday", "thursday", "friday",
    "saturday", "sunday",
    "january", "february", "march", "april", "may", "june",
    "july", "august", "september", "october", "november", "december",
    "gmt", "ut", "utc", "est", "edt", "cst", "cdt",
    "mst", "mdt", "pst", "pdt"
    /* time zone table needs to be expanded */
};

static int ttb[] = {
    0, 1, 0, 0, 0, 0, 0, 0, 0,
    2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
    10000 + 0, 10000 + 0, 10000 + 0, /* UT/UTC */
    10000 + 5 * 60, 10000 + 4 * 60, /* EDT */
    10000 + 6 * 60, 10000 + 5 * 60,
    10000 + 7 * 60, 10000 + 6 * 60,
    10000 + 8 * 60, 10000 + 7 * 60
};

static void
date_explode(DateObject* dateObj)
{
    if (dateObj->valid == MOCHA_FALSE) {
        MJ_PR_ExplodeTime( &dateObj->split, dateObj->value );
        dateObj->value = MJ_PR_ComputeTime( &dateObj->split );
        dateObj->valid = MOCHA_TRUE;
    }
}

static void
date_implode(DateObject* dateObj)
{
    if (dateObj->valid == MOCHA_FALSE) {
        dateObj->value = MJ_PR_ComputeTime( &dateObj->split );
        MJ_PR_ExplodeTime( &dateObj->split, dateObj->value );
        dateObj->valid = MOCHA_TRUE;
    }
}


static MochaBoolean
date_regionMatches(const char* s1, int s1off, const char* s2, int s2off,
                   int count, int ignoreCase)
{
    int result = MOCHA_FALSE;
    /* return true if matches, otherwise, false */

    while ( count > 0 && *(s1+s1off) && (s2+s2off) ) {
        if ( ignoreCase ) {
            if ( tolower(*(s1+s1off)) != tolower(*(s2+s2off)) ) {
                break;
            }
        }
        else {
            if ( *(s1+s1off) != *(s2+s2off) ) {
                break;
            }
        }
        s1off++;
        s2off++;
        count--;
    }

    if ( count == 0 ) {
        result = MOCHA_TRUE;
    }

    return result;
}

static int64
date_localTime(int year, int mon, int mday, int hour, int min, int sec)
{
    int64 result;
    struct PRTimeStr split;

    /* check for year out of range */
    if(year < 0 ){
	/* invalid date */
	year = 0;
    }

    if(year > MAX_YEAR){
	year = MAX_YEAR;
    }

    if ( year < 100 )
        year += 1900;

    split.tm_year = year;
    split.tm_mon = mon;
    split.tm_mday = mday;
    split.tm_hour = hour;
    split.tm_min = min;
    split.tm_sec = sec;

    split.tm_usec = 0;
    split.tm_wday = 0;
    split.tm_yday = 0;
    split.tm_isdst = -1; /* XXX put the right value here */

    result = MJ_PR_ComputeTime( &split );

    return result;
}



static int64
date_UTCTime(int year, int mon, int mday, int hour, int min, int sec)
{
    int64 localTime;

    localTime = date_localTime(year, mon, mday, hour, min, sec);

    /* adjust this "GMT" time and convert it to a local time */
    return MJ_PR_ToLocal(localTime);
}



/* XXX this function must be above date_parseString to avoid a
horrid bug in the win16 1.52 compiler */
static MochaBoolean
date_UTC(MochaContext *mc, MochaObject *obj,
	 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    int array[6];
    int loop;
    MochaFloat fval;
    int64 UTCTime; /* microseconds */
    int64 UTCTimeMS; /* milliseconds */
    int64 oneThousand;
    MochaFloat UTCTimeMSDouble;

    for ( loop = 0; loop < 6; loop++ ) {
        if ( loop < argc ) {
	    if (!mocha_DatumToNumber(mc,argv[loop],&fval))
		return MOCHA_FALSE;
            array[loop] = (int)fval;
        } else {
            array[loop] = 0;
        }
    }

    UTCTime = date_UTCTime(array[0],array[1],array[2],
			   array[3],array[4],array[5]);

    LL_I2L(oneThousand, 1000);
    LL_DIV(UTCTimeMS, UTCTime, oneThousand);
    LL_L2D(UTCTimeMSDouble, UTCTimeMS);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, UTCTimeMSDouble);

    return MOCHA_TRUE;
}

static int64
date_parseString(const char* s)
{
    int64 result, temp1, temp2, temp3;

    int year = -1;
    int mon = -1;
    int mday = -1;
    int hour = -1;
    int min = -1;
    int sec = -1;
    int c = -1;
    int i = 0;
    int n = -1;
    int tzoffset = -1;
    int prevc = 0;
    int limit = 0;

    if (s == 0)
        goto syntax;
    limit = strlen(s);
    while (i < limit) {
        c = s[i];
        i++;
        if (c <= ' ' || c == ',' || c == '-'){
	    if (c == '-' && '0' <= s[i] && s[i] <= '9') {
	      prevc = c;
	    }
            continue;
	}
        if (c == '(') { /* comments */
            int depth = 1;
            while (i < limit) {
                c = s[i];
                i++;
                if (c == '(') depth++;
                else if (c == ')')
                    if (--depth <= 0)
                        break;
            }
            continue;
        }
        if ('0' <= c && c <= '9') {
            n = c - '0';
            while (i < limit && '0' <= (c = s[i]) && c <= '9') {
                n = n * 10 + c - '0';
                i++;
            }
            if ((prevc == '+' || prevc == '-') && year>=0) {
                /* offset */
                if (n < 24)
                    n = n * 60; /* EG. "GMT-3" */
                else
                    n = n % 100 + n / 100 * 60; /* eg "GMT-0430" */
                if (prevc == '+')       /* plus means east of GMT */
                    n = -n;
                if (tzoffset != 0 && tzoffset != -1)
                    goto syntax;
                tzoffset = n;
            } else if (n >= 70  ||
		       (prevc == '/' && mon >= 0 && mday >= 0 && year < 0)) {
                if (year >= 0)
                    goto syntax;
                else if (c <= ' ' || c == ',' || c == '/' || i >= limit)
                    year = n < 100 ? n + 1900 : n;
                else
                    goto syntax;
            } else if (c == ':') {
                if (hour < 0)
                    hour = /*byte*/ n;
                else if (min < 0)
                    min = /*byte*/ n;
                else
                    goto syntax;
            } else if (c == '/') {
                if (mon < 0)
                    mon = /*byte*/ n-1;
                else if (mday < 0)
                    mday = /*byte*/ n;
                else
                    goto syntax;
            } else if (i < limit && c != ',' && c > ' ' && c != '-') {
                goto syntax;
            } else if (hour >= 0 && min < 0) {
                min = /*byte*/ n;
            } else if (min >= 0 && sec < 0) {
                sec = /*byte*/ n;
            } else if (mday < 0) {
                mday = /*byte*/ n;
            } else {
                goto syntax;
            }
	    prevc = 0;
        } else if (c == '/' || c == ':' || c == '+' || c == '-') {
            prevc = c;
        } else {
            int st = i - 1;
            int k;
            while (i < limit) {
                c = s[i];
                if (!(('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z')))
                    break;
                i++;
            }
            if (i <= st + 1)
                goto syntax;
            for (k = (sizeof(wtb)/sizeof(char*)); --k >= 0;)
                if (date_regionMatches(wtb[k], 0, s, st, i-st, 1)) {
                    int action = ttb[k];
                    if (action != 0)
                        if (action == 1) /* pm */
                            if (hour > 12 || hour < 0)
                                goto syntax;
                            else
                                hour += 12;
                        else if (action <= 13) /* month! */
                            if (mon < 0)
                                mon = /*byte*/ (action - 2);
                            else
                                goto syntax;
                        else
                            tzoffset = action - 10000;
                    break;
                }
            if (k < 0)
                goto syntax;
            prevc = 0;
        }
    }
    if (year < 0 || mon < 0 || mday < 0)
        goto syntax;
    if (sec < 0)
        sec = 0;
    if (min < 0)
        min = 0;
    if (hour < 0)
        hour = 0;
    if (tzoffset == -1) { /* no time zone specified, have to use local */
        return date_localTime(year, mon, mday, hour, min, sec);
    }

    /*
    ** return (date_UTCTime(year, mon, mday, hour, min, sec) +
    **         tzoffset * (60 * 1000));
    */

    temp1 = date_UTCTime(year, mon, mday, hour, min, sec);
    LL_UI2L(temp2,60000000);
    LL_I2L(temp3,tzoffset);
    LL_MUL(temp2,temp2,temp3);
    LL_ADD(temp3,temp1,temp2);
    return temp3;

syntax:
    /* syntax error */
    LL_I2L(result,DATE_ERROR);
    return result;
}


/***********************************************************************
** methods
*/


static MochaBoolean
date_getDate(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, dateObj->split.tm_mday );
    return MOCHA_TRUE;
}

static MochaBoolean
date_getDay(MochaContext *mc, MochaObject *obj,
	    unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, dateObj->split.tm_wday );
    return MOCHA_TRUE;
}

static MochaBoolean
date_getHours(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, dateObj->split.tm_hour );
    return MOCHA_TRUE;
}

static MochaBoolean
date_getMinutes(MochaContext *mc, MochaObject *obj,
		unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, dateObj->split.tm_min );
    return MOCHA_TRUE;
}

static MochaBoolean
date_getMonth(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, dateObj->split.tm_mon );
    return MOCHA_TRUE;
}

static MochaBoolean
date_getSeconds(MochaContext *mc, MochaObject *obj,
		unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, dateObj->split.tm_sec );
    return MOCHA_TRUE;
}

static MochaBoolean
date_getTime(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    int64 oneThousand;
    int64 valueMS;
    MochaFloat theTime;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_implode( dateObj );

    LL_I2L(oneThousand, 1000);
    LL_DIV(valueMS, dateObj->value, oneThousand);

    LL_L2D(theTime,valueMS);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, theTime);

    return MOCHA_TRUE;
}

static MochaBoolean
date_getTimezoneOffset(MochaContext *mc, MochaObject *obj,
		       unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    int64 timezoneOffset; /* in minutes */
    int64 UTCTime;
    int64 currentTime;
    int64 diffTime;
    int64 sixtymillion; /* microseconds per minute */
    MochaFloat result;

    /*
    ** Return the time zone offset in minutes for the current locale
    ** that is appropriate for this time. This value would be a
    ** constant except for daylight savings time.
    */
    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );

    /* get the current time from the object (computed in date_explode) */
    currentTime = dateObj->value;

    /* convert current time to GMT */
    UTCTime = MJ_PR_ToGMT(currentTime);
    /* get the DST offset but not if a Mac with weird local time */
    diffTime = MJ_PR_DSTOffset(currentTime);

    LL_SUB(diffTime,UTCTime,diffTime);
    LL_SUB(diffTime,diffTime,currentTime);
    LL_I2L(sixtymillion,60000000);
    LL_DIV(timezoneOffset,diffTime,sixtymillion);

    LL_L2D(result,timezoneOffset);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, result);

    return MOCHA_TRUE;
}

static MochaBoolean
date_getYear(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );

    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval,
		     (dateObj->split.tm_year >= 1900 &&
		      dateObj->split.tm_year < 2000) ?
		     (dateObj->split.tm_year -1900) :
		     dateObj->split.tm_year);
    return MOCHA_TRUE;
}

static MochaBoolean
date_parse(MochaContext *mc, MochaObject *obj,
	   unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    MochaAtom *atom;
    int64 theDateTime;
    int64 oneThousand;
    MochaFloat result;

    if (!mocha_DatumToString(mc,argv[0],&atom))
	return MOCHA_FALSE;
    theDateTime = date_parseString(atom_name(atom));
    mocha_DropAtom(mc,atom);
    LL_I2L(oneThousand, 1000);
    LL_DIV(theDateTime, theDateTime, oneThousand);
    LL_L2D(result, theDateTime);
    MOCHA_INIT_DATUM(mc, rval, MOCHA_NUMBER, u.fval, result);

    return MOCHA_TRUE;
}

static MochaBoolean
date_setDate(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    MochaFloat fval;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
    if (!mocha_DatumToNumber(mc,argv[0],&fval)) return MOCHA_FALSE;
    dateObj->split.tm_mday = (int)fval;
    dateObj->valid = MOCHA_FALSE;
    date_implode( dateObj );
    return MOCHA_TRUE;
}

static MochaBoolean
date_setMinutes(MochaContext *mc, MochaObject *obj,
		unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    MochaFloat fval;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    if (!mocha_DatumToNumber(mc,argv[0],&fval)) return MOCHA_FALSE;

    date_explode( dateObj );
    dateObj->split.tm_min = (int)fval;
    dateObj->valid = MOCHA_FALSE;
    date_implode( dateObj );
    return MOCHA_TRUE;
}

static MochaBoolean
date_setHours(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    MochaFloat fval;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    if (!mocha_DatumToNumber(mc,argv[0],&fval)) return MOCHA_FALSE;

    date_explode( dateObj );
    dateObj->split.tm_hour = (int)fval;
    dateObj->valid = MOCHA_FALSE;
    date_implode( dateObj );
    return MOCHA_TRUE;
}

static MochaBoolean
date_setMonth(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    MochaFloat fval;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    if (!mocha_DatumToNumber(mc,argv[0],&fval)) return MOCHA_FALSE;

    date_explode( dateObj );
    dateObj->split.tm_mon = (int)fval;
    dateObj->valid = MOCHA_FALSE;
    date_implode( dateObj );
    return MOCHA_TRUE;
}

static MochaBoolean
date_setSeconds(MochaContext *mc, MochaObject *obj,
		unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    MochaFloat fval;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    if (!mocha_DatumToNumber(mc,argv[0],&fval)) return MOCHA_FALSE;

    date_explode( dateObj );
    dateObj->split.tm_sec = (int)fval;
    dateObj->valid = MOCHA_FALSE;
    date_implode( dateObj );

    return MOCHA_TRUE;
}

static MochaBoolean
date_setTime(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    MochaFloat fval;
    int64 oneThousand;
    int64 theTimeMS;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    if (!mocha_DatumToNumber(mc,argv[0],&fval)) return MOCHA_FALSE;

    LL_I2L( oneThousand, 1000 );
    LL_D2L( theTimeMS, fval );
    LL_MUL( dateObj->value, theTimeMS, oneThousand );

    dateObj->valid = MOCHA_FALSE;
    date_explode( dateObj );
    return MOCHA_TRUE;
}

static MochaBoolean
date_setYear(MochaContext *mc, MochaObject *obj,
	     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    MochaFloat fval;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
    if (!mocha_DatumToNumber(mc,argv[0],&fval)) return MOCHA_FALSE;
    dateObj->split.tm_year = (int)fval;
    if ( dateObj->split.tm_year < 100 ) {
        dateObj->split.tm_year += 1900;
    }
    dateObj->valid = MOCHA_FALSE;
    date_implode( dateObj );
    return MOCHA_TRUE;
}

static MochaBoolean
date_toGMTString(MochaContext *mc, MochaObject *obj,
		 unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    char buf[100];
    MochaAtom* atom;
    struct PRTimeStr theGmtSplit;
    int64 theGmtTime;
    int64 us;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_implode( dateObj );

    /* get the number of seconds */
    LL_UI2L(us,PR_USEC_PER_SEC);
    LL_DIV(theGmtTime,dateObj->value,us);

    MJ_PR_gmtime(theGmtTime, &theGmtSplit );

    PR_FormatTimeUSEnglish(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S GMT",
                           &theGmtSplit);
    atom = mocha_Atomize(mc, buf, ATOM_STRING);
    if (atom)
	MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    else
	*rval = MOCHA_empty;
    return MOCHA_TRUE;
}

static MochaBoolean
date_toLocaleString(MochaContext *mc, MochaObject *obj,
		    unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    char buf[100];
    MochaAtom* atom;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;

    date_explode( dateObj );
    PR_FormatTime(buf, sizeof buf, "%c", &dateObj->split);

    atom = mocha_Atomize(mc, buf, ATOM_STRING);
    if (atom)
	MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    else
	*rval = MOCHA_empty;
    return MOCHA_TRUE;
}

static MochaBoolean
date_toString(MochaContext *mc, MochaObject *obj,
	      unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    DateObject *dateObj;
    char buf[100];
    MochaAtom* atom;

    if (!MOCHA_InstanceOf(mc, obj, &date_class, argv[-1].u.fun))
	return MOCHA_FALSE;
    dateObj = obj->data;
    date_explode( dateObj );
#if XP_MAC
    PR_FormatTime(buf, sizeof buf, "%a %b %d %H:%M:%S  %Y",
                  &dateObj->split);
#else
    PR_FormatTime(buf, sizeof buf, "%a %b %d %H:%M:%S %Z %Y",
                  &dateObj->split);
#endif

    atom = mocha_Atomize(mc, buf, ATOM_STRING);
    if (atom)
	MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
    else
	*rval = MOCHA_empty;
    return MOCHA_TRUE;
}


/***********************************************************************
** creation and destruction
*/

static MochaFunctionSpec date_static_methods[] = {
    {"UTC",               date_UTC,               6 },
    {"parse",             date_parse,             1 },
    {0}
};

static MochaFunctionSpec date_methods[] = {
    {"getDate",           date_getDate,           0 },
    {"getDay",            date_getDay,            0 },
    {"getHours",          date_getHours,          0 },
    {"getMinutes",        date_getMinutes,        0 },
    {"getMonth",          date_getMonth,          0 },
    {"getSeconds",        date_getSeconds,        0 },
    {"getTime",           date_getTime,           0 },
    {"getTimezoneOffset", date_getTimezoneOffset, 0 },
    {"getYear",           date_getYear,           0 },
    {"setDate",           date_setDate,           1 },
    {"setHours",          date_setHours,          1 },
    {"setMinutes",        date_setMinutes,        1 },
    {"setMonth",          date_setMonth,          1 },
    {"setSeconds",        date_setSeconds,        1 },
    {"setTime",           date_setTime,           1 },
    {"setYear",           date_setYear,           1 },
    {"toGMTString",       date_toGMTString,       0 },
    {"toLocaleString",    date_toLocaleString,    0 },
    {mocha_toStringStr,   date_toString,          0 },
    {mocha_valueOfStr,    date_getTime,           0 },
    {0}
};

static DateObject*
date_constructor(MochaContext *mc, MochaObject* obj)
{
    DateObject* dateObj;

    dateObj = MOCHA_malloc(mc, sizeof *dateObj);
    if (!dateObj) return 0;
    memset( (char*)dateObj, 0, sizeof *dateObj );
    /* need to set tm_isdst to -1 to get auto date operations for
     * local time to work
     */
    dateObj->split.tm_isdst = -1;
    obj->data = dateObj;
    return dateObj;
}


static MochaBoolean
Date(MochaContext *mc, MochaObject *obj,
     unsigned argc, MochaDatum *argv, MochaDatum *rval)
{
    /*
    ** REQUIREMENT: The Date() funtion is set up so argv contains
    ** at least 6 arguments (even though argc doesn't reflect it).
    */
    DateObject* dateObj;

    if (obj->clazz != &date_class) {
	PRTime prtm;
	char buf[100];
	MochaAtom *atom;

        /* constructor call should follow 'new', but this one didn't */
        MJ_PR_ExplodeTime(&prtm, MJ_PR_Now());
#if XP_MAC
	PR_FormatTime(buf, sizeof buf, "%a %b %d %H:%M:%S  %Y", &prtm);
#else
	PR_FormatTime(buf, sizeof buf, "%a %b %d %H:%M:%S %Z %Y", &prtm);
#endif

	atom = mocha_Atomize(mc, buf, ATOM_STRING);
	if (atom)
	    MOCHA_INIT_DATUM(mc, rval, MOCHA_STRING, u.atom, atom);
	else
	    *rval = MOCHA_empty;
        return MOCHA_TRUE;
    }

    if ( argc == 0 ) {
        dateObj = date_constructor(mc, obj);
	if (!dateObj) return MOCHA_FALSE;
        dateObj->value = MJ_PR_Now();
        dateObj->valid = MOCHA_FALSE;
        date_explode( dateObj );
    } else if ( argc == 1 ) {
        if ( argv[0].tag == MOCHA_NUMBER ) {
	    MochaFloat fval;
            int64 oneThousand;
            int64 theTimeMS;

            dateObj = date_constructor(mc, obj);
	    if (!dateObj) return MOCHA_FALSE;
	    if (!mocha_DatumToNumber(mc,argv[0],&fval)) return MOCHA_FALSE;
            LL_I2L( oneThousand, 1000 );
            LL_D2L( theTimeMS,fval );
            LL_MUL( dateObj->value, theTimeMS, oneThousand );
            dateObj->valid = MOCHA_FALSE;
            date_explode( dateObj );
        } else {
            MochaAtom *atom;
            int64 theTime;

            if (!mocha_DatumToString(mc,argv[0],&atom))
		return MOCHA_FALSE;
            theTime = date_parseString(atom_name(atom));
	    mocha_DropAtom(mc,atom);

            dateObj = date_constructor(mc, obj);
	    if (!dateObj) return MOCHA_FALSE;
            dateObj->value = theTime;
            dateObj->valid = MOCHA_FALSE;
            date_explode( dateObj );
        }
    } else {
        int array[6];
        int loop;
	MochaFloat fval;

        for ( loop = 0; loop < 6; loop++ ) {
            if ( loop < argc ) {
		if (!mocha_DatumToNumber(mc,argv[loop],&fval))
		    return MOCHA_FALSE;
                array[loop] = (int)fval;
            } else {
                array[loop] = 0;
            }
        }

        dateObj = date_constructor(mc, obj);
	if (!dateObj) return MOCHA_FALSE;

        if ( array[0] < 100 )
            array[0] += 1900;

        dateObj->split.tm_year = array[0];
        dateObj->split.tm_mon = array[1];
        dateObj->split.tm_mday = array[2];
        dateObj->split.tm_hour = array[3];
        dateObj->split.tm_min = array[4];
        dateObj->split.tm_sec = array[5];

        dateObj->split.tm_usec = 0;
        dateObj->split.tm_wday = 0;
        dateObj->split.tm_yday = 0;
        dateObj->split.tm_isdst = -1; /* XXX put the right value here */

        dateObj->valid = MOCHA_FALSE;
        date_implode( dateObj );
    }

    return MOCHA_TRUE;
}

MochaObject *
mocha_InitDateClass(MochaContext *mc, MochaObject *obj)
{
    return MOCHA_InitClass(mc, obj, &date_class, 0, Date, 6,
			   0, date_methods, 0, date_static_methods);
}

