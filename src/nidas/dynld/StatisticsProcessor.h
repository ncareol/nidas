/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate$

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL$
 ********************************************************************
*/

#ifndef NIDAS_DYNLD_STATISTICSPROCESSOR_H
#define NIDAS_DYNLD_STATISTICSPROCESSOR_H

#include <nidas/core/SampleIOProcessor.h>
#include <nidas/dynld/StatisticsCruncher.h>
#include <nidas/util/UTime.h>

namespace nidas { namespace dynld {

using namespace nidas::core;

/**
 * Interface of a processor of samples. A StatisticsProcessor reads
 * input Samples from a single SampleInput, and sends its processed
 * output Samples to one or more SampleOutputs.  
 */
class StatisticsProcessor: public SampleIOProcessor
{
public:

    StatisticsProcessor();

    /**
     * Copy constructor.
     */
    StatisticsProcessor(const StatisticsProcessor&);

    ~StatisticsProcessor();

    StatisticsProcessor* clone() const;

    /**
     * Does this StatisticsProcessor only expect input samples from
     * one DSM?
     */
    bool singleDSM() const { return false; }

    void addSampleTag(SampleTag* tag)
	    throw(nidas::util::InvalidParameterException);
    /**
     * Do common operations necessary when a input has connected:
     * 1. Copy the DSMConfig information from the input to the
     *    disconnected outputs.
     * 2. Request connections for all disconnected outputs.
     *
     * connect() methods in subclasses should do whatever
     * initialization necessary before invoking this
     * StatisticsProcessor::connect().
     */
    void connect(SampleInput*) throw(nidas::util::IOException);

    /**
     * Disconnect a SampleInput from this StatisticsProcessor.
     * Right now just does a flush() of all connected outputs.
     */
    void disconnect(SampleInput*) throw(nidas::util::IOException);

    /**
     * Do common operations necessary when a output has connected:
     * 1. do: output->init().
     * 2. add output to a list of connected outputs.
     */
    virtual void connected(SampleOutput* orig,SampleOutput* output) throw();

    /**
     * Do common operations necessary when a output has disconnected:
     * 1. do: output->close().
     * 2. remove output from a list of connected outputs.
     */
    virtual void disconnected(SampleOutput* output) throw();

    void setStartTime(const nidas::util::UTime& val) 
    {
        startTime = val;
    }

    void setEndTime(const nidas::util::UTime& val) 
    {
        endTime = val;
    }

    float getPeriod() const 
    {
        return _statsPeriod;
    }

protected:

    std::list<StatisticsCruncher*> crunchers;

    struct OutputInfo {
        StatisticsCruncher::statisticsType type;
	std::string countsName;
        bool higherMoments;
    };

    std::map<dsm_sample_id_t,struct OutputInfo> infoBySampleId;

    std::list<SampleTag*> configTags;

    nidas::util::UTime startTime;

    nidas::util::UTime endTime;

    float _statsPeriod;

};

}}	// namespace nidas namespace core

#endif
