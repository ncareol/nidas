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

#include "CVIProcessor.h"
#include "CVI_LV_Input.h"

#include <nidas/core/Project.h>
#include <nidas/core/DSMConfig.h>
#include <nidas/core/SampleOutputRequestThread.h>
#include <nidas/core/Variable.h>
#include <nidas/util/Logger.h>

using namespace nidas::core;
using namespace nidas::dynld;
using namespace nidas::dynld::raf;
using namespace std;

namespace n_u = nidas::util;

NIDAS_CREATOR_FUNCTION_NS(raf,CVIProcessor)

CVIProcessor::CVIProcessor(): SampleIOProcessor(false),
    _connectionMutex(),_connectedSources(),_connectedOutputs(),
    _outputSampleTag(0),_d2aDeviceName(),_digioDeviceName(),
    _varMatched(),_averager(),
    _rate(0.0),_lvSampleId(0),_aout(),_dout(),
    _numD2A(0),_numDigout(0),_site(0)
{
    for (unsigned int i = 0; i < sizeof(_douts)/sizeof(_douts[0]); i++)
        _douts[i] = -1;
    for (unsigned int i = 0; i < sizeof(_vouts)/sizeof(_vouts[0]); i++)
        _vouts[i] = -99.0;
    setName("CVIProcessor");
}

CVIProcessor::~CVIProcessor()
{
    std::set<SampleOutput*>::const_iterator oi = _connectedOutputs.begin();
    for ( ; oi != _connectedOutputs.end(); ++oi) {
        SampleOutput* output = * oi;
        _averager.removeSampleClient(output);

        output->flush();
        try {
            output->close();
        }
        catch (const n_u::IOException& ioe) {
            n_u::Logger::getInstance()->log(LOG_ERR,
                "%s: error closing %s: %s",
                getName().c_str(),output->getName().c_str(),ioe.what());
        }

        SampleOutput* orig = output->getOriginal();
        if (orig != output)
            delete output;
    }
}

void CVIProcessor::addRequestedSampleTag(SampleTag* tag)
{
    if (getSampleTags().size() > 1)
        throw n_u::InvalidParameterException("CVIProcessor","sample","cannot have more than one sample");

    if (!_site) {
        const DSMConfig* dsm = getDSMConfig();
        if (dsm) _site = dsm->getSite();
    }

    _outputSampleTag = tag;

    const vector<Variable*>& vars = tag->getVariables();
    vector<Variable*>::const_iterator vi = vars.begin();
    for ( ; vi != vars.end(); ++vi) {
        Variable* var = *vi;
        if (_site) var->setSite(_site);
        _varMatched.push_back(false);
        _averager.addVariable(var);
    }
    if (tag->getRate() <= 0.0) {
        ostringstream ost;
        ost << "invalid rate: " << _rate;
        throw n_u::InvalidParameterException("CVIProcessor","sample",ost.str());
    }
    _averager.setAveragePeriodSecs(1.0/tag->getRate());

    // SampleIOProcessor will delete
    SampleIOProcessor::addRequestedSampleTag(tag);
    addSampleTag(_outputSampleTag);
}

void CVIProcessor::connectSource(SampleSource* source)
{
    /*
     * In the typical usage on a DSM, this connection will
     * be from the SamplePipeline.
     */
    source = source->getProcessedSampleSource();
    assert(source);

    _connectionMutex.lock();

    // on first SampleSource connection, request output connections.
    if (_connectedSources.size() == 0) {
        const list<SampleOutput*>& outputs = getOutputs();
        list<SampleOutput*>::const_iterator oi = outputs.begin();
        for ( ; oi != outputs.end(); ++oi) {
            SampleOutput* output = *oi;
            // some SampleOutputs want to know what they are getting
            output->addSourceSampleTags(getSampleTags());
            SampleOutputRequestThread::getInstance()->addConnectRequest(output,this,0);
        }
    }
    _connectedSources.insert(source);
    _connectionMutex.unlock();

    DSMSensor* sensor = 0;

    SampleTagIterator inti = source->getSampleTagIterator();
    for ( ; inti.hasNext(); ) {
        const SampleTag* intag = inti.next();
        dsm_sample_id_t sensorId = intag->getId() - intag->getSampleId();
        // find sensor for this sample
        sensor = Project::getInstance()->findSensor(sensorId);

#ifdef DEBUG
        if (sensor) cerr << "CVIProcessor::connect sensor=" <<
            sensor->getName() << " (" << GET_DSM_ID(sensorId) << ',' <<
            GET_SHORT_ID(sensorId) << ')' << endl;
        else cerr << "CVIProcessor no sensor" << endl;
#endif

        // can throw IOException
        if (sensor && _lvSampleId == 0 && dynamic_cast<CVI_LV_Input*>(sensor))
                attachLVInput(source,intag);
        // sensor->setApplyVariableConversions(true);
    }

    _averager.connect(source);
}

void CVIProcessor::disconnectSource(SampleSource* source) throw()
{
    source = source->getProcessedSampleSource();

    _connectionMutex.lock();
    _connectedSources.erase(source);
    _connectionMutex.unlock();

    _averager.disconnect(source);
    _averager.flush();
    source->removeSampleClient(this);
    _aout.close();
    _dout.close();
}

void CVIProcessor::attachLVInput(SampleSource* source, const SampleTag* tag)
{
    // cerr << "CVIProcessor::attachLVInput: sensor=" <<
      //   _lvSensor->getName() << endl;
    if (getD2ADeviceName().length() > 0) {
        _aout.setDeviceName(getD2ADeviceName());
        _aout.open();
        _numD2A = _aout.getNumOutputs();
#ifdef DEBUG
        cerr << "numD2A=" << _numD2A << endl;
#endif
        for (unsigned int i = 0; i < sizeof(_vouts)/sizeof(_vouts[0]); i++)
            _vouts[i] = -99.0;
    }

    if (getDigIODeviceName().length() > 0) {
        _dout.setDeviceName(getDigIODeviceName());
        _dout.open();
        _numDigout = _dout.getNumOutputs();
        for (unsigned int i = 0; i < sizeof(_douts)/sizeof(_douts[0]); i++)
            _douts[i] = -1;
#ifdef DEBUG
        cerr << "numDigout=" << _numDigout << endl;
#endif
    }
    _lvSampleId = tag->getId();
    source->addSampleClientForTag(this,tag);
}

void CVIProcessor::connect(SampleOutput* output) throw()
{
    ILOG(("CVIProcessor::connect from ") << output->getName());
    _connectionMutex.lock();
    _averager.addSampleClient(output);
    _connectedOutputs.insert(output);
    _connectionMutex.unlock();
}

void CVIProcessor::disconnect(SampleOutput* output) throw()
{
    _averager.removeSampleClient(output);

    _connectionMutex.lock();
    _connectedOutputs.erase(output);
    _connectionMutex.unlock();

    output->flush();
    try {
        output->close();
    }
    catch (const n_u::IOException& ioe) {
        n_u::Logger::getInstance()->log(LOG_ERR,
            "%s: error closing %s: %s",
            getName().c_str(),output->getName().c_str(),ioe.what());
    }

    SampleOutput* orig = output->getOriginal();
    if (orig != output)
        SampleOutputRequestThread::getInstance()->addDeleteRequest(output);

    // reschedule a request for the original output.
    int delay = orig->getReconnectDelaySecs();
    if (delay < 0) return;
    SampleOutputRequestThread::getInstance()->addConnectRequest(orig,this,delay);
}

void CVIProcessor::flush() throw()
{
    _averager.flush();
    std::set<SampleOutput*>::const_iterator oi = _connectedOutputs.begin();
    for ( ; oi != _connectedOutputs.end(); ++oi) {
        SampleOutput* output = * oi;
        output->flush();
    }
}

bool CVIProcessor::receive(const Sample *insamp) throw()
{
#ifdef DEBUG
    cerr << "CVIProcessor::receive, insamp length=" <<
        insamp->getDataByteLength() << endl;
#endif
    if (insamp->getId() != _lvSampleId) return false;
    if (insamp->getType() != FLOAT_ST) return false;

    const SampleT<float>* fsamp = (const SampleT<float>*) insamp;
    const float* fin = fsamp->getConstDataPtr();

    unsigned int ndata = fsamp->getDataLength();
    unsigned int nout = std::min((unsigned int)(sizeof(_vouts)/sizeof(_vouts[0])),_numD2A);

    vector<int> which;
    vector<float> volts;
#ifdef DEBUG
    cerr << "LV receive " << endl;
    for (unsigned int i = 0; i < ndata ; i++) cerr << fin[i] << ' ';
    cerr << endl;
#endif

    unsigned int idata = 1;     // skip seconds value
    for (unsigned int iout = 0; iout < nout && idata < ndata; iout++,idata++) {
        float f = fin[idata];
        if (fabs(f - _vouts[iout]) > 1.e-3) {
            int ix = iout;
            /* Temporary flip of 3 and 4 to correct for cross-wired outputs on GV.
             * This wiring issue does not exist on the C130.
             * It is unknown at this point whether the GV rack still needs this switch.
             */
#ifdef FLIP_VOUT_3_4
            if (iout == 3) ix = 4;
            else if (iout == 4) ix = 3;
#endif
            which.push_back(ix);
            volts.push_back(f);
            _vouts[iout] = f;
#ifdef DEBUG
            cerr << "setting VOUT " << ix << " to " << f << endl;
#endif
        }
    }
    try {
        if (which.size() > 0) _aout.setVoltages(which,volts);
    }
    catch(n_u::IOException& e) {
        n_u::Logger::getInstance()->log(LOG_ERR,"%s: %s\n",
            _aout.getName().c_str(),e.what());
    }

    nout = std::min((unsigned int)(sizeof(_douts)/sizeof(_douts[0])),_numDigout);
    n_u::BitArray dwhich(_numDigout);
    n_u::BitArray dvals(_numDigout);
    for (unsigned int iout = 0; iout < nout && idata < ndata; iout++,idata++) {
        int d = (fin[idata] != 0.0); // change to boolean 0 or 1
        if (d != _douts[iout]) {
            dwhich.setBit(iout,1);
            dvals.setBit(iout,d);
            _douts[iout] = d;
#ifdef DEBUG
            cerr << "setting DOUT pin " << iout <<
                        " to " << d << endl;
#endif
        }
    }
    try {
        if (dwhich.any()) _dout.setOutputs(dwhich,dvals);
    }
    catch(n_u::IOException& e) {
        n_u::Logger::getInstance()->log(LOG_ERR,"%s: %s\n",
            _dout.getName().c_str(),e.what());
    }
    return true;
}

void CVIProcessor::fromDOMElement(const xercesc::DOMElement* node)
{

    SampleIOProcessor::fromDOMElement(node);

    const std::list<const Parameter*>& params = getParameters();
    list<const Parameter*>::const_iterator pi;
    for (pi = params.begin(); pi != params.end(); ++pi) {
        const Parameter* param = *pi;
        const string& pname = param->getName();
        if (pname == "vout") {
                if (param->getLength() != 1)
                    throw n_u::InvalidParameterException(
                        SampleIOProcessor::getName(),"parameter",
                        "bad vout parameter");
                setD2ADeviceName(param->getStringValue(0));
        }
        else if (pname == "dout") {
                if (param->getLength() != 1)
                    throw n_u::InvalidParameterException(
                        SampleIOProcessor::getName(),"parameter",
                        "bad dout parameter");
                setDigIODeviceName(param->getStringValue(0));
        }
    }
}

