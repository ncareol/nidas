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

#include <sstream>
#include <fstream>

using namespace nidas::dynld::isff;
using namespace std;

namespace n_u = nidas::util;

NIDAS_CREATOR_FUNCTION_NS(isff,CSAT3_Sonic)

const float CSAT3_Sonic::GAMMA_R = 402.684;

CSAT3_Sonic::CSAT3_Sonic():
    Wind3D(),
    _windInLen(12),	// two bytes each for u,v,w,tc,diag, and 0x55aa
    _totalInLen(12),
    _windNumOut(0),
    _spikeIndex(-1),
    _windSampleId(0),
    _extraSampleTags(),
    _nttsave(-2),
    _counter(-1),
    _swapBuf(),
    _rate(0),
    _oversample(false),
    _serialNumber(),_sonicLogFile(),
    _gapDtUsecs(0),
    _ttlast(0),
    _nanIfDiag(true),
    _consecutiveOpenFailures(0),
    _checkConfiguration(true),
    _checkCounter(true),
    _ttadjuster(0)
{
}

CSAT3_Sonic::~CSAT3_Sonic()
{
    if (_ttadjuster) {
        _ttadjuster->log(nidas::util::LOGGER_INFO, this);
    }
    delete _ttadjuster;
}

bool CSAT3_Sonic::dataMode()
{
    clearBuffer();

    DLOG(("%s: sending D (nocr)",getName().c_str()));
    write("D",1);
    usleep(250 * USECS_PER_MSEC);
    size_t ml = getMessageLength() + getMessageSeparator().length();

    // read until we get an actual sample or 5 seconds have elapsed.
    n_u::UTime quit;
    quit += USECS_PER_SEC * 5;

    int nbad = 0;

    for (int ntimeout = 0; ; ) {
        try {
            for (;;) {
                bool goodsample = false;
                readBuffer(1 * MSECS_PER_SEC);
                DLOG(("%s: CSAT3 buffer read",getName().c_str()));
                for (Sample* samp = nextSample(); samp; samp = nextSample()) {
                    // Sample might be slightly larger that what is configured
                    // if a serializer is adding some bytes
                    // Or if a port is configured for serializer, but no serializer
                    // is present, the records will be of length 24 (2*12) instead
                    // of the expected 14.
                    if (samp->getDataByteLength() >= ml &&
                        samp->getDataByteLength() < ml*2) goodsample = true;
                    else nbad++;

                    distributeRaw(samp);
                }
                if (goodsample) return true;
                if (nbad > 0) ILOG(("%s: %d unrecognized samples", getName().c_str(),nbad));
                if (n_u::UTime() > quit) {
                    ILOG(("%s: timeout reading CSAT3 samples",getName().c_str()));
                    return false;
                }
            }
        }
        catch (const n_u::IOTimeoutException& e) {
            if ((++ntimeout % 3)) {
                ILOG(("%s: timeout reading CSAT3 data, sending D (nocr)",getName().c_str()));
                write("D",1);
            }
            else {
                ILOG(("%s: timeout reading CSAT3 data, sending D& (nocr)",getName().c_str()));
                write("D&",2);
            }
            if (n_u::UTime() > quit) return false;
        }
    }
    return false;
}

/* static */
string CSAT3_Sonic::getSerialNumber(const string& str,
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
        string& revision, int& rtsIndep, int& recSep)
{
    string result;

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
        DLOG(("%s: sending ?? CR",getName().c_str()));
        write("??\r",3);    // must send CR
        // sonic takes a while to respond to ??
        int timeout = 4 * MSECS_PER_SEC;
        result.clear();
        string::size_type stidx = string::npos;

        // read until timeout, or complete message
        for ( ; !scanned && n_u::UTime() < quit; ) {
            try {
                readBuffer(timeout);
                for (Sample* samp = nextSample(); samp; samp = nextSample()) {
                    unsigned int l = samp->getDataByteLength();
                    // strings will not be null terminated
                    const char * cp = (const char*)samp->getConstVoidDataPtr();
                    // sonic echoes back "T" or "??" command
                    if (result.length() == 0)
                        while (l && (*cp == 'T' || *cp == '?' || ::isspace(*cp))) { cp++; l--; }
                    string rec(cp,l);
                    DLOG(("%s: CSAT3 query: len=",getName().c_str())
                            << rec.length() << ", \"" << rec << '"');

                    result += rec;
                    distributeRaw(samp);
                }
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
                serialNumber = getSerialNumber(result, snidx);
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
    if (fs != string::npos && fs + 3 < rlen)
        acqrate = atoi(result.substr(fs+3).c_str());

    // get os parameter, e.g. "os=g"
    // g for 10Hz 6x oversampling
    // h for 20Hz 3x oversampling
    // ' ' otherwise
    // For version 4, os=0 means no oversampling
    fs = result.find("os=");
    if (fs != string::npos && fs + 3 < rlen) osc = result[fs+3];

    // get software revision, e.g. "rev 3.0f"
    fs = result.find("rev");
    if (fs != string::npos && fs + 4 < rlen) {
        string::size_type bl = result.find(' ',fs+4);
        revision = result.substr(fs+4,bl-fs-4);
    }

    // get RI=n setting. 0=power RS-232 drivers on RTS, 1=power always
    fs = result.find("RI=");
    if (fs != string::npos && fs + 3 < rlen)
        rtsIndep = atoi(result.substr(fs+3).c_str());

    // get RS=n setting. 0=no record separator, 1=0x55AA
    fs = result.find("RS=");
    if (fs != string::npos && fs + 3 < rlen)
        recSep = atoi(result.substr(fs+3).c_str());

    return result;
}

string CSAT3_Sonic::sendRateCommand(const char* cmd)
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

void CSAT3_Sonic::open(int flags)
{
    SerialSensor::open(flags);

    if (!_checkConfiguration) return;

    const int NOPEN_TRY = 5;

    const char* rateCmd = 0;
    DLOG(("%s: _rate=%d",getName().c_str(),_rate));
    if (_rate > 0) {
        rateCmd = getRateCommand(_rate,_oversample);
        if (!rateCmd) {
            ostringstream ost;
            ost << "rate=" << _rate << " Hz not supported with oversample=" << _oversample;
            throw n_u::InvalidParameterException(getName(),
                    "sample rate",ost.str());
        }
    }

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

    int ml = getMessageLength();
    string sep = getMessageSeparator();
    bool eom = getMessageSeparatorAtEOM();

    // switch sonic to terminal mode
    terminalMode();

    int acqrate = 0;
    string serialNumber;
    char osc = ' ';
    string revision;
    int rtsIndep, recSep;

    string query = querySonic(acqrate, osc, serialNumber, revision,
            rtsIndep, recSep);
    DLOG(("%s: AQ=%d,os=%c,serial number=\"",getName().c_str(),acqrate,osc) << serialNumber << "\" rev=" << revision << ", rtsIndep(RI)=" << rtsIndep <<
            ", recSep(RS)=" << recSep);

    // set RI=1, always power RS232, independent of RTS
    if (rtsIndep != 1) {
        write("ri 1\r",5);
        usleep(100 * USECS_PER_MSEC);
    }
    // set RS=1, send 0x55AA record separator
    if (recSep != 1) {
        write("rs 1\r",5);
        usleep(100 * USECS_PER_MSEC);
    }

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
            query = querySonic(acqrate,osc,serialNumber,revision, rtsIndep, recSep);
            DLOG(("%s: AQ=%d,os=%c,serial number=",getName().c_str(),acqrate,osc) << serialNumber << " rev=" << revision);
        }

        // On rate or serial number change, log to file.
        if (!serialNumber.empty() && (!rateOK || serialNumber != _serialNumber) && _sonicLogFile.length() > 0) {
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
        _serialNumber = serialNumber;
    }

    try {
        setMessageParameters(ml,sep,eom);
    }
    catch(const n_u::InvalidParameterException& e) {
        throw n_u::IOException(getName(),"open",e.what());
    }

    // returns true if some recognizeable samples are received.
    bool dataok = dataMode();

    /*
     * An IOTimeoutException is thrown, which will cause another open attempt to
     * be scheduled, in the following circumstances:
     *   1. If a serial number is not successfully queried, and no data is arriving.
     *      We don't add any extra log messages in this case to avoid filling up the
     *      logs when a sonic simply isn't connected. We don't want to give up in
     *      this case, and return without an exception, because then a sonic might
     *      later be connected and the data read without knowing the serial number,
     *      which is only determined in this open method. The serial number is often
     *      important to know for later analysis and QC purposes.
     *   2. If no serial number, but some data is received and less than NOPEN_TRY
     *      attempts have been made, throw a timeout exception.  After NOPEN_TRY
     *      consecutive failures, just return from this open method without an
     *      exception, which will pass this DSMSensor to the select loop for reading,
     *      i.e. give up on getting the serial number - the data is more important.
     *   3. If a serial number, but no data is received, and less than NOPEN_TRY
     *      attempts have been tried, throw a timeout exception. This should be a
     *      rare situation because if the sonic responds to serial number queries, it
     *      generally should spout data.  After NOPEN_TRY consecutive failures,
     *      just return from this open method, and this DSMSensor will be passed to
     *      the select loop for reading. If a timeout value is defined for this DSMSensor,
     *      and data doesn't start arriving in that amount of time, then this
     *      DSMSensor will be scheduled for another open.
     */
    if (serialNumber.empty()) {
        // can't query sonic
        _consecutiveOpenFailures++;
        if (dataok) {
            if (_consecutiveOpenFailures >= NOPEN_TRY) {
                WLOG(("%s: Cannot query sonic serial number, but data received. %d open failures. Will try to read.",
                            getName().c_str(),_consecutiveOpenFailures));
                return; // return from open, proceed to read data.
            }
            WLOG(("%s: Cannot query sonic serial number, but data received. %d open failures.",
                        getName().c_str(),_consecutiveOpenFailures));
        }
        throw n_u::IOTimeoutException(getName(),"open");
    }
    else {
        // serial number query success, but no data - should be a rare occurence.
        if (!dataok) {
            _consecutiveOpenFailures++;
            if (_consecutiveOpenFailures >= NOPEN_TRY) {
                WLOG(("%s: Sonic serial number=\"%s\", but no data received. %d open failures. Will try to read.",
                            getName().c_str(),serialNumber.c_str(),_consecutiveOpenFailures));
                return; // return from open, proceed to read data.
            }
            WLOG(("%s: Sonic serial number=\"%s\", but no data received. %d open failures.",
                        getName().c_str(),serialNumber.c_str(),_consecutiveOpenFailures));
            throw n_u::IOTimeoutException(getName(),"open");
        }
        else
            DLOG(("%s: successful open of CSAT3: serial number=\"",
                            getName().c_str()) << serialNumber << "\"");
        _consecutiveOpenFailures = 0;   // what-a-ya-know, success!
    }
}

bool CSAT3_Sonic::terminalMode()
{

    // in terminal mode, sonic sends ">" prompts
    try {
        setMessageParameters(0,">",true);
    }
    catch(const n_u::InvalidParameterException& e) {
        throw n_u::IOException(getName(),"open",e.what());
    }

    bool rcvdPrompt = false;
    bool rcvdTimeout = false;
    for (int i = 0; i < 2; i++) {
        DLOG(("%s: sending T (nocr)",getName().c_str()));
        /*
         * P means print status and turn off internal triggering.
         * If rev 5 sonics are set to 60 Hz raw sampling or
         * 3x or 6x oversampling, they don't respond until sent a P
         */
        if (i > 1) write("PT",2);
        else write("T",1);
        try {
            for (int j = 0; j < 20; j++) {
                unsigned int l;
                l = readBuffer(MSECS_PER_SEC + 10);
                rcvdPrompt = false;
                for (Sample* samp = nextSample(); samp; samp = nextSample()) {
                    if ((l = samp->getDataByteLength()) > 0 &&
                            ((const char*)samp->getConstVoidDataPtr())[l-1] == '>') rcvdPrompt = true;
                    distributeRaw(samp);
                }
            }
        }
        catch (const n_u::IOTimeoutException& e) {
            DLOG(("%s: timeout",getName().c_str()));
            rcvdTimeout = true;
            break;
        }
    }

    if (!rcvdTimeout && !rcvdPrompt)
        WLOG(("%s: cannot switch CSAT3 to terminal mode",getName().c_str()));
    return rcvdTimeout || rcvdPrompt;
}

float CSAT3_Sonic::correctTcForPathCurvature(float tc, float, float, float)
{
    // no correction necessary. CSAT outputs speed of sound
    // that is already corrected for path curvature.
    return tc;
}

bool CSAT3_Sonic::process(const Sample* samp,
        std::list<const Sample*>& results)
{
    size_t inlen = samp->getDataByteLength();
    if (inlen < _windInLen) return false;	// not enough data
    if (inlen > _totalInLen + 2) return false;  // exclude wacko records

    const char* dinptr = (const char*) samp->getConstVoidDataPtr();
    // check for correct termination bytes
    VLOG(("") << "inlen=" << inlen << ' ' << hex << (int)dinptr[inlen-2]
         << ',' << (int)dinptr[inlen-1] << dec);

    // Sometimes a serializer is connected to a sonic, in which
    // case it generates records longer than 12 bytes.
    // Check here that the record ends in 0x55 0xaa.
    if (dinptr[inlen-2] != '\x55' || dinptr[inlen-1] != '\xaa') return false;

    if (inlen > _totalInLen) inlen = _totalInLen;

    const short* win;
    if (__BYTE_ORDER == __BIG_ENDIAN)
    {
        /* Swap bytes of input. Campbell output is little endian */
        swab(dinptr,(char *)&_swapBuf.front(),inlen-2);     // dont' swap 0x55 0xaa
        win = &_swapBuf.front();
    }
    else
    {
        win = (const short*) dinptr;
    }

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

        // apply shadow correction before correcting for unusual orientation
        transducerShadowCorrection(wsamp->getTimeTag(), dout);

        applyOrientation(wsamp->getTimeTag(), dout);

        offsetsTiltAndRotate(wsamp->getTimeTag(), dout);

        if (_spdIndex >= 0)
            dout[_spdIndex] = sqrt(dout[0] * dout[0] + dout[1] * dout[1]);
        if (_dirIndex >= 0) {
            dout[_dirIndex] = n_u::dirFromUV(dout[0], dout[1]);
        }
        results.push_back(wsamp);
    }

    // inlen is now less than or equal to the expected input length
    bool goodterm = dinptr[inlen-2] == '\x55' && dinptr[inlen-1] == '\xaa';

    for (unsigned int i = 0; i < _extraSampleTags.size(); i++) {

        if (inlen < _windInLen + (i+1) * 2) break;

        SampleTag* stag = _extraSampleTags[i];
        const vector<Variable*>& vars = stag->getVariables();
        size_t nvars = vars.size();
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

void CSAT3_Sonic::parseParameters()
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

    if (_shadowFactor != 0.0 && !_atCalFile) 
            throw n_u::InvalidParameterException(getName(),
                "shadowFactor","transducer shadowFactor is non-zero, but no abc2uvw cal file is specified");
}

void CSAT3_Sonic::checkSampleTags()
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
            size_t nvars = stag->getVariables().size();
            _rate = (int)rint(stag->getRate());
            if (!_ttadjuster && _rate > 0.0 && stag->getTimetagAdjust() > 0.0) 
                _ttadjuster = new TimetagAdjuster(stag->getId(), _rate);
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
    if (__BYTE_ORDER == __BIG_ENDIAN)
        _swapBuf.resize(_totalInLen/2);
}

