// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*-
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

#ifndef NIDAS_DYNLD_STATISTICSCRUNCHER_H
#define NIDAS_DYNLD_STATISTICSCRUNCHER_H

#include <nidas/core/SampleSource.h>
#include <nidas/core/SampleClient.h>
#include <nidas/core/SamplePipeline.h>
#include <nidas/core/NearestResampler.h>
#include <nidas/util/UTime.h>

#include <vector>
#include <algorithm>

namespace nidas { namespace dynld {

class StatisticsProcessor;

using namespace nidas::core;

/**
 */
class StatisticsCruncher : public Resampler
{
public:

    /**
     * Types of statistics I can generate.
     */
    typedef enum statsEnumType {
        STATS_UNKNOWN,STATS_MINIMUM,STATS_MAXIMUM,STATS_MEAN,STATS_VAR,
	STATS_COV, STATS_FLUX, STATS_RFLUX,STATS_SFLUX,
        STATS_TRIVAR,STATS_PRUNEDTRIVAR, STATS_WINDDIR, STATS_SUM
    } statisticsType;

    /**
     * Constructor.
     */
    StatisticsCruncher(StatisticsProcessor* proc,
        const SampleTag* stag,statisticsType type,
    	std::string countsName,bool higherMoments);

    ~StatisticsCruncher();

    SampleSource* getRawSampleSource() { return 0; }

    SampleSource* getProcessedSampleSource() { return &_source; }


    /**
     * Get the output SampleTags.
     */
    std::list<const SampleTag*> getSampleTags() const
    {
        return _source.getSampleTags();
    }

    /**
     * Implementation of SampleSource::getSampleTagIterator().
     */
    SampleTagIterator getSampleTagIterator() const
    {
        return _source.getSampleTagIterator();
    }

    /**
     * Implementation of SampleSource::addSampleClient().
     */
    void addSampleClient(SampleClient* client) throw()
    {
        _source.addSampleClient(client);
    }

    void removeSampleClient(SampleClient* client) throw()
    {
        _source.removeSampleClient(client);
    }

    /**
     * Add a Client for a given SampleTag.
     * Implementation of SampleSource::addSampleClient().
     */
    void addSampleClientForTag(SampleClient* client,const SampleTag*) throw()
    {
        // I only have one tag, so just call addSampleClient()
        _source.addSampleClient(client);
    }

    void removeSampleClientForTag(SampleClient* client,const SampleTag*) throw()
    {
        _source.removeSampleClient(client);
    }

    int getClientCount() const throw()
    {
        return _source.getClientCount();
    }

    /**
     * Implementation of Resampler::flush().
     */
    void flush() throw();

    const SampleStats& getSampleStats() const
    {
        return _source.getSampleStats();
    }

    bool receive(const Sample *s) throw();

    /**
     * Connect a SamplePipeline to the cruncher.
     *
     * @throws nidas::util::InvalidParameterException
     **/
    void connect(SampleSource* source);

    void disconnect(SampleSource* source) throw();

    /**
     * Connect a SamplePipeline to the cruncher.
     */
    void connect(SampleOutput* output);

    void disconnect(SampleOutput* output);

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    static statisticsType getStatisticsType(const std::string& type);

    /**
     * Return string name for the given @p stype.
     * 
     * If @p stype does not match a known name, return "none".
     */
    static const std::string& getStatisticsString(statisticsType stype);

    void setStartTime(const nidas::util::UTime& val);

    nidas::util::UTime getStartTime() const
    {
        return _startTime;
    }

    void setEndTime(const nidas::util::UTime& val) 
    {
        _endTime = val;
    }

    nidas::util::UTime getEndTime() const
    {
        return _endTime;
    }

    long long getPeriodUsecs() const 
    {
        return _periodUsecs;
    }

    /**
     * Whether to generate output samples over time gaps.
     * In some circumstances one might be generating statistics
     * for separate time periods, and one does not want
     * to output samples of missing data for the gaps between
     * the periods.
     */
    bool getFillGaps() const 
    {
        return _fillGaps;
    }

    void setFillGaps(bool val)
    {
        _fillGaps = val;
    }

protected:

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void attach(SampleSource* source);

    /**
     * Split input variable names at periods.
     */
    void splitNames();

    /**
     * Create a derived variable name from input variables i,j,k,l.
     * Specify j,k,l for 2nd,3rd,4th order products.
     */
    std::string makeName(int i, int j=-1, int k=-1, int l=-1);

    /**
     * Create a derived units field from input variables i,j,k,l.
     * Specify j,k,l for 2nd,3rd,4th order products.
     */
    std::string makeUnits(int i, int j=-1, int k=-1, int l=-1);

    std::string makeUnits(const std::vector<std::string>&);

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void createCombinations();

    void setupMoments(unsigned int nvars, unsigned int moment);

    void setupMinMax(const std::string&);

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void setupWindDir();

    void setupCovariances();

    void setupTrivariances();

    void setupPrunedTrivariances();

    void setupFluxes();

    void setupReducedFluxes();

    void setupReducedScalarFluxes();

    void initStats();

    void zeroStats();

    void computeStats();

    void
    addVariable(const std::string& name,
                const std::string& longname,
                const std::string& units);
private:

    /**
     * Add a SampleTag to this SampleSource. This SampleSource
     * does not own the SampleTag.
     */
    void addSampleTag(const SampleTag* tag) throw ()
    {
        _source.addSampleTag(tag);
    }

    void removeSampleTag(const SampleTag* tag) throw ()
    {
        _source.removeSampleTag(tag);
    }

    StatisticsProcessor* _proc;

    SampleSourceSupport _source;

    SampleTag _reqTag;

    std::vector<const Variable*> _reqVariables;

    /**
     * Number of input variables.
     */
    unsigned int _ninvars;

    /**
     * Name of counts variable.
     */
    std::string _countsName;

    /**
     * Does the user want number-of-points output?
     */
    bool _numpoints;

    dsm_time_t _periodUsecs;

    /**
     * Does this cruncher compute cross-terms?
     */
    bool _crossTerms;

    NearestResampler* _resampler;

    /**
     * Types of statistics I can generate.
     */
    statisticsType _statsType;

    /**
     * Input variable names, split at dots.
     */
    std::vector<std::vector<std::string> > _splitVarNames;

    /**
     * Portion after the first dot-separated word that
     * is common to all variable names.
     */
    std::string _leadCommon;

    /**
     * Trailing portion that is common to all variable names.
     */
    std::string _commonSuffix;

    SampleTag _outSample;

    unsigned int _nOutVar;

    unsigned int _outlen;

    dsm_time_t _tout;

    struct sampleInfo {
        sampleInfo(): weightsIndex(0),varIndices() {}
        unsigned int weightsIndex;
	std::vector<unsigned int*> varIndices;
    };

    std::map<dsm_sample_id_t,sampleInfo > _sampleMap;

    float* _xMin;

    float* _xMax;

    // statistics sums.
    double* _xSum;

    double** _xySum;
    double* _xyzSum;
    double* _x4Sum;

    unsigned int *_nSamples;

    unsigned int **_triComb;

    /**
     * Number of simple sums to maintain
     */
    unsigned int _nsum;

    /**
     * Number of covariances to compute.
     */
    unsigned int _ncov;

    /**
     * Number of trivariances to compute.
     */
    unsigned int _ntri;

    /**
     * Number of 1st,2nd,3rd,4th moments to compute.
     */
    unsigned int _n1mom,_n2mom,_n3mom,_n4mom;

    /**
     * Total number of products to compute.
     */
    unsigned int _ntot;

    bool _higherMoments;

    const Site* _site;

    int _station;

    nidas::util::UTime _startTime;

    nidas::util::UTime _endTime;

    bool _fillGaps;

    /** No copy.  */
    StatisticsCruncher(const StatisticsCruncher&);

    /** No assignment.  */
    StatisticsCruncher& operator=(const StatisticsCruncher&);

};

}}	// namespace nidas namespace core

#endif
