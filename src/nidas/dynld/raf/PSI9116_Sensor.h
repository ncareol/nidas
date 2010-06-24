/*
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate$

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL$

*/

#ifndef NIDAS_DYNLD_RAF_PSI9116_SENSOR_H
#define NIDAS_DYNLD_RAF_PSI9116_SENSOR_H

#include <nidas/core/CharacterSensor.h>

namespace nidas { namespace dynld { namespace raf {

using namespace nidas::core;

/**
 * Support for sampling a PSI 9116 pressure scanner from EsterLine
 * Pressure Systems.  This is a networked sensor, accepting connections
 * on TCP port 9000 and capable of receiving UDP broadcasts on port 7000.
 * Currently this class does not use UDP.
 */
class PSI9116_Sensor: public CharacterSensor
{

public:

    PSI9116_Sensor();

    ~PSI9116_Sensor() { }

    IODevice* buildIODevice() throw(nidas::util::IOException);

    void open(int flags)
    	throw(nidas::util::IOException,nidas::util::InvalidParameterException);

    void addSampleTag(SampleTag* stag)
            throw(nidas::util::InvalidParameterException);

    bool process(const Sample* samp,std::list<const Sample*>& results)
    	throw();

    /**
     * Stop data streams, set valve position to PURGE.
     */
    void startPurge() throw(nidas::util::IOException);

    /**
     * Set valve position back to RUN from PURGE,
     * then restart data streams.
     */
    void stopPurge() throw(nidas::util::IOException);

    void startStreams() throw(nidas::util::IOException);

    void stopStreams() throw(nidas::util::IOException);

    void executeXmlRpc(XmlRpc::XmlRpcValue& params, XmlRpc::XmlRpcValue& result) throw();

protected:

    std::string sendCommand(const std::string& cmd,int readlen = 0)
    	throw(nidas::util::IOException);

    int _msecPeriod;

    /**
     * Number of sampled channels.
     */
    int _nchannels;

    dsm_sample_id_t _sampleId;

    /**
     * Conversion factor to apply to PSI data. 
     * PSI9116 by default reports data in psi.
     * A factor 68.94757 will convert to millibars.
     */
    float _psiConvert;

    unsigned int _sequenceNumber;

    size_t _outOfSequence;

};

}}}	// namespace nidas namespace dynld namespace raf

#endif
