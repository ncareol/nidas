// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2006, Copyright University Corporation for Atmospheric Research
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation; either version 2 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** The LICENSE.txt file accompanying this software contains
 ** a copy of the GNU General Public License. If it is not found,
 ** write to the Free Software Foundation, Inc.,
 ** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 **
 ********************************************************************
*/
/*

    C++ class for time handling.

*/

#ifndef NIDAS_UTIL_UTIME_H
#define NIDAS_UTIL_UTIME_H

#include <sys/types.h>
#include <sys/time.h>
#include <ctime>
#include <iostream>
#include <cctype>
#include <cmath>
#include <climits>

#include <locale>
#include <string>

#include "ThreadSupport.h"
#include "ParseException.h"
#include "time_constants.h"
#include "IOException.h"


namespace nidas { namespace util {

/**
 * A class for parsing, formatting and doing operations on time, based
 * on Unix time conventions, where leap seconds are ignored,
 * so that there are always 60 seconds in a minute, 3600 seconds in an hour
 * and 86400 seconds in a day. Time values are typically assigned by a computer
 * with an NTP controlled clock, and that time is converted to a
 * Unix time as the number of non-leap seconds since Jan 1970 00:00 GMT.
 * Conversion back to human readable time uses the same no-leap-second
 * convention. Time values around the time that a system's NTP clock are
 * being adjusted for a leap second will be indeterminate by up to a second,
 * depending on how the Unix clock on that system was adjusted.
 *
 * UTime supports output to the std::basic_ostream<charT> template class,
 * which includes output to std::ostream, which is std::basic_ostream<char>.
 * The general support for basic_ostream<charT> is only useful to support wide
 * character output, which is not likely, but it works.
 * 
 * There is no explicit support for an "unset" or "null" UTime value.  The
 * default constructor initializes from the current system time; there is no
 * way to construct an invalid or null time.  Instead, historical practice has
 * been to use LONG_LONG_MIN or sometimes just (time_t)0 to indicate an unset
 * UTime.  Therefore the static constants MIN, MAX, and ZERO exist to
 * formalize this practice.  Use Utime(UTime::ZERO) to construct a single time
 * for which isZero() returns true.  Use MIN when the intention is to use the
 * earliest possible UTime value, and similarly for MAX.  The meaning of
 * (time_t)0 should be made explicit by replacing it with MIN, MAX, or ZERO.
 * Instead of comparing a UTime to LONG_LONG_MIN or LONG_LONG_MAX, call
 * isMin() or isMax() or isSet().  Typically, MIN is used as the default to
 * indicate an unset start time while MAX is used for a default end time, so
 * calling isSet() works for either start or end times, returning true if the
 * UTime is neither MIN nor MAX.
 * 
 * The stored microseconds since the Posix epoch is a signed long long, so 0
 * represents the epoch but not the earliest possible UTime.  The minimum
 * possible UTime is represented by the negative value LONG_LONG_MIN.  ZERO
 * and isZero() are available in case an application really does want the
 * default to be the epoch or otherwise needs a convenient UTime constant for
 * the Posix epoch, but generally it should be avoided since the intention is
 * less clear, either zero as epoch or zero as unset.  In particular,
 * ZERO.isSet() returns true.
 */
class UTime {
public:

    /**
     * No-arg constructor initializes to current time, with isUTC() true.
     */
    UTime( );

    /**
     * Constructor.
     * isUTC() will be set to true.
     * @param t Non-leap microseconds since Jan 1, 1970 00:00 UTC
     */
    UTime(long long t): _utime(t),_fmt(),_utc(true) {}

    static const UTime MIN;
    static const UTime MAX;
    static const UTime ZERO;

    /**
     * Return true if this UTime is equivalent to UTime::ZERO.
     */
    bool isZero() const;

    /**
     * Return true if this UTime is equivalent to UTime::MIN.
     */
    bool isMin() const;

    /**
     * Return true if this UTime is equivalent to UTime::MAX.
     */
    bool isMax() const;

    /**
     * Return true if UTime is neither MIN nor MAX.
     */
    bool isSet() const;

    /**
     * Constructor.
     * isUTC() will be set to true.
     * @param t Non-leap seconds since Jan 1, 1970 00:00 UTC
     */
    UTime(time_t t): _utime(fromSecs(t)),_fmt(),_utc(true) { }

    /**
     * Constructor.
     * isUTC() will be set to true.
     * @param t Non-leap seconds since Jan 1, 1970 00:00 UTC
     */
    UTime(double t):  _utime(fromSecs(t)),_fmt(),_utc(true) { }

    /**
     * Constructor from a struct tm. See the fromTm() static method.
     */
    UTime(bool utc,const struct tm* tmp,int usecs = 0);

    /**
     * Constructor. mon is 1-12, day is 1-31.
     * Note that mon differs from the definition of tm_mon in struct tm
     * which is in the range 0-11.
     */
    UTime(bool utc, int year,int mon, int day,int hour, int min, double sec);

    /**
     * Constructor. yday is day of year, 1-366.
     * Note that yday differs from the definition of tm_yday in struct tm
     * which is in the range 0-365.
     */
    UTime(bool utc, int year,int yday,int hour, int min, double sec);

    UTime(const UTime& u) :
        _utime(u._utime),
        _fmt(u._fmt),
        _utc(u._utc)
    { }

    void setFromSecs(time_t t) { _utime = fromSecs(t); }

    /**
     * Set values in a struct tm from a UTime.
     */
    struct tm* toTm(bool utc,struct tm* tmp, int* usecs = 0) const;

    /**
     * Set values in a struct tm from a UTime, using the isUTC() attribute.
     */
    struct tm* toTm(struct tm* tmp, int* usecs = 0) const;

    /**
     * Return number of non-leap micro-seconds since Jan 1970 00:00 UTC
     * computed from time fields in a struct tm. See "man mktime".
     * If the value of tmp->tm_yday is greater than or equal to 0, and
     * tmp->tm_mon is less than 0 or tmp->tm_mday is less than 1, then
     * the result is calculated using tm_yday. Otherwise tm_mon and tm_mday
     * are used.
     */
    static long long fromTm(bool utc,const struct tm* tmp, int usecs = 0);

    /**
     * Format this UTime relative to UTC, or based on the TZ environment variable.
     */
    bool isUTC() const { return _utc; }

    /**
     * Format this UTime relative to UTC, or the local timezone?
     */
    void setUTC(bool val) { _utc = val; }

    /**
     * Parse a character string into a UTime, using these formats until success:
     *
     * [CC]YY [cmon|mon] day h:m[:s.f]      h,m and s are one or two digits
     * [CC]YY [cmon|mon] day hhmmss[.f]     hh, mm and ss are two digits
     * [CC]YY [cmon|mon] day
     * s.f
     * YYYY-mm-dd[THH:MM[:SS[.f]]]          ISO format
     * YYYY-mm-dd[ HH:MM[:SS[.f]]]          ISO format with space separator
     *
     * "cmon" is a character month or abbreviation.
     * "mon" is a numeric month (1-12).
     * "day" is day of month, 1-31.
     * "h" or "hh" are in the range 0-23.
     * "f" is the fractional seconds, one or more digits.
     * The last format, "s.f" is the number of non-leap seconds since
     * 1970 Jan 1 00:00 GMT. For example, 1262304000.0 is 2010 Jan 1 00:00 GMT.
     * Note: one can also use a "%s" descriptor in the format argument to
     * parse(false,str,format,nparsed) to do the same conversion.
     * If all parsing fails, throw ParseException.
     * @param nparsed: number of characters parsed.
     *
     * @throws ParseException
     */
    static UTime parse(bool utc, const std::string& string, int* nparsed=0);

    /**
     * Parse a character string into a UTime.
     * @param format: a time format in the form of strptime. All the % format
     * descriptors of strptime are available. In addition one can
     * use "%nf" to parse fractional seconds, where n is the number of
     * digits in the fraction to parse. n defaults to 3 if not specified.
     * If the "%s" descriptor is used, then the utc parameter is silently forced
     * to false, since strptime does that conversion in local time.
     * @param nparsed: number of characters parsed.
     * Example:
     * UTime ut = UTime::parse(true,timestr,"%Y %m %d %H:%M:%S.%2f");
     *
     * @throws ParseException
     */
    static UTime parse(bool utc, const std::string& string,
                       const std::string& format, int* nparsed=0);

    /**
     * Updates the value of a UTime by doing a parse(utc,string,nparsed).
     *
     * @throws ParseException
     */
    void set(bool utc,const std::string& string,int* nparsed=0);

    /**
     * Updates the value of a UTime by doing a parse(utc,string,format,nparsed).
     *
     * @throws ParseException
     */
    void set(bool utc,const std::string& string, const std::string& format,
             int* nparsed=0);

    /**
     * Format a UTime into a string.
     * @param utc: if true, use UTC timezone, otherwise the TZ environment variable.
     * @param fmt: a time format in the form of strftime. All the % format
     * descriptors of strftime are available. In addition one can
     * use "%nf" to print fractional seconds, where n is the precision,
     * a digit from 1 to 9. n defaults to 3, providing millisecond precision,
     * if not specified.  For example:
     * ut.format(true,"time is: %Y %m %d %H:%M:%S.%2f");
     *
     * The "%s" format descriptor will print the number of non-leap seconds
     * since 1970 Jan 01 00:00 UTC. This is the same number returned by toSecs().
     * Note that %s will generate the same value as strftime in the following code:
     * struct tm tm;
     * char timestr[12];
     * time_t tval = mytime;
     * localtime_r(&tval,&tm);
     * strftime(timestr,sizeof(timestr),"%s",&tm);
     *
     * Using gmtime_r to fill in struct tm and then call strftime with a %s
     * generates the wrong value if the local timezone is other than GMT,
     * since strftime with a %s assumes the struct tm is in the local timezone:
     * gmtime_r(&tval,&tm);
     * strftime(timestr,sizeof(timestr),"%s",&tm);  // wrong
     */
    std::string format(bool utc,const std::string& fmt) const;

    /**
     * Format a UTime into a string.
     * isUTC() attribute detemines whether it is formatted in UTC or based on the TZ environment variable.
     * @param fmt: as in format(utc,fmt).
     */
    std::string format(const std::string& fmt) const;

    /**
     * Format a UTime into a string using the format returned by getFormat().
     * @param utc: if true, use UTC timezone, otherwise the TZ environment variable.
     */
    std::string format(bool utc) const;

    /**
     * Format a UTime into a string using the format returned by getFormat().
     * isUTC() attribute detemines whether it is formatted in UTC or based on the TZ environment variable.
     *
     */
    std::string format() const;

    UTime& operator=(const UTime& u)
    {
        if (this != &u) {
            _utime = u._utime;
            _fmt = u._fmt;
        }
        return *this;
    }

    UTime& operator=(long long u) { _utime = u; return *this; }

    UTime operator+ (long long u) const { return UTime(_utime + u); }

    UTime operator- (long long u) const { return UTime(_utime - u); }

    long long operator- (const UTime& u) const { return _utime - u._utime; }

    UTime& operator+=(long long u) { _utime += u; return *this; }

    UTime& operator-=(long long u) { _utime -= u; return *this; }

    bool operator<(const UTime& u) const { return _utime < u._utime; }

    bool operator<=(const UTime& u) const { return _utime <= u._utime; }

    bool operator>(const UTime& u) const { return _utime > u._utime; }

    bool operator>=(const UTime& u) const { return _utime >= u._utime; }

    bool operator==(const UTime& u) const { return _utime == u._utime; }

    bool operator!=(const UTime& u) const { return _utime != u._utime; }

    /**
     * Return the UTime on the even microseconds interval @p y at or just
     * prior to this UTime.  If @p y is zero, return this UTime.
     * 
     * @param y 
     * @return UTime 
     */
    UTime earlier(long long y) const;

    /**
     * Like earlier(), but if this UTime is closer to one interval past
     * earlier(), then return that UTime instead of the earlier one.  If @p y
     * is zero, return this UTime.
     */
    UTime round(long long y) const;

    static int month(std::string monstr);

    long long toUsecs() const
    {
        return _utime;
    } 

    double toDoubleSecs() const
    {
        // should work for positive and negative.
        return (time_t)(_utime/USECS_PER_SEC) +
                (double)(_utime % USECS_PER_SEC) / USECS_PER_SEC;
    } 

    time_t toSecs() const
    {
        return (_utime + USECS_PER_SEC / 2) / USECS_PER_SEC;
    } 

    /**
     * Set the format used when converting this UTime to a string
     * with format(utc), or format(), or on a ostream.
     */
    UTime& setFormat(const std::string& val)
    {
        _fmt = val;
        return *this;
    }

    /**
     * Get the format used when converting this UTime to a string 
     * with format(utc), or format(), or on a ostream.
     * If the user hasn't set the format, the default value is getDefaultFormat().
     */
    const std::string& getFormat() const
    {
        if (_fmt.length() > 0) return _fmt;
        return getDefaultFormat();
    }

    /**
     * Static method to set the default output format.
     * If not set by the user, the default default (sic) is "%c".
     */
    static void setDefaultFormat(const std::string& val);

    static const std::string& getDefaultFormat();

    /**
     * Set the TZ environment variable to val. If val is an empty string
     * the TZ is removed from the environment.
     */
    static void setTZ(const std::string& val);

    static std::string getTZ();

    struct tm tm(bool utc) const;

    template<typename charT> friend
    std::basic_ostream<charT, std::char_traits<charT> >& operator << 
        (std::basic_ostream<charT, std::char_traits<charT>  >& os,
            const UTime& x);

    /**
     * Positive modulus:  if x > 0, returns x % y
     *                    else  y + (x % y)
     * Useful for time calculation on negative times, for
     * example:
     *	pmod((long long)ut,USECS_PER_DAY)
     * gives microseconds since 00:00 of day whether 
     * date is negative or positive.
     */
    static long long pmod(long long x, long long y);

protected:

    /**
     * Convert a unsigned value in seconds to a value in the units of UTime.
     */
    static long long fromSecs(time_t x)
    {
        return (long long)x * USECS_PER_SEC;
    } 

    /**
     * Convert a double value in seconds to a value in the units of UTime.
     */
    static long long fromSecs(double x)
    {
        double xf = floor(x);
        return (long long)xf * USECS_PER_SEC +
                (int)rint((x-xf) * USECS_PER_SEC) ;
    } 

    static double toDoubleSecs(long long x)
    {
        // should work for positive and negative.
        return (time_t)(x/USECS_PER_SEC) + (double)(x % USECS_PER_SEC) / USECS_PER_SEC;
    } 

    static time_t toSecs(long long x)
    {
        return x / USECS_PER_SEC;
    } 

private:

    /**
     * Parse into this UTime same as parse(), returning true on success.
     * If parsing fails and throwx is true, then throw an exception.
     * If parsing fails and throwx is false, then return false.
     **/
    bool
    checkParse(bool utc, const std::string& str, const std::string& fmt,
               int *ncharp, bool throwx=false);

    /**
     * non-leap micro-seconds since 1970 Jan 1 00:00 UTC.
     */
    long long _utime;

    /**
     * strftime string to use when formatting this UTime.
     */
    std::string _fmt;

    /**
     * Whether to format this UTime relative to UTC. Can be overridden with
     * format calls that provide a utc argument.
     */
    bool _utc;

    static std::string _defaultFormat;

    static Mutex _fmtMutex;

};

/**
 * class for changing output format of UTime on ostream, in a way
 * like the standard stream manipulator classes.
 * This supports doing:
 * using namespace nidas::util;
 *	cout << setTZ<char>("PST8PDT") << setDefaultFormat("%H%M%S") << ut << endl;
 *  or (if we use the template class):
 *	cout << setTZ<char>("PST8PDT") << setDefaultFormat<char>("%H%M%S") << ut << endl;
 */
template<typename charT>
class UTime_stream_manip {

    std::string _fmt;
    /** 
     * Pointer to function that does a manipulation on a ostream
     * with a string argument.
     */
    std::basic_ostream<charT,std::char_traits<charT> >& (*_f)(std::basic_ostream<charT,std::char_traits<charT> >&, const std::string&);

public:
    /**
     * Constructor of manipulator.
     * @param f A function that returns a reference to an ostream,
     *          with arguments of the ostream reference and a string.
     */
    UTime_stream_manip(std::basic_ostream<charT,std::char_traits<charT> >&  (*f)(
        std::basic_ostream<charT,std::char_traits<charT> >&, const std::string&), const std::string& fmt): _fmt(fmt),_f(f)
    {
    }

    /**
     * << operator of this manipulator on an ostream.
     * Invokes the function that was passed to the constructor.
     */
    template<typename charTx>
    friend std::basic_ostream<charTx, std::char_traits<charTx> >& operator << 
        (std::basic_ostream<charTx, std::char_traits<charTx> >& os,const UTime_stream_manip<charTx>& m);
};

// format a UTime on an output stream.
template<typename charT>
std::basic_ostream<charT, std::char_traits<charT> >& operator << 
    (std::basic_ostream<charT, std::char_traits<charT> >& os,const UTime& x)
{
    return os << x.format();
}
template<typename charT>
std::basic_ostream<charT, std::char_traits<charT> >& operator <<
    (std::basic_ostream<charT, std::char_traits<charT> >& os,const UTime_stream_manip<charT>& m)
{
    return m._f(os,m._fmt);
}

// anonymous namespace for private, internal functions
namespace {
/**
 * Internal function to set the default UTime output format on an ostream.
 * Typically this is invoked by an UTime_stream_manip << operator.
 */
template<typename charT>
std::basic_ostream<charT, std::char_traits<charT> >& 
    setOstreamDefaultFormat(std::basic_ostream<charT, std::char_traits<charT> >& os,
        const std::string& val)
{
    UTime::setDefaultFormat(val);
    return os;
}

/**
 * Internal function to set the UTime timezone on an ostream.
 * Passed to the manipulator constructor.
 */
template<typename charT>
std::basic_ostream<charT, std::char_traits<charT> >& 
    setOstreamTZ(std::basic_ostream<charT, std::char_traits<charT> >& os,
        const std::string& val)
{
    UTime::setTZ(val);
    return os;
}
}   // end of anonymous namespace

/**
 * Function to set the default UTime output format on an ostream.
 */
template<typename charT>
UTime_stream_manip<charT> setDefaultFormat(const std::string& val)
{
    return UTime_stream_manip<charT>(&setOstreamDefaultFormat,val);
}

/**
 * Function to set the UTime timezone on an ostream.
 */
template<typename charT>
UTime_stream_manip<charT> setTZ(const std::string& val)
{
    return UTime_stream_manip<charT>(&setOstreamTZ,val);
}

/**
 * Return the current unix system time, in microseconds 
 * since Jan 1, 1970, 00:00 GMT
 */
inline long long getSystemTime() {
    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME,&ts);
    return (long long)ts.tv_sec * USECS_PER_SEC + ts.tv_nsec / NSECS_PER_USEC;
}

/**
 * Return smallest time that is an integral multiple of
 * delta, that isn't less than or equal to argument t.
 * Similar to to ceil() math function, except ceil() finds value
 * that isn't less than argument, not less-than-or-equal, i.e.
 * this function always returns a value greater than the arg.
 */
inline long long timeCeiling(long long t,long long delta) {
    return ((t / delta) + 1) * delta;
}

/**
 * Return largest time that is an integral multiple of
 * delta, that isn't greater than argument t.  Analogous to floor()
 * math function.
 */
inline long long timeFloor(long long t,long long delta) {
    return (t / delta) * delta;
}
/**
 * Utility function, sleeps until the next even period + offset.
 * Returns true if interrupted.
 *
 * @throws IOException
 */
extern bool sleepUntil(unsigned int periodMsec,unsigned int offsetMsec=0);

}}	// namespace nidas namespace util

#endif
