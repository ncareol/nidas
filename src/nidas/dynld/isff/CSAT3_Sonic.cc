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

#include "CSAT3_Sonic.h"

#include <nidas/core/DSMConfig.h>
#include <nidas/core/Variable.h>
#include <nidas/core/PhysConstants.h>
#include <nidas/core/Project.h>
#include <nidas/core/TimetagAdjuster.h>
#include <nidas/util/UTime.h>
#include <nidas/util/Logger.h>
#include <nidas/util/IOTimeoutException.h>

#include <math.h>

#include <string>
#include <sstream>
#include <fstream>

using namespace nidas::dynld::isff;
using namespace std;

namespace n_u = nidas::util;
namespace n_c = nidas::core;

NIDAS_CREATOR_FUNCTION_NS(isff,CSAT3_Sonic)

/**
 * AUTOCONFIG Stuff
 */

const PortConfig CSAT3_Sonic::DEFAULT_PORT_CONFIG(DEFAULT_BAUD_RATE, DEFAULT_DATA_BITS, DEFAULT_PARITY,
												  DEFAULT_STOP_BITS, DEFAULT_PORT_TYPE, DEFAULT_LINE_TERMINATION,
											  DEFAULT_SENSOR_POWER, DEFAULT_RTS485, DEFAULT_CONFIG_APPLIED);
const char* CSAT3_Sonic::DEFAULT_MSG_SEP_CHARS = "\x55\xaa";
const int CSAT3_Sonic::SENSOR_BAUDS[CSAT3_Sonic::NUM_SENSOR_BAUDS] = {9600, 19200};
const WordSpec CSAT3_Sonic::SENSOR_WORD_SPECS[CSAT3_Sonic::NUM_WORD_SPECS] =
{
	{8,Termios::NONE,1},
};

const PORT_TYPES CSAT3_Sonic::SENSOR_PORT_TYPES[NUM_PORT_TYPES] = {RS232};

/*
 * CSAT3 stuff
 */
const float CSAT3_Sonic::GAMMA_R = 402.684;

CSAT3_Sonic::CSAT3_Sonic():
    Wind3D(DEFAULT_PORT_CONFIG),
    _windInLen(12),	// two bytes each for u,v,w,tc,diag, and 0x55aa
    _totalInLen(12),
    _windNumOut(0),
    _spikeIndex(-1),
    _windSampleId(0),
    _extraSampleTags(),
    _nttsave(-2),
    _counter(-1),
#if __BYTE_ORDER == __BIG_ENDIAN
    _swapBuf(),
#endif
    _rate(0),
    _oversample(false),
    _sonicLogFile(),
    _gapDtUsecs(0),
    _ttlast(0),
    _nanIfDiag(true),
    _consecutiveOpenFailures(0),
    _checkConfiguration(true),
    _checkCounter(true),
    _ttadjuster(0),
    defaultMessageConfig(DEFAULT_MESSAGE_LENGTH, DEFAULT_MSG_SEP_CHARS, DEFAULT_MSG_SEP_EOM),
    rateCmd(0),
    acqrate(0),
    serialNumber(""),
    osc(0),
    revision(""),
    rtsIndep(-1),
    recSep(-1),
    baudRate(-1)
{
    // We set the defaults at construction,
    // letting the base class modify according to fromDOMElement()
    setMessageParameters(defaultMessageConfig);

	// copy the serial comm parameters to the SerialSensor lists
    // Let the base class know about PTB210 RS232 limitations
    for (int i=0; i<NUM_PORT_TYPES; ++i) {
    	_portTypeList.push_back(SENSOR_PORT_TYPES[i]);
    }

    for (int i=0; i<NUM_SENSOR_BAUDS; ++i) {
    	_baudRateList.push_back(SENSOR_BAUDS[i]);
    }

    for (int i=0; i<NUM_WORD_SPECS; ++i) {
    	_serialWordSpecList.push_back(SENSOR_WORD_SPECS[i]);
    }

}

CSAT3_Sonic::~CSAT3_Sonic()
{
    delete _ttadjuster;
}


void CSAT3_Sonic::exitConfigMode() throw(n_u::IOException)
{
    clearBuffer();

    DLOG(("%s:%s sending D (nocr)",getName().c_str(), getClassName().c_str()));
    write("D",1);
}

/* static */
string CSAT3_Sonic::parseSerialNumber(const string& str,
        string::size_type & idx)
{
    // Version 3 and 4 serial numbers: "SNXXXX", version 5: "SnXXXX".
    // Serial number of a special test version 4 sonic was "PR0001"
    const char* sn[] = {
        "SN", "Sn", "PR"
    };

    idx = string::npos;
    for (unsigned int i = 0; idx == string::npos &&
            i < sizeof(sn)/sizeof(sn[0]); i++)
        idx = str.find(sn[i]);

    string empty;
    if (idx == string::npos) return empty;

    string::size_type bl = str.find(' ',idx);
    if (bl != string::npos) return str.substr(idx,bl-idx);
    return empty;
}

string CSAT3_Sonic::querySonic(int &acqrate,char &osc, string& serialNumber,
        string& revision, int& rtsIndep, int& recSep, int& baudRate)
throw(n_u::IOException)
{
    std::string result = "";
    acqrate = 0;
    osc = ' ';
    serialNumber = "";
    revision = "unknown";
    rtsIndep = -1;  // ri setting, RTS independent, -1=unknown
    recSep = -1;  // rs setting, record separator, -1=unknown

    /*
     * Make this robust around the following situations:
     * 1. No sonic connected to the port. This will result in a timeout and a break
     *    out of the inner loop. Will try up to the number of times in the
     *    outer loop, and return an empty result, and the returned parameters will
     *    have the above initial values.
     *    If a timeout is set for this sensor in the config, the open() will be
     *    retried again and things should succeed once a sonic, with power is connected.
     * 2. Sonic is connected but it isn't responding to commands, just spewing binary
     *    wind data.  Since we're trying to split into records by a ">" terminator, we may
     *    not not get anything in result, or it would be non-ASCII jibberish.
     *    The returned parameters will have the above initial values since the
     *    keywords won't be found.  An attempt may be made to set the rate, which
     *    will also not succeed, the open() will return anyway and the good data
     *    will be read as usual but it may have the wrong rate.
     * 3. sonic isn't responding to commands and is sending jibberish data, i.e.
     *    a wrong baud rate or bad cable connection. result will be as in
     *    2 above, but the data read will be junk. User is expected to notice
     *    the bad data and resolve the issue. Software can't do anything about it
     *    (could try other baud rates, but that ain't worth doing...).
     * 4. Operational sonic. All should be happy.
     */

    n_u::UTime quit;
    quit += USECS_PER_SEC * 5;
    bool scanned = false;

    for ( ; !scanned && n_u::UTime() < quit; ) {
        DLOG(("%s:%s sending ?? CR",getName().c_str(), getClassName().c_str()));
        write("??\r",3);    // must send CR
        // sonic takes a while to respond to ??
        int timeout = 4 * MSECS_PER_SEC;
        string::size_type stidx = string::npos;

        // read until timeout, or complete message
        for ( ; !scanned && n_u::UTime() < quit; ) {
            try {
                char buf[512];
                memset(buf, 0, 512);
                unsigned int l = readEntireResponse(buf, 511, timeout);
                // strings will not be null terminated
                const char * cp = (const char*)buf;
                // sonic echoes back "T" or "??" command
                if (l != 0)
                    while (*cp == 'T' || *cp == '?' || ::isspace(*cp)) {
                        cp++;
                        l--;
                    }
                string rec(cp,l);
                DLOG(("%s: CSAT3 query: len=",getName().c_str())
                        << rec.length() << ", \"" << rec << '"');

                result += rec;
            }
            catch (const n_u::IOTimeoutException& e) {
                DLOG(("%s: timeout",getName().c_str()));
                break;
            }

            // rev 3 message, starts with ET=, contains serial number
            // rev 4 or 5 message, starts with serial number
            if (stidx == string::npos) {
                string::size_type etidx = result.find("ET=");
                string::size_type snidx;
                serialNumber = parseSerialNumber(result, snidx);
                stidx = std::min(snidx, etidx);

                if (stidx != string::npos) {
                    result = result.substr(stidx);
                    stidx = 0;
                }
            }
            if (stidx != string::npos) {
                string::size_type ri = result.find("\n>");
                if (ri != string::npos) {
                    result.resize(ri);
                    scanned = true;
                }
            }
        }
    }
    clearBuffer();

    if (result.empty()) return result;

    string::size_type rlen = result.length();
    DLOG(("%s: query=",getName().c_str()) << n_u::addBackslashSequences(result) << " result length=" << rlen);

    // find and get AQ parameter, e.g. AQ=1.0 (raw sampling rate)
    string::size_type fs = result.find("AQ=");
    if (fs != string::npos && fs + 3 < rlen) {
        acqrate = atoi(result.substr(fs+3).c_str());
    }
    else {
    	DLOG(("CSAT3_Sonic::querySensor(): Didn't find AQ parameter in query response."));
    	return std::string();
    }

    // get os parameter, e.g. "os=g"
    // g for 10Hz 6x oversampling
    // h for 20Hz 3x oversampling
    // ' ' otherwise
    // For version 4, os=0 means no oversampling
    fs = result.find("os=");
    if (fs != string::npos && fs + 3 < rlen) {
    	osc = result[fs+3];
	}
	else {
		DLOG(("CSAT3_Sonic::querySensor(): Didn't find os parameter in query response."));
		return std::string();
	}

    // get software revision, e.g. "rev 3.0f"
    fs = result.find("rev");
    if (fs != string::npos && fs + 4 < rlen) {
        string::size_type bl = result.find(' ',fs+4);
        revision = result.substr(fs+4,bl-fs-4);
    }
	else {
		DLOG(("CSAT3_Sonic::querySensor(): Didn't find rev parameter in query response."));
		return std::string();
	}

    // get RI=n setting. 0=power RS-232 drivers on RTS, 1=power always
    fs = result.find("RI=");
    if (fs != string::npos && fs + 3 < rlen) {
        rtsIndep = atoi(result.substr(fs+3).c_str());
    }
	else {
		DLOG(("CSAT3_Sonic::querySensor(): Didn't find RI parameter in query response."));
		return std::string();
	}

    // get RS=n setting. 0=no record separator, 1=0x55AA
    fs = result.find("RS=");
    if (fs != string::npos && fs + 3 < rlen) {
        recSep = atoi(result.substr(fs+3).c_str());
    }
	else {
		DLOG(("CSAT3_Sonic::querySensor(): Didn't find RS parameter in query response."));
		return std::string();
	}

    // get BR=n setting. 0=9600, 1=19200
    fs = result.find("BR=");
    if (fs != string::npos && fs + 3 < rlen) {
        baudRate = atoi(result.substr(fs+3).c_str());
    }
	else {
		DLOG(("CSAT3_Sonic::querySensor(): Didn't find BR parameter in query response."));
		return std::string();
	}

    return result;
}

void CSAT3_Sonic::sendBaudCmd(int baud)
{
	std::ostringstream baudCmd;
	baudCmd << "br "<< baud << "\r";
    DLOG(("%s: sending %s", getName().c_str(), baudCmd.str().c_str()));
	(void)write((void*)(baudCmd.str().c_str()), baudCmd.str().length());
	usleep(100 * USECS_PER_MSEC);
}

void CSAT3_Sonic::sendRTSIndepCmd(bool on)
{
	std::ostringstream rtsIndepCmd;
	rtsIndepCmd << "ri " << (on ? '1' : '0') << "\r";
    DLOG(("%s: sending %s", getName().c_str(), rtsIndepCmd.str().c_str()));
    (void)write((void*)(rtsIndepCmd.str().c_str()), rtsIndepCmd.str().length());
    usleep(100 * USECS_PER_MSEC);
}

void CSAT3_Sonic::sendRecSepCmd()
{
    std::string cmd("rs 1\r");
    DLOG(("%s: sending %s", getName().c_str(), cmd.c_str()));
	(void)write(cmd.c_str(), cmd.length());
	usleep(100 * USECS_PER_MSEC);
}

string CSAT3_Sonic::sendRateCommand(const char* cmd)
throw(n_u::IOException)
{
    DLOG(("%s: sending %s",getName().c_str(),cmd));
    write(cmd,2);
    int timeout = MSECS_PER_SEC * 4;

    string result;
    // do up to 10 reads or a timeout.
    for (int i = 0; i < 10; i++) {
        try {
            readBuffer(timeout);
        }
        catch (const n_u::IOTimeoutException& e) {
            DLOG(("%s: timeout",getName().c_str()));
            break;
        }
        for (Sample* samp = nextSample(); samp; samp = nextSample()) {
            // strings will not be null terminated
            const char * cp = (const char*)samp->getConstVoidDataPtr();
            result += string(cp,samp->getDataByteLength());
            distributeRaw(samp);
        }
    }
    clearBuffer();
    while (result.length() > 0 && result[result.length() - 1] == '>') result.resize(result.length()-1);
    return result;
}

const char* CSAT3_Sonic::getRateCommand(int rate,bool oversample) const
{
    struct acqSigTable {
        int rate;
        bool oversample;
        const char* cmd;
    };
    static const struct acqSigTable acqSigCmds[] = {
        {1,false,"A2"},
        {2,false,"A5"},
        {3,false,"A6"},
        {5,false,"A7"},
        {6,false,"A8"},
        {10,false,"A9"},
        {12,false,"Aa"},
        {15,false,"Ab"},
        {20,false,"Ac"},
        {30,false,"Ad"},
        {60,false,"Ae"},
        {10,true,"Ag"},
        {20,true,"Ah"},
    };

    int nr =  (signed)(sizeof(acqSigCmds)/sizeof(acqSigCmds[0]));
    for (int i = 0; i < nr; i++) {
        if (acqSigCmds[i].rate == rate &&
                (oversample == acqSigCmds[i].oversample)) {
            return acqSigCmds[i].cmd;
        }
    }
    return 0;
}

void CSAT3_Sonic::open(int flags) throw(n_u::IOException,n_u::InvalidParameterException)
{
	// Gotta set up the rate command before it's used.
	// Should have everything needed at this point to configure this.
	// But if being instantiated outside the context of a DOM, this may not be the case.
	if (getCatalogName().length() == 0) {
		DLOG(("Not operating in the context of a DOM, so init _rate"));
		_rate = 20;
	}
	DLOG(("%s: _rate=%d",getName().c_str(),_rate));
	if (_rate > 0) {
		rateCmd = const_cast<char*>(getRateCommand(_rate,_oversample));
		if (!rateCmd) {
			ostringstream ost;
			ost << "rate=" << _rate << " Hz not supported with oversample=" << _oversample;
			throw n_u::InvalidParameterException(getName(),
					"sample rate",ost.str());
		}
	}

	// AutoConfig happens here...
	SerialSensor::open(flags);

	if (!_checkConfiguration) return;

//	const int NOPEN_TRY = 5;
//
	/*
	 * Typical session when a sonic port is opened. If the sonic rate is determined
	 * to be correct after the first query, then the "change rate" and "second query"
	 * steps are not done.
	 * --------------------------------------------------
	 * action         user         sonic response
	 * --------------------------------------------------
	 * initial                     spewing binary data
	 * terminal mode  "T"          ">"
	 * query          "??\r"       status message "ET=...." followed by ">"
	 * change rate    "Ah"         "Acq sigs 60->20 Type x to abort......< ...>"
	 * second query   "??\r"       status message "ET=...." followed by ">"
	 * data mode      "D"
	 *
	 * If Campbell changes the format of the status message, or any of the other
	 * responses, then this code may have to be modified.
	 */

	/*
	 * Code originally set the message parameters to those already held by the base class. However,
	 * the AutoConfig code sets those values to that expected when entering terminal mode, which is 0 + '>'.
	 * So use the defaultMessageConfig instead to enter data mode.
	 */
	try {
		setMessageParameters(defaultMessageConfig);
	}
	catch(const n_u::InvalidParameterException& e) {
		throw n_u::IOException(getName(),"open",e.what());
	}
//	/*
//	 * An IOTimeoutException is thrown, which will cause another open attempt to
//	 * be scheduled, in the following circumstances:
//	 *   1. If a serial number is not successfully queried, and no data is arriving.
//	 *      We don't add any extra log messages in this case to avoid filling up the
//	 *      logs when a sonic simply isn't connected. We don't want to give up in
//	 *      this case, and return without an exception, because then a sonic might
//	 *      later be connected and the data read without knowing the serial number,
//	 *      which is only determined in this open method. The serial number is often
//	 *      important to know for later analysis and QC purposes.
//	 *   2. If no serial number, but some data is received and less than NOPEN_TRY
//	 *      attempts have been made, throw a timeout exception.  After NOPEN_TRY
//	 *      consecutive failures, just return from this open method without an
//	 *      exception, which will pass this DSMSensor to the select loop for reading,
//	 *      i.e. give up on getting the serial number - the data is more important.
//	 *   3. If a serial number, but no data is received, and less than NOPEN_TRY
//	 *      attempts have been tried, throw a timeout exception. This should be a
//	 *      rare situation because if the sonic responds to serial number queries, it
//	 *      generally should spout data.  After NOPEN_TRY consecutive failures,
//	 *      just return from this open method, and this DSMSensor will be passed to
//	 *      the select loop for reading. If a timeout value is defined for this DSMSensor,
//	 *      and data doesn't start arriving in that amount of time, then this
//	 *      DSMSensor will be scheduled for another open.
//	 */
//	if (serialNumber.empty()) {
//		// can't query sonic
//		_consecutiveOpenFailures++;
//		if (dataok) {
//			if (_consecutiveOpenFailures >= NOPEN_TRY) {
//				WLOG(("%s: Cannot query sonic serial number, but data received. %d open failures. Will try to read.",
//							getName().c_str(),_consecutiveOpenFailures));
//				return; // return from open, proceed to read data.
//			}
//			WLOG(("%s: Cannot query sonic serial number, but data received. %d open failures.",
//						getName().c_str(),_consecutiveOpenFailures));
//		}
//		throw n_u::IOTimeoutException(getName(),"open");
//	}
//	else {
//		// serial number query success, but no data - should be a rare occurence.
//		if (!dataok) {
//			_consecutiveOpenFailures++;
//			if (_consecutiveOpenFailures >= NOPEN_TRY) {
//				WLOG(("%s: Sonic serial number=\"%s\", but no data received. %d open failures. Will try to read.",
//							getName().c_str(),serialNumber.c_str(),_consecutiveOpenFailures));
//				return; // return from open, proceed to read data.
//			}
//			WLOG(("%s: Sonic serial number=\"%s\", but no data received. %d open failures.",
//						getName().c_str(),serialNumber.c_str(),_consecutiveOpenFailures));
//			throw n_u::IOTimeoutException(getName(),"open");
//		}
//		else
//			DLOG(("%s: successful open of CSAT3: serial number=\"",
//							getName().c_str()) << serialNumber << "\"");
//		_consecutiveOpenFailures = 0;   // what-a-ya-know, success!
//	}
}

n_c::CFG_MODE_STATUS CSAT3_Sonic::enterConfigMode() throw(n_u::IOException)
{
    n_c::CFG_MODE_STATUS cfgStatus = getConfigMode();
    bool timedOut = false;

    for (int i = 0; i < 2 && cfgStatus != ENTERED && !timedOut; i++) {
        DLOG(("%s: sending (P)T (nocr)",getName().c_str()));
        /*
         * P means print status and turn off internal triggering.
         * If rev 5 sonics are set to 60 Hz raw sampling or
         * 3x or 6x oversampling, they don't respond until sent a P
         */
        if (i > 1) write("PT",2);
        else write("T",1);
        try {
            unsigned int l;
            char buf[50];
            memset(buf, 0, 50);
            l = readEntireResponse(buf, 49, MSECS_PER_SEC + 10);
            if (l > 0 && strstr(buf, ">") != 0) {
                cfgStatus = ENTERED;
                setConfigMode(cfgStatus);
                ILOG(("%s:%s Successfully entered config mode!",
                      getName().c_str(), getClassName().c_str()));
                break;
            }
        }
        catch (const n_u::IOTimeoutException& e) {
            DLOG(("%s:%s CSAT3_Sonic::enterConfigMode(): timeout",getName().c_str(), getClassName().c_str()));
            timedOut = true;
            break;
        }
    }

    if (cfgStatus != ENTERED) {
        WLOG(("%s:%s cannot switch CSAT3 to terminal mode",
              getName().c_str(), getClassName().c_str()));
    }

    return cfgStatus;
}

float CSAT3_Sonic::correctTcForPathCurvature(float tc, float, float, float)
{
    // no correction necessary. CSAT outputs speed of sound
    // that is already corrected for path curvature.
    return tc;
}

bool CSAT3_Sonic::process(const Sample* samp, std::list<const Sample*>& results) throw()
{

    std::size_t inlen = samp->getDataByteLength();
    if (inlen < _windInLen) return false;	// not enough data
    if (inlen > _totalInLen + 2) return false;  // exclude wacko records

    const char* dinptr = (const char*) samp->getConstVoidDataPtr();
    // check for correct termination bytes
#ifdef DEBUG
    cerr << "inlen=" << inlen << ' ' << hex << (int)dinptr[inlen-2] <<
        ',' << (int)dinptr[inlen-1] << dec << endl;
#endif

    // Sometimes a serializer is connected to a sonic, in which
    // case it generates records longer than 12 bytes.
    // Check here that the record ends in 0x55 0xaa.
    if (dinptr[inlen-2] != '\x55' || dinptr[inlen-1] != '\xaa') return false;

    if (inlen > _totalInLen) inlen = _totalInLen;

#if __BYTE_ORDER == __BIG_ENDIAN
    /* Swap bytes of input. Campbell output is little endian */
    swab(dinptr,(char *)&_swapBuf.front(),inlen-2);     // dont' swap 0x55 0xaa
    const short* win = &_swapBuf.front();
#else
    const short* win = (const short*) dinptr;
#endif

    dsm_time_t timetag = samp->getTimeTag();

    // Reduce latency jitter in time tags
    if (_ttadjuster) timetag = _ttadjuster->adjust(timetag);

    /*
     * CSAT3 has an internal two sample buffer, so shift
     * wind time tags backwards by two samples.
     */

    /* restart sample time shifting on a data gap */
    if (_gapDtUsecs > 0 && (timetag - _ttlast) > _gapDtUsecs) _nttsave = -2;
    _ttlast = timetag;

    if (_nttsave < 0)
        _timetags[_nttsave++ + 2] = timetag;
    else {
        SampleT<float>* wsamp = getSample<float>(_windNumOut);
        wsamp->setTimeTag(_timetags[_nttsave]);
        wsamp->setId(_windSampleId);

        _timetags[_nttsave] = timetag;
        _nttsave = (_nttsave + 1) % 2;

        float* dout = wsamp->getDataPtr();

        unsigned short diag = (unsigned) win[4];
        int cntr = (diag & 0x003f);

        // special NaN encodings of diagnostic value
        // (F03F=61503 and F000=61440), where diag
        // bits (12 to 15) are set and a counter
        // value of 0 or 63. Range codes all 0.
        if (diag == 61503 || diag == 61440) {
            for (int i = 0; i < 4; i++) 
                dout[i] = floatNAN;
            diag = (diag & 0xf000) >> 12;
        }
        else {
            int range[3];
            range[0] = (diag & 0x0c00) >> 10;
            range[1] = (diag & 0x0300) >> 8;
            range[2] = (diag & 0x00c0) >> 6;
            diag = (diag & 0xf000) >> 12;

            if (diag && _nanIfDiag) {
                for (int i = 0; i < 4; i++) {
                        dout[i] = floatNAN;
                }
            }
            else {
                const float scale[] = {0.002,0.001,0.0005,0.00025};
                int nmissing = 0;
                for (int i = 0; i < 3; i++) {
                    if (win[i] == -32768) {
                        dout[i] = floatNAN;
                        nmissing++;
                    }
                    else {
                        dout[i] = win[i] * scale[range[i]];
                    }
                }

                /*
                 * Documentation also says speed of sound should be a NaN if
                 * ALL the wind components are NaNs.
                 */
                if (nmissing == 3 || win[3] == -32768)
                    dout[3] = floatNAN;
                else {
                    /* convert raw word to speed of sound in m/s */
                    float c = (win[3] * 0.001) + 340.0;
                    /* Convert speed of sound to Tc */
                    dout[3] = (c * c) / GAMMA_R - KELVIN_AT_0C;
                }
            }
        }

        if (_checkCounter) {
            if (_counter >=0 && ((++_counter % 64) != cntr)) diag += 16;
            _counter = cntr;
        }
        dout[4] = diag;

        // logical diagnostic value: set to 0 if all sonic
        // diagnostics are zero, otherwise one.
        if (_ldiagIndex >= 0) dout[_ldiagIndex] = (float)(diag != 0);

        // Note that despiking is optionally done before
        // transducer shadow correction. The idea is that
        // the despiking "should" replace spike values
        // with a value  more close to what should have
        // been actually measured.
        if (_spikeIndex >= 0 || getDespike()) {
            bool spikes[4] = {false,false,false,false};
            despike(wsamp->getTimeTag(),dout,4,spikes);

            if (_spikeIndex >= 0) {
                for (int i = 0; i < 4; i++) 
                    dout[i + _spikeIndex] = (float) spikes[i];
            }
        }

#ifdef HAVE_LIBGSL
        // apply shadow correction before correcting for unusual orientation
        transducerShadowCorrection(wsamp->getTimeTag(),dout);
#endif

        applyOrientation(wsamp->getTimeTag(), dout);

        offsetsTiltAndRotate(wsamp->getTimeTag(), dout);

        if (_spdIndex >= 0)
            dout[_spdIndex] = sqrt(dout[0] * dout[0] + dout[1] * dout[1]);
        if (_dirIndex >= 0) {
            float dr = atan2f(-dout[0],-dout[1]) * 180.0 / M_PI;
            if (dr < 0.0) dr += 360.;
            dout[_dirIndex] = dr;
        }
        results.push_back(wsamp);
    }

    // inlen is now less than or equal to the expected input length
    bool goodterm = dinptr[inlen-2] == '\x55' && dinptr[inlen-1] == '\xaa';

    for (unsigned int i = 0; i < _extraSampleTags.size(); i++) {

        if (inlen < _windInLen + (i+1) * 2) break;

        SampleTag* stag = _extraSampleTags[i];
        const vector<Variable*>& vars = stag->getVariables();
        std::size_t nvars = vars.size();
        SampleT<float>* hsamp = getSample<float>(nvars);
        hsamp->setTimeTag(timetag);
        hsamp->setId(stag->getId());

        unsigned short counts = ((const unsigned short*) win)[i+5];
        float volts;
        if (counts < 65500 && goodterm) volts = counts * 0.0001;
        else volts = floatNAN;

        for (unsigned int j = 0; j < nvars; j++) {
            Variable* var = vars[j];
            VariableConverter* conv = var->getConverter();
            if (!conv) hsamp->getDataPtr()[j] = volts;
            else hsamp->getDataPtr()[j] =
                conv->convert(hsamp->getTimeTag(),volts);
        }
        results.push_back(hsamp);
    }
    return true;
}

void CSAT3_Sonic::parseParameters() throw(n_u::InvalidParameterException)
{

    Wind3D::parseParameters();

    const list<const Parameter*>& params = getParameters();
    list<const Parameter*>::const_iterator pi = params.begin();

    for ( ; pi != params.end(); ++pi) {
        const Parameter* parameter = *pi;

        if (parameter->getName() == "oversample") {
            if (parameter->getType() != Parameter::BOOL_PARAM ||
                    parameter->getLength() != 1)
                throw n_u::InvalidParameterException(getName(),
                        parameter->getName(),
                        "must be boolean true or false");
            _oversample = (int)parameter->getNumericValue(0);
        }
        else if (parameter->getName() == "soniclog") {
            if (parameter->getType() != Parameter::STRING_PARAM ||
                    parameter->getLength() != 1)
                throw n_u::InvalidParameterException(getName(),
                        parameter->getName(),
                        "must be a string");
            _sonicLogFile = parameter->getStringValue(0);
        }
        else if (parameter->getName() == "configure") {
            if (parameter->getType() != Parameter::BOOL_PARAM ||
                    parameter->getLength() != 1)
                throw n_u::InvalidParameterException(getName(),
                        parameter->getName(),
                        "must be boolean true or false");
            _checkConfiguration = (int)parameter->getNumericValue(0);
        }
        else if (parameter->getName() == "checkCounter") {
            if (parameter->getType() != Parameter::BOOL_PARAM ||
                    parameter->getLength() != 1)
                throw n_u::InvalidParameterException(getName(),
                        parameter->getName(),
                        "must be boolean true or false");
            _checkCounter = (int)parameter->getNumericValue(0);
        }
    }

#ifdef HAVE_LIBGSL
    if (_shadowFactor != 0.0 && !_atCalFile) 
            throw n_u::InvalidParameterException(getName(),
                "shadowFactor","transducer shadowFactor is non-zero, but no abc2uvw cal file is specified");
#endif
}

void CSAT3_Sonic::checkSampleTags() throw(n_u::InvalidParameterException)
{

    Wind3D::checkSampleTags();

    list<SampleTag*>& tags= getSampleTags();

    if (tags.size() > 2 || tags.size() < 1)
        throw n_u::InvalidParameterException(getName() +
                " can only create two samples (wind and extra)");

    std::list<SampleTag*>::const_iterator si = tags.begin();
    for ( ; si != tags.end(); ++si) {
        SampleTag* stag = *si;
        /*
         * nvars
         * 5	u,v,w,tc,diag
         * 6	u,v,w,tc,diag,ldiag
         * 7	u,v,w,tc,diag,spd,dir
         * 8	u,v,w,tc,diag,ldiag,spd,dir
         * 9	u,v,w,tc,diag,uflag,vflag,wflag,tcflag
         * 11	u,v,w,tc,diag,spd,dir,uflag,vflag,wflag,tcflag
         */
        if (_windSampleId == 0) {
            std::size_t nvars = stag->getVariables().size();
            _rate = (int)rint(stag->getRate());
            if (!_ttadjuster && _rate > 0.0 && stag->getTimetagAdjustPeriod() > 0.0) 
                _ttadjuster = new TimetagAdjuster(_rate,
                        stag->getTimetagAdjustPeriod(),
                        stag->getTimetagAdjustSampleGap());
            _gapDtUsecs = 5 * USECS_PER_SEC;

            _windSampleId = stag->getId();
            _windNumOut = nvars;
            switch(nvars) {
            case 5:
            case 6:
            case 9:
                if (nvars == 9) _spikeIndex = 5;
                if (nvars == 6) _ldiagIndex = 5;
                break;
            case 11:
            case 7:
            case 8:
                if (nvars == 8) _ldiagIndex = 5;
                if (nvars == 11) _spikeIndex = 7;
                if (_spdIndex < 0 || _dirIndex < 0)
                    throw n_u::InvalidParameterException(getName() +
                            " CSAT3 cannot find speed or direction variables");
                break;
            default:
                throw n_u::InvalidParameterException(getName() +
                        " unsupported number of variables. Must be: u,v,w,tc,diag,[spd,dir][4xflags]]");
            }
        }
        else {
            _extraSampleTags.push_back(stag);
            _totalInLen += 2;	// 2 bytes for each additional input
        }
    }
#if __BYTE_ORDER == __BIG_ENDIAN
    _swapBuf.resize(_totalInLen/2);
#endif
}

bool CSAT3_Sonic::checkResponse()
{
    acqrate = -1;
    osc = 'Z';
    serialNumber = "";
    revision = "";
    rtsIndep = -1;
    recSep = -1;
    baudRate = -1;

    string query = querySonic(acqrate, osc, serialNumber, revision, rtsIndep, recSep, baudRate);
    if (query.length() == 0) {
    	return false;
    }

    DLOG(("%s: AQ=%d,os=%c,serial number=\"", getName().c_str(), acqrate, osc)
    		<< serialNumber << "\" rev=" << revision << ", rtsIndep(RI)=" << rtsIndep
			<< ", recSep(RS)=" << recSep << ", baud(BR)=" << baudRate);

	if (!serialNumber.empty()) {
		// Is current sonic rate OK?  If requested rate is 0, don't change.
		bool rateOK = _rate == 0;
		if (!_oversample && acqrate == _rate) {
			// osc blank or 0 means no oversampling
			if (osc == ' ' || osc == '0') rateOK = true;
		}
		if (_oversample && acqrate == 60) {
			if (_rate == 10 && osc == 'g') rateOK = true;
			if (_rate == 20 && osc == 'h') rateOK = true;
		}

		string rateResult;
		// set rate if it is different from what is desired, or sonic doesn't respond to first query.
		if (!rateOK) {
			assert(rateCmd != 0);
			rateResult = sendRateCommand(rateCmd);
			sleep(3);
			query = querySonic(acqrate, osc, serialNumber, revision, rtsIndep, recSep, baudRate);
			DLOG(("%s: AQ=%d,os=%c,serial number=",getName().c_str(),acqrate,osc) << serialNumber << " rev=" << revision);
		}

		// On rate or serial number change, log to file.
		if (!serialNumber.empty() && (!rateOK || serialNumber != getSerialNumber()) && _sonicLogFile.length() > 0) {
			n_u::UTime now;
			string fname = getDSMConfig()->expandString(_sonicLogFile);
			ofstream fst(fname.c_str(),ios_base::out | ios_base::app);
			fst << "csat3: " << getName() <<
				", id=" << getDSMId() << ',' << getSensorId() <<
				", " << serialNumber << ", " << revision << endl;
			fst << "time: " << now.format(true,"%Y %m %d %H:%M:%S") << endl;
			n_u::trimString(rateResult);
			if (rateResult.length() > 0) fst << rateResult << endl;
			n_u::trimString(query);
			fst << query << endl;
			fst << "##################" << endl;
			if (fst.fail()) PLOG(("%s: write failed",_sonicLogFile.c_str()));
			fst.close();
		}
		setSerialNumber(serialNumber);
	}

	if (!revision.empty()) {
		setSwVersion(revision);
	}

    return (query.length() != 0);
}

int baudToCSATCmdArg(int baud)
{
		if (baud == 9600) {
			return 0;
		}
		else if (baud == 19200) {
			return 1;
		}
		else {
			std::ostringstream msg("illegal parameter: ");
			msg << baud;
			throw InvalidParameterException("CSAT3_Sonic", "baudToCSATCmdArg()", msg.str());
		}
	return 0;
}

bool CSAT3_Sonic::installDesiredSensorConfig(const PortConfig& rDesiredConfig)
{

	/*
	 * First send the commands to change the baud rate
	 */
	sendRTSIndepCmd(false); // Turn off RTS Independent before changing the baud rate
	sendBaudCmd(baudToCSATCmdArg(rDesiredConfig.termios.getBaudRate()));
	sendRTSIndepCmd();

	/*
	 * Then change DSM baud rate according to rDesiredConfig
	 */
	setPortConfig(rDesiredConfig);
	applyPortConfig();

	/*
	 * Now check that we can still communicate
	 */
	std::string query = querySonic(acqrate, osc, serialNumber, revision, rtsIndep, recSep, baudRate);

	return ((rDesiredConfig.termios.getBaudRate() == 9600 && baudRate == 0) ||
		   (rDesiredConfig.termios.getBaudRate() == 19200 && baudRate == 1));
}

void CSAT3_Sonic::sendScienceParameters()
{
	sendRTSIndepCmd();
	sendRecSepCmd();
	sendRateCommand(rateCmd);
}

bool CSAT3_Sonic::checkScienceParameters()
{
	return doubleCheckResponse();
}
