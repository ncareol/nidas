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

#include "UTime.h"
#include "Process.h"
#include "auto_ptr.h"
#include "Logger.h"

#include <sys/time.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iomanip>

using namespace std;

namespace nidas {
namespace util {


/* static */
Mutex UTime::_fmtMutex;
/* static */
string UTime::_defaultFormat("%c");

UTime::UTime():_utime(0),_fmt(),_utc(true)
{
    struct timespec ts;
    ::clock_gettime(CLOCK_REALTIME,&ts);
    _utime = (long long)ts.tv_sec * USECS_PER_SEC + ts.tv_nsec / NSECS_PER_USEC;
}

//
// If utc is false, then these fields will be interpreted in the
// local time zone, otherwise UTC
//
UTime::UTime(bool utc, int year, int mon, int day, int hour, int minute,
             double dsec):
    _utime(0),_fmt(),_utc(utc)
{
    if (year > 1900) year -= 1900;	// convert to years since 1900
    else if (year < 50) year += 100;

    struct tm tms = ::tm();
    tms.tm_sec = (int)dsec;
    dsec -= tms.tm_sec;
    tms.tm_min = minute;
    tms.tm_hour = hour;
    tms.tm_mday = day;
    tms.tm_mon = mon - 1;	// convert to 0:11
    tms.tm_year = year;
    tms.tm_yday = -1;
    /* from mktime man page:
     * tm_isdst
     * A flag that indicates whether daylight saving time is in  effect
     * at the time described.  The value is positive if daylight saving
     * time is in effect, zero if it is not, and negative if the
     * information is not available.
     */
    tms.tm_isdst = (utc ? 0 : -1);

    _utime = fromTm(utc,&tms) + fromSecs(dsec);
}

UTime::UTime(bool utc, int year, int yday, int hour, int minute, double dsec):
    _utime(0),_fmt(),_utc(utc)
{

    if (year > 1900) year -= 1900;	// convert to years since 1900
    else if (year < 50) year += 100;	

    struct tm tms = ::tm();
    tms.tm_sec = (int)dsec;
    dsec -= tms.tm_sec;

    tms.tm_min = minute;
    tms.tm_hour = hour;
    tms.tm_mday = 0;
    tms.tm_mon = -1;
    tms.tm_year = year;
    tms.tm_yday = yday - 1;
    tms.tm_isdst = (utc ? 0 : -1);
    _utime = fromTm(utc,&tms) + fromSecs(dsec);
}

UTime::UTime(bool utc, const struct tm* tmp,int usecs):
    _utime(fromTm(utc,tmp,usecs)),
    _fmt(),
    _utc(utc)
{
}

const UTime UTime::MIN(LONG_LONG_MIN);
const UTime UTime::MAX(LONG_LONG_MAX);
const UTime UTime::ZERO(0ll);

bool UTime::isZero() const
{
    return _utime == 0ll;
}

bool UTime::isMin() const
{
    return _utime == LONG_LONG_MIN;
}

bool UTime::isMax() const
{
    return _utime == LONG_LONG_MAX;
}

bool UTime::isSet() const
{
    return _utime != LONG_LONG_MIN && _utime != LONG_LONG_MAX;
}

/* static */
long long UTime::fromTm(bool utc,const struct tm* tmp, int usecs)
{
    time_t ut;
    struct tm tms = *tmp;

    int yday = -1;

    if (tms.tm_yday >= 0 && (tms.tm_mon < 0 || tms.tm_mday < 1)) {
        yday = tms.tm_yday;
        tms.tm_yday = -1;	// ::mktime ignores yday
        tms.tm_mon = 0;
        tms.tm_mday = 1;
    }

    if (utc) tms.tm_isdst = 0;
    ut = ::mktime(&tms);
    // if (ut == (time_t)-1) return ParseException();

#ifdef DEBUG
    cerr << "yr=" << tms.tm_year <<
            " mn=" << tms.tm_mon <<
            " dy=" << tms.tm_mday <<
            " ydy=" << tms.tm_yday <<
            " hr=" << tms.tm_hour <<
            " mn=" << tms.tm_min <<
            " sc=" << tms.tm_sec <<
            " isdst=" << tms.tm_isdst <<
            " utc=" << utc <<
            " timezone=" << timezone <<
            " ut=" << ut << endl;
#endif

    // utc means input time is to be interpreted as UTC, even
    // though the TZ environment var may not be GMT/UTC.
    if (utc) ut -= timezone;

    if (yday >= 0) {
        // this is close, but off by an hour if there is DST
        // change between Jan 1 and the time of interest.
        ut += yday * 86400;
        if (!utc) {			// correct for DST switch
            int d1dst = tms.tm_isdst;
            struct tm tm2;
            localtime_r(&ut,&tm2);
            int yddst = tm2.tm_isdst;
            if (d1dst == 0 && yddst > 0) ut -= 3600;
            else if (d1dst > 0 && yddst == 0) ut += 3600;
        }
    }
    return (long long) ut * USECS_PER_SEC + usecs;
}

struct tm* UTime::toTm(bool utc,struct tm* tmp,int *usecs) const
{

    long long ute = earlier(USECS_PER_SEC).toUsecs();
    time_t ut = ute  / USECS_PER_SEC;
    if (usecs) *usecs = _utime - ute;
    if (utc) return gmtime_r(&ut,tmp);
    else return localtime_r(&ut,tmp);
}

struct tm* UTime::toTm(struct tm* tmp,int *usecs) const
{

    return toTm(_utc,tmp,usecs);
}

/* static */
UTime UTime::parse(bool utc, const std::string& str, int *ncharp)
{
    char cmon[32];
    int year,mon,day,hour,min;
    double dsec = 0.;
    int nchar = 0;
    UTime ut(0L);
    bool done = false;

    year = 70;
    mon = day = 1;
    hour = min = 0;

    if (str.length() == 0 || str == "now")
    {
        return UTime();
    }

    // We have to make sure we check from most specific to least specific.
    // scanf("%d %d %d") will parse "YYYY-mm-dd", and the month and days
    // will be negative.
    static const char* formats[] =
        { "%Y-%m-%dT%H:%M:%S.%f",
          "%Y-%m-%d %H:%M:%S.%f",
          "%Y-%m-%dT%H:%M:%S",
          "%Y-%m-%d %H:%M:%S",
          "%Y-%m-%dT%H:%M",
          "%Y-%m-%d %H:%M",
          "%Y-%m-%d", 0 };

    for (const char** fi = formats; *fi; ++fi)
    {
        if (ut.checkParse(utc, str, *fi, ncharp))
        {
            return ut;
        }
    }

    // 97 Feb 1 11:22:33.4
    if (!done && sscanf(str.c_str(),
                        "%d %31[A-Za-z] %d %d:%d:%lf%n",
                        &year,cmon,&day,&hour,&min,&dsec,&nchar) >= 6)
    {
        mon = month(cmon);
        done = true;
    }

    // 97 Feb 1 11:22
    if (!done && sscanf(str.c_str(),
                        "%d %31[A-Za-z] %d %d:%d%n",
                        &year,cmon,&day,&hour,&min,&nchar) >= 5)
    {
        mon = month(cmon);
        done = true;
    }

    // 97 Feb 1 112233.4
    if (!done && sscanf(str.c_str(),
                        "%d %31[A-Za-z] %d %lf%n",
                        &year,cmon,&day,&dsec,&nchar) >= 4)
    {
        mon = month(cmon);
        hour = (int) dsec / 10000;
        dsec -= hour * 10000;
        min = (int) dsec / 100;
        dsec -= min * 100;
        done = true;
    }

    // 97 Feb 1
    if (!done && sscanf(str.c_str(),
                        "%d %31[A-Za-z] %d%n",&year,cmon,&day,&nchar) >= 3)
    {
        mon = month(cmon);
        done = true;
    }

    // 97 2 1 11:22:33.4
    if (!done && sscanf(str.c_str(),"%d %d %d %d:%d:%lf%n",
                        &year,&mon,&day,&hour,&min,&dsec,&nchar) >= 6)
    {
        done = true;
    }

    // 97 2 1 11:22
    if (!done && sscanf(str.c_str(),"%d %d %d %d:%d%n",
                        &year,&mon,&day,&hour,&min,&nchar) >= 5)
    {
        done = true;
    }

    // 97 2 1 112233.4
    if (!done && sscanf(str.c_str(),
                        "%d %d %d %lf%n",&year,&mon,&day,&dsec,&nchar) >= 4)
    {
        hour = (int) dsec / 10000;
        dsec -= hour * 10000;
        min = (int) dsec / 100;
        dsec -= min * 100;
        done = true;
    }

    // 97 2 1
    if (!done && sscanf(str.c_str(),
                        "%d %d %d%n",&year,&mon,&day,&nchar) >= 3)
    {
        done = true;
    }

    // seconds since 1970
    if (!done && sscanf(str.c_str(),"%lf%n",&dsec,&nchar))
    {
        return UTime((long long)trunc(dsec) * USECS_PER_SEC +
                     (long long)rint(fmod(dsec,1.0) * USECS_PER_SEC));
    }

    if (!done)
    {
        throw ParseException(str,"year month day hour:min:sec");
    }

    if (ncharp) *ncharp = nchar;

    return UTime(utc,year,mon,day,hour,min,dsec);
}

void UTime::set(bool utc, const std::string& str,int* nparsed)
{
    *this = UTime::parse(utc,str,nparsed);
}

void UTime::set(bool utc, const std::string& str,
                const std::string& format, int* nparsed)
{
    *this = UTime::parse(utc,str,format,nparsed);
}

/* static */
UTime UTime::parse(bool utc, const std::string& str, const std::string& fmt,
                   int *ncharp)
{
    // Try parsing, and throw an exception if it fails.
    UTime ut(0L);
    ut.checkParse(utc, str, fmt, ncharp, true);
    return ut;
}


bool
UTime::
checkParse(bool utc, const std::string& str, const std::string& fmt,
           int *ncharp, bool throwx)
{
    struct tm tms = ::tm();
    tms.tm_isdst = (utc ? 0 : -1);

    const char* cstr = str.c_str();
    const char* cptr = cstr;
    const char* cp2;
    //
    // Add support for %f format to parse fractional seconds,
    // Note that %F is a GNU extension, so we'll use %f.
    //
    string::size_type i0,i1;
    string::size_type flen = fmt.length();

    string newfmt;
    int usecs = 0;
    int ncharParsed = 0;

    for (i0 = 0;  (i1 = fmt.find('%',i0)) != string::npos; i0 = i1 ) {

        newfmt.append(fmt.substr(i0,i1-i0));	// append up to %

        // cerr << "i0=" << i0 << " i1=" << i1 << endl;

        i1++;	// points to one past the % sign
        if (flen > i1) {
            string sfmt;
            if (fmt[i1] == 'f') {
                sfmt = "%3d%n";
                i1++;
            }
            else if (flen > i1 + 1 && ::isdigit(fmt[i1]) && fmt[i1+1] == 'f') {
                sfmt = string("%") + fmt[i1] + "d%n";
                i1 += 2;
            }
            else {
                // with %s, strptime fills out the struct tm in local time
                if (fmt[i1] == 's') utc = false;
                newfmt.push_back('%');
                continue;
            }
            cp2 = strptime(cptr,newfmt.c_str(),&tms);
            if (!cp2) {
                VLOG(("failed to parse %s", cptr) << " with format " << newfmt);
                if (throwx)
                    throw ParseException(str, fmt);
                return false;
            }
            VLOG(("parsed %s", cptr) << " with format " << newfmt);
            ncharParsed += cp2 - cptr;
            cptr = cp2;
            int nchar = 0;
            if (sscanf(cptr,sfmt.c_str(),&usecs,&nchar) < 1)
            {
                VLOG(("failed to parse %s", cptr) << " with format " << sfmt);
                if (throwx)
                    throw ParseException(str, fmt);
                return false;
            }
            VLOG(("parsed %s", cptr) << " with format " << sfmt
                 << ", got " << usecs);
            for (int i = nchar; i < 6; i++) usecs *= 10;
            cptr += nchar;
            ncharParsed += nchar;
            newfmt.clear();
        }
    }
    if (i0 < flen) newfmt.append(fmt.substr(i0));
    // cerr << "fmt=" << fmt << " newfmt=" << newfmt << endl;
    if (newfmt.length() > 0) {
        cp2 = strptime(cptr,newfmt.c_str(),&tms);
        if (!cp2)
        {
            if (throwx)
                throw ParseException(str,fmt);
            return false;
        }
        ncharParsed += cp2 - cptr;
    }

    // cerr << "tms.tm_mday=" << tms.tm_mday << " usecs=" << usecs << endl;

    if (ncharp) *ncharp = ncharParsed;

    *this = UTime(utc,&tms) + (long long)usecs;
    return true;
}


string UTime::format(bool utc) const
{
    return format(utc,getFormat());
}

string UTime::format() const
{
    return format(_utc,getFormat());
}

// method for conversion to string.
std::string UTime::format(bool utc, const std::string& fmt) const
{
    //
    // Add support for %nf format to print fractional seconds,
    // where n is number of digits after decimal point, 1-6
    // Note that %F is a GNU extension, so we'll use %f.
    //
    string newfmt; 
    string::size_type i0,i1;
    string::size_type flen = fmt.length();

    long long ute = earlier(USECS_PER_SEC).toUsecs();
    time_t ut = ute / USECS_PER_SEC;

    for (i0 = 0;  (i1 = fmt.find('%',i0)) != string::npos; i0 = i1 ) {

        // cerr << "i0=" << i0 << " i1=" << i1 << endl;
        newfmt.append(fmt.substr(i0,i1-i0));	// append up to %

        i1++;	// points to one past the % sign
        int n = -1;
        if (flen > i1) {
            if (fmt[i1] == 'f') {
                i1++;
                n = 3;
            }
            else if (flen > i1 + 1 && ::isdigit(fmt[i1]) && fmt[i1+1] == 'f') {
                n = fmt[i1] - '0';
                i1 += 2;
            }
            else if (fmt[i1] == 's') {
                // %s format descriptor requires some special handling.
                // strftime always assumes that the input struct tm is time in
                // the local time zone.  struct tm contains no information
                // on the timezone of the data fields.  Therefore if one fills
                // in a struct tm from gmtime_r, and then does strftime with a
                // %s, the number of seconds will be wrong by the time zone offset.
                // So, catch it here and use localtime_r to fill in struct tm
                // and call strftime with %s by itself.  One could set the
                // local timezone to GMT, but that would effect other threads.
                i1++;
                struct tm tms;
                localtime_r(&ut,&tms);
#ifdef USE_STRFTIME
                char out[12];
                strftime(out,sizeof(out),"%s",&tms);
                newfmt.append(out);
#else
                ostringstream ostr;
                ostr.str("");
                const time_put<char> &timeputter(use_facet<time_put<char> >(locale()));
                timeputter.put(ostr.rdbuf(),ostr,' ',&tms,fmt.data()+i1-2,fmt.data()+i1);
                newfmt.append(ostr.str());
#endif
                continue;
            }
        }
        // cerr << "i0=" << i0 << " i1=" << i1 << " n=" << n << endl;
        if (n < 0) {		// not a %f or %nf, append %
            newfmt.push_back('%');
            continue;
        }
            
        int div = USECS_PER_SEC;
        int mult = 1;
        for (int j=0; j < n; j++)
            if (div > 1) div /= 10;
            else mult *= 10;

        /*
         * Round printed value.
         */
        int modusecs = (_utime - ute) + div / 2;
        // cerr << "modusecs=" << modusecs <<
        // 	" div=" << div << " mult=" << mult << endl;
        if (modusecs > USECS_PER_SEC) {
            ut++;		// round up
            modusecs = 0;
        }
        modusecs = modusecs / div * mult;
        ostringstream ost;
        ost << setw(n) << setfill('0') << modusecs;
        // cerr << "n=" << n << " modusecs=" << modusecs << " ost=" << ost.str() << endl;
        newfmt.append(ost.str());
    }
    if (i0 < flen) newfmt.append(fmt.substr(i0));

    struct tm tms;

    if (utc) gmtime_r(&ut,&tms);
    else localtime_r(&ut,&tms);

#ifdef USE_STRFTIME
    char out[512];
    strftime(out,sizeof(out),newfmt.c_str(),&tms);
    return out;
#else
    // use std::time_put to format time
    ostringstream ostr;
    ostr.str("");

    const time_put<char> &timeputter(use_facet<time_put<char> >(locale()));
    timeputter.put(ostr.rdbuf(),ostr,' ',&tms,
        newfmt.data(),newfmt.data()+newfmt.length());

    return ostr.str();
#endif
}

string UTime::format(const std::string& fmt) const
{
    return format(_utc,fmt);
}

/* static */
void UTime::setDefaultFormat(const string& val)
{
    Synchronized autolock(_fmtMutex);
    _defaultFormat = val;
}

const string& UTime::getDefaultFormat()
{
    Synchronized autolock(_fmtMutex);
    return _defaultFormat;
}

/* static */
void UTime::setTZ(const string& val)
{
    string curval;
    Process::getEnvVar("TZ",curval);
    if (curval != val) {
        Process::setEnvVar("TZ",val);
        ::tzset();
    }
}

string UTime::getTZ() 
{
    string curval;
    Process::getEnvVar("TZ",curval);
    return curval;
}

/*
 * Return month number in the range 1-12, corresponding to a string.
 */
int UTime::month(string monstr)
{
    if (monstr.length() > 0 && std::islower(monstr[0]))
        monstr[0] = std::toupper(monstr[0]);

    istringstream iss(monstr);

    // make this a static member?  Need to mutex it? Probably.
    const time_get<char>& tim_get = use_facet<time_get<char> >(iss.getloc());

    iss.imbue(iss.getloc());
    istreambuf_iterator<char> is_it01(iss);
    istreambuf_iterator<char> end;
    struct tm tms;
    ios_base::iostate errorstate = ios_base::goodbit;

    tim_get.get_monthname(is_it01, end, iss, errorstate, &tms);
    if (errorstate & ios_base::failbit) {
        cerr << "error in parsing " << monstr << endl;
        return 0;
    }
    return tms.tm_mon + 1;
}

UTime UTime::earlier(long long y) const
{
    if (y == 0)
        return *this;
    long long ymod = _utime % y;
    if (ymod >= 0) return UTime(_utime - ymod);
    return UTime(_utime - (y + ymod));
}

UTime UTime::round(long long y) const
{
    if (y == 0)
        return *this;
    long long ymod = _utime % y;
    if (ymod < 0)
        ymod += y;
    if (ymod >= y / 2)
        ymod = ymod - y;
    return UTime(_utime - ymod);
}

/* static */
long long UTime::pmod(long long x, long long y)
{
    if (x < 0) return y + (x % y);
    else return x % y;
}

bool sleepUntil(unsigned int periodMsec, unsigned int offsetMsec)
{
    struct timespec sleepTime;
    /*
     * sleep until an even number of periodMsec since 
     * creation of the universe (Jan 1, 1970 0Z).
     */
    long long tnow = getSystemTime();
    unsigned int mSecVal =
      periodMsec - (unsigned int)((tnow / USECS_PER_MSEC) % periodMsec) + offsetMsec;

    sleepTime.tv_sec = mSecVal / MSECS_PER_SEC;
    sleepTime.tv_nsec = (mSecVal % MSECS_PER_SEC) * NSECS_PER_MSEC;
    if (::nanosleep(&sleepTime,0) < 0) {
        if (errno == EINTR) return true;
        throw IOException("Looper","nanosleep",errno);
    }
    return false;
}

} // namespace util
} // namespace nidas
