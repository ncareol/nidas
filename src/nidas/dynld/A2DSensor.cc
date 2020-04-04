// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2007, Copyright University Corporation for Atmospheric Research
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

#include "A2DSensor.h"
#include <nidas/core/Variable.h>
#include <nidas/core/UnixIODevice.h>
#include <nidas/linux/diamond/dmd_mmat.h>

#include <nidas/util/Logger.h>

#include <cmath>

#include <iostream>

using namespace nidas::dynld;
using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

namespace nidas { namespace dynld {
int NIDAS_DYNLD_LINK_ME_DAMMIT;
}}

A2DSensor::A2DSensor() :
    DSMSensor(),_sampleCfgs(),_sampleInfos(),
    _badRawSamples(0),_maxNChannels(0),
    _convSlopes(0),_convIntercepts(0),
    _scanRate(0), _prevChan(-1),
    _gains(0),_bipolars(0)
{
    setLatency(0.1);
}

A2DSensor::~A2DSensor()
{
    delete [] _gains;
    delete [] _bipolars;
    delete [] _convSlopes;
    delete [] _convIntercepts;
    for (unsigned int i = 0; i < _sampleCfgs.size(); i++)
        delete _sampleCfgs[i];
}

void A2DSensor::open(int flags)
    	throw(n_u::IOException,n_u::InvalidParameterException)
{
    DSMSensor::open(flags);
    initParameters();
}

void A2DSensor::close() throw(n_u::IOException)
{
    DSMSensor::close();
}

void A2DSensor::init() throw(n_u::InvalidParameterException)
{
    DSMSensor::init();
    initParameters();
}

int A2DSensor::getGain(int ichan) const
{
    if (ichan < 0 || ichan >= _maxNChannels) return 0;
    return _gains[ichan];
}

int A2DSensor::getBipolar(int ichan) const
{
    if (ichan < 0 || ichan >= _maxNChannels) return -1;
    return _bipolars[ichan];
}

void A2DSensor::setA2DParameters(int ichan, int gain, int bipolar)
    throw(n_u::InvalidParameterException)
{
    initParameters();
    if (ichan < 0 || ichan >= _maxNChannels) {
        ostringstream ost;
        ost << "value=" << ichan << " is out of range of A2D";
        throw n_u::InvalidParameterException(getName(),
            "channel",ost.str());
    }
    _gains[ichan] = gain;
    _bipolars[ichan] = bipolar;
    getBasicConversion(ichan,_convIntercepts[ichan],_convSlopes[ichan]);
}

void A2DSensor::getA2DParameters(int ichan, int& gain, int& bipolar) const
{
    if (ichan < 0 || ichan >= _maxNChannels) {
        gain = 0;
        bipolar = -1;
        return;
    }
    gain = _gains[ichan];
    bipolar = _bipolars[ichan];
}

void A2DSensor::setConversionCorrection(int ichan, float corIntercept,
    float corSlope) throw(n_u::InvalidParameterException)
{
    initParameters();
    if (ichan < 0 || ichan >= _maxNChannels) {
        ostringstream ost;
        ost << "value=" << ichan << " is out of range of A2D";
        throw n_u::InvalidParameterException(getName(),
            "channel",ost.str());
    }

    float basIntercept,basSlope;
    getBasicConversion(ichan,basIntercept,basSlope);
     /*
     * corSlope and corIntercept are the slope and intercept
     * of an A2D calibration, where
     *    Vcorr = Vuncorr * corSlope + corIntercept
     *
     * Note that Vcorr is the Y (independent) variable. This is
     * because the A2D calibration is done in a similar
     * way to normal sensor calibration, where Y are the
     * set points from an input standard, and X is the measured
     * voltage value.
     *
     *    Vcorr = Vuncorr * corSlope + corIntercept
     *	    = (cnts * 20 / 65535 / gain + offset) * corSlope +
     *			corIntercept
     *	    = cnts * 20 / 65535 / gain * corSlope +
     *		offset * corSlope + corIntercept
     */
    _convSlopes[ichan] = basSlope * corSlope;
    _convIntercepts[ichan] = basIntercept * corSlope + corIntercept;
}

void A2DSensor::getConversion(int ichan,float& intercept, float& slope) const
{
    intercept = getIntercept(ichan);
    slope = getSlope(ichan);
}


void A2DSensor::initParameters()
{
    int n = getMaxNumChannels();
    if (n != _maxNChannels) {
        int* g = new int[n];
        int* b = new int[n];
        float* ci = new float[n];
        float* cs = new float[n];

        int i;
        for (i = 0; i < std::min(_maxNChannels,n); i++) {
            g[i] = _gains[i];
            b[i] = _bipolars[i];
            ci[i] = _convIntercepts[i];
            cs[i] = _convSlopes[i];
        }
        for ( ; i < n; i++) {
            g[i] = 0;
            b[i] = -1;
            ci[i] = 0.0;
            cs[i] = 1.0;
        }
        delete [] _gains;
        delete [] _bipolars;
        delete [] _convIntercepts;
        delete [] _convSlopes;
        _gains = g;
        _bipolars = b;
        _convIntercepts = ci;
        _convSlopes = cs;
        _maxNChannels = n;
    }
}


bool A2DSensor::process(const Sample* insamp,list<const Sample*>& results) throw()
{
    // pointer to raw A2D counts
    const short* sp = (const short*) insamp->getConstVoidDataPtr();

    // raw data are shorts
    if (insamp->getDataByteLength() % sizeof(short)) {
        _badRawSamples++;
        return false;
    }

    // number of short values in this raw sample.
    int nvalues = insamp->getDataByteLength() / sizeof(short);
    if (nvalues < 1) {
        _badRawSamples++;
        return false;      // nothin
    }
    const short* spend = sp + nvalues;

    unsigned int sindex = 0;
    // if more than one sample, the first value is an index
    if (_sampleInfos.size() > 1 || nvalues == _sampleInfos[0].nvalues + 1) {
        sindex = *sp++;
        if (sindex >=  _sampleInfos.size()) {
            _badRawSamples++;
            return false;
        }
    }

    A2DSampleInfo& sinfo = _sampleInfos[sindex];
    SampleTag* stag = sinfo.stag;
    const vector<Variable*>& vars = stag->getVariables();

    SampleT<float>* osamp = getSample<float>(sinfo.nvalues);
    osamp->setTimeTag(insamp->getTimeTag());
    osamp->setId(stag->getId());
    float *fp = osamp->getDataPtr();
    const float* fpend = fp + sinfo.nvalues;

    for (unsigned int ivar = 0; ivar < vars.size(); ivar++) {
        Variable* var = vars[ivar];
        int ichan = sinfo.channels[ivar];

        for (unsigned int ival = 0; sp < spend && ival < var->getLength();
             ival++, fp++)
        {
            short sval = *sp++;
            if (sval == -32768 || sval == 32767) {
                *fp = floatNAN;
                continue;
            }
            *fp = _convIntercepts[ichan] + _convSlopes[ichan] * sval;
        }
    }

    for ( ; fp < fpend; ) *fp++ = floatNAN;
    applyConversions(stag, osamp);
    results.push_back(osamp);

    return true;
}

void A2DSensor::validate()
        throw(n_u::InvalidParameterException)
{
    /*
     Hypothetical, somewhat complex sensor configuration:
     User wants to sample channels 0,4 and 5 at 10/s,
     and channels 1 and 2 at 100/s:

        <sensor>
            <parameter name="rate" value="1000"/>   // sample rate 1KHz
            <sample id="1" rate="10">               // output rate 10Hz
                <parameter name="filter" value="boxcar" type="string"/>
                <parameter name="numpoints" value="4" type="int"/>
                <variable name="IN0"/>  // default channel 0
                <variable name="IN4">
                   <parameter name="channel" value="4" type="int"/>
                </variable>
                <variable name="IN5"/>  // channel=5 is 1 plus previous
                </variable>
            </sample>
            <sample id="2" rate="100">              // output rate 100Hz
                <parameter name="filter" value="boxcar" type="string"/>
                <parameter name="numpoints" value="4" type="int"/>
                <variable name="IN1">
                   <parameter name="channel" value="1" type="int"/>
                </variable>
                <variable name="IN2"/>      // channel=2 is 1 plus previous
            </sample>
        </sensor>

     For the driver, we save this information in
     vector<struct chan_info> channels by channel number:
         channels[0]
            gain=X,bipolar=X,id=0 (id is one less than id="1")
         channels[1]
            gain=X,bipolar=X,id=1 (id is one less than id="2")
         channels[2]
            gain=X,bipolar=X,id=1 (id is one less than id="2")
         channels[3]
            gain=0,bipolar=0,id=0 (gain=0 means not sampled)
         channels[4]
            gain=X,bipolar=x,id=0 
         channels[5]
            gain=X,bipolar=X,id=0

     Also build vector<struct sample_info> samples:
     samples[0]
        id=0,rate=10,filterType=X,nvars=3,convSlopes=(m,m,m) convIntercepts=(b,b,b)
     samples[1]
        id=1,rate=100,filterType=X,nvars=2,convSlopes=(m,m) convIntercepts=(b,b)

     The process method will receive the individual samples. The first
     short int in the data is the id.
     For id==0, there should be 3 more short int values containing
     the A2D counts for variables IN0,IN4 and IN5. The data length
     of the raw sample should be 8 bytes.
     For id==1, there should be 2 more short int values containing
     the A2D counts for variables IN1,IN2, and the data length
     should be 6 bytes.

     If only one sample is configured, the driver does not add
     an id to the samples (backward compatibility)
     */
    DSMSensor::validate();

    const std::list<const Parameter*>& params = getParameters();
    list<const Parameter*>::const_iterator pi;
    for (pi = params.begin(); pi != params.end(); ++pi) {
        const Parameter* param = *pi;
        const string& pname = param->getName();
        if (pname == "rate") {
                if (param->getLength() != 1)
                    throw n_u::InvalidParameterException(getName(),"parameter",
                        "bad rate parameter");
                setScanRate((int)param->getNumericValue(0));
        }
        if (pname == "latency") {
        	if (param->getLength() != 1)
			throw n_u::InvalidParameterException(getName(),"parameter",
                        "bad latency  parameter");
                setLatency((int)param->getNumericValue(0));
        }
    }

    const std::list<SampleTag*>& tags = getSampleTags();
    std::list<SampleTag*>::const_iterator ti = tags.begin();

    for ( ; ti != tags.end(); ++ti) {
        SampleTag* tag = *ti;

        float frate = tag->getRate();
        if (fmodf(frate,1.0) != 0.0) {
            ostringstream ost;
            ost << frate;
            throw n_u::InvalidParameterException(getName(),
                    "rate must be an integer",ost.str());
        }

        int rate = (int)frate;
        if (getScanRate() < rate) setScanRate(rate);
        int boxcarNpts = 1;
        bool temperature = false;

        // Default time average rate is the sample rate.
        // User can choose a time average rate that is a multiple
        // of the sample rate, in which case the results of the 
        // time averaging are subsampled by a pickoff of 1 out of
        // every N time averages, where N = timeavgRate / sample rate.
        int timeavgRate = tag->getRate();

        enum nidas_short_filter filterType = NIDAS_FILTER_PICKOFF;
        const std::list<const Parameter*>& params = tag->getParameters();
        list<const Parameter*>::const_iterator pi;
        for (pi = params.begin(); pi != params.end(); ++pi) {
            const Parameter* param = *pi;
            const string& pname = param->getName();
            if (pname == "filter") {
                    if (param->getType() != Parameter::STRING_PARAM ||
                        param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),"sample",
                            "filter parameter is not a string");
                    string fname = param->getStringValue(0);
                    if (fname == "boxcar") filterType = NIDAS_FILTER_BOXCAR;
                    else if (fname == "pickoff") filterType = NIDAS_FILTER_PICKOFF;
                    else if (fname == "timeavg") filterType = NIDAS_FILTER_TIMEAVG;
                    else throw n_u::InvalidParameterException(getName(),"sample",
                            fname + " filter is not supported");
            }
            else if (pname == "numpoints") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),"sample",
                            "bad numpoints parameter");
                    boxcarNpts = (int)param->getNumericValue(0);
            }
            else if (pname == "rate") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),"sample",
                            "bad rate parameter");
                    timeavgRate = (int)param->getNumericValue(0);
            }
            else if (pname == "temperature") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),"sample",
                            "bad temperature parameter");
                    temperature = (int)param->getNumericValue(0);
            }
        }
        if (temperature) continue;

        if (filterType == NIDAS_FILTER_BOXCAR && boxcarNpts <= 0) {
            throw n_u::InvalidParameterException(getName(),"numpoints",
                "numpoints parameter must be > 0 with boxcar filter");

        }
        if (filterType == NIDAS_FILTER_TIMEAVG) {
            ostringstream ost;
            if (timeavgRate <= 0) {
                ost << timeavgRate << " Hz must be > 0";
                throw n_u::InvalidParameterException(getName(), 
                    "timeavg rate", ost.str());
            }
            if (fmod((double) timeavgRate, tag->getRate()) != 0.0) {
                ost << timeavgRate <<
                    " Hz must be a multiple of the sample rate=" <<
                    tag->getRate() << " Hz";
                throw n_u::InvalidParameterException(getName(),
                    "timeavg rate", ost.str());
            }
        }

        int sindex = _sampleInfos.size();       // sample index, 0,1,...

        const vector<Variable*>& vars = tag->getVariables();
        int nvars = vars.size();

        A2DSampleInfo sinfo(nvars);
        sinfo.stag = tag;

        A2DSampleConfig* scfg;

        switch (filterType) {
        case NIDAS_FILTER_TIMEAVG:
            scfg = new A2DTimeAvgConfig(timeavgRate);
            break;
        case NIDAS_FILTER_BOXCAR:
            scfg = new A2DBoxcarConfig(boxcarNpts);
            break;
        default:
            scfg = new A2DSampleConfig();
            break;
        }
        nidas_a2d_sample_config& ncfg = scfg->cfg();

        ncfg.sindex = sindex;
        ncfg.nvars = nvars;
        ncfg.rate = rate;
        ncfg.filterType = filterType;

        int nvalues = 0;
        for (int iv = 0; iv < nvars; iv++) {
            Variable* var = vars[iv];

            float fgain = 0.0;
            int bipolar = -1;   // unknown

            int ichan = _prevChan + 1;
            float corSlope = 1.0;
            float corIntercept = 0.0;
            bool rawCounts = false;

            nvalues += var->getLength();

            const std::list<const Parameter*>& vparams = var->getParameters();
            list<const Parameter*>::const_iterator pi;
            for (pi = vparams.begin(); pi != vparams.end(); ++pi) {
                const Parameter* param = *pi;
                const string& pname = param->getName();
                if (pname == "gain") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),
                            pname,"no value");

                    fgain = param->getNumericValue(0);
                }
                else if (pname == "bipolar") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),
                            pname,"no value");
                    bipolar = param->getNumericValue(0) != 0;
                }
                else if (pname == "channel") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),pname,"no value");
                    ichan = (int)param->getNumericValue(0);
                }
                else if (pname == "corSlope") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),
                            pname,"no value");
                    corSlope = param->getNumericValue(0);
                }
                else if (pname == "corIntercept") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),
                            pname,"no value");
                    corIntercept = param->getNumericValue(0);
                }
                else if (pname == "rawCounts") {
                    if (param->getLength() != 1)
                        throw n_u::InvalidParameterException(getName(),
                            pname,"no value");
                    rawCounts = param->getNumericValue(0);
                }
            }
            if (ichan < 0 || ichan >= getMaxNumChannels()) {
                ostringstream ost;
                ost << "value=" << ichan << " is outside the range 0:" <<
                    (getMaxNumChannels() - 1);
                throw n_u::InvalidParameterException(getName(),
                        "channel",ost.str());
            }

            if (fgain == 0.0) fgain = getGain(ichan);
            if (fmodf(fgain,1.0) != 0.0) {
                ostringstream ost;
                ost << "channel " << ichan << " gain=" << fgain << " but must be an integer";
                throw n_u::InvalidParameterException(getName(),
                        "gain",ost.str());
            }
            int gain = (int)fgain;
            if (gain == 0) {
                ostringstream ost;
                ost << "channel " << ichan << " gain not specified";
                throw n_u::InvalidParameterException(getName(),
                        "gain",ost.str());
            }
            if (bipolar < 0) bipolar = getBipolar(ichan);
            if (bipolar < 0) {
                ostringstream ost;
                ost << "channel " << ichan << " polarity not specified";
                throw n_u::InvalidParameterException(getName(),
                        "bipolar",ost.str());
            }

            // cerr << "ichan=" << ichan << " gain=" << gain << " bipolar=" << bipolar << endl;
            setA2DParameters(ichan,gain,bipolar);
            setConversionCorrection(ichan,corIntercept,corSlope);

            var->setA2dChannel(ichan);
            sinfo.channels[iv] = ichan;

            ncfg.channels[iv] = ichan;
            ncfg.gain[iv] = gain;
            ncfg.bipolar[iv] = bipolar;
            _prevChan = ichan;
        }
        sinfo.nvalues = nvalues;

        _sampleInfos.push_back(sinfo);
        _sampleCfgs.push_back(scfg);
    }
}

