// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate: 2007-01-31 11:23:38 -0700 (Wed, 31 Jan 2007) $

    $LastChangedRevision: 3648 $

    $LastChangedBy: cjw $

    $HeadURL: http://svn/svn/nids/trunk/src/nidas/dynld/CVIProcessor.h $
 ********************************************************************
*/

#ifndef __nidas_dynld_raf_CVIProcessor_h
#define __nidas_dynld_raf_CVIProcessor_h

#include <memory>

#include <nidas/core/SampleIOProcessor.h>
#include <nidas/dynld/ViperDIO.h>
#include <nidas/dynld/DSC_AnalogOut.h>
#include <nidas/core/SampleAverager.h>

namespace nidas { namespace dynld { namespace raf {

using namespace nidas::core;

/**
 * Processor to support Counter-flow Virtual Impactor.
 */
class CVIProcessor: public SampleIOProcessor, public SampleClient
{
public:

    CVIProcessor();

    ~CVIProcessor();

    /**
     * Implementation of SampleSource and SampleClient flush().
     */
    void flush() throw();

    void addRequestedSampleTag(SampleTag* tag)
	    throw(nidas::util::InvalidParameterException);
    /**
     * Do common operations necessary when a input has connected:
     * 1. Copy the DSMConfig information from the input to the
     *    disconnected outputs.
     * 2. Request connections for all disconnected outputs.
     *
     * connect() methods in subclasses should do whatever
     * initialization necessary before invoking this
     * CVIProcessor::connect().
     */
    void connect(SampleSource*)
        throw(nidas::util::InvalidParameterException,nidas::util::IOException);

    /**
     * Disconnect a SampleSource from this CVIProcessor.
     * Calls flush() on the averager and closes the digital and analog outputs.
     */
    void disconnect(SampleSource*) throw();

    /**
     * Do common operations necessary when a output has connected:
     * 1. do: output->init().
     * 2. add output to a list of connected outputs.
     */
    void connect(SampleOutput* output) throw();

    /**
     * Do common operations necessary when a output has disconnected:
     * 1. do: output->close().
     * 2. remove output from a list of connected outputs.
     */
    void disconnect(SampleOutput* output) throw();

    void setD2ADeviceName(const std::string& val)
    {
        _d2aDeviceName = val;
    }

    const std::string& getD2ADeviceName() const
    {
        return _d2aDeviceName;
    }

    void setDigIODeviceName(const std::string& val)
    {
        _digioDeviceName = val;
    }

    const std::string& getDigIODeviceName()
    {
        return _digioDeviceName;
    }

    /**
     * The CVIProcessor receive method is configured to received the
     * processed samples from CVI_LV_Input. These samples contain settings
     * for the analog and digital out channels. receive() reads these
     * values and controls the outputs accordingly.
     */
    bool receive(const Sample*s) throw();

    void fromDOMElement(const xercesc::DOMElement* node)
        throw(nidas::util::InvalidParameterException);

protected:

    /**
     */
    void attachLVInput(SampleSource* src, const SampleTag* tag)
        throw(nidas::util::IOException);

private:

    nidas::util::Mutex _connectionMutex;

    std::set<SampleSource*> _connectedSources;

    std::set<SampleOutput*> _connectedOutputs;

    SampleTag* _outputSampleTag;

    std::string _d2aDeviceName;

    std::string _digioDeviceName;

    std::vector<bool> _varMatched;

    SampleAverager _averager;

    float _rate;

    dsm_sample_id_t _lvSampleId;

    DSC_AnalogOut _aout;

    ViperDIO _dout;

    unsigned int _numD2A;

    unsigned int _numDigout;

    float _vouts[5];

    int _douts[4];

    /**
     * Copy not supported
     */
    CVIProcessor(const CVIProcessor&);

    /**
     * Assignment not supported
     */
    CVIProcessor& operator=(const CVIProcessor&);

};

}}}	// namespace nidas namespace dynld namespace raf

#endif
