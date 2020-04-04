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
#ifndef NIDAS_DYNLD_A2DSENSOR_H
#define NIDAS_DYNLD_A2DSENSOR_H

#include <nidas/core/DSMSensor.h>

#include <nidas/linux/a2d.h>

#include <vector>
#include <map>
#include <set>


namespace nidas { namespace dynld {

extern int NIDAS_DYNLD_LINK_ME_DAMMIT;

using namespace nidas::core;

/**
 * One or more sensors connected to an A2D
 */
class A2DSensor : public DSMSensor {

public:

    A2DSensor();
    ~A2DSensor();

    /**
     * Open the device connected to the sensor.
     */
    void open(int flags) throw(nidas::util::IOException,
        nidas::util::InvalidParameterException);

    void init() throw(nidas::util::InvalidParameterException);

    /*
     * Close the device connected to the sensor.
     */
    void close() throw(nidas::util::IOException);

    void validate() throw(nidas::util::InvalidParameterException);

    void setScanRate(int val) { _scanRate = val; }

    int getScanRate() const { return _scanRate; }

    bool process(const Sample* insamp,std::list<const Sample*>& results) throw();

    /**
     * Return the maximum possible number of A2D channels
     * on this device.  Derived classes must implement
     * this method.
     */
    virtual int getMaxNumChannels() const = 0;

    /**
     * Set the sampling gain and polarity for a channel.
     * Derived classes are just responsible for throwing
     * nidas::util::InvalidParameterException in case of bad
     * values. If the values are OK, they should call base class
     * A2DSensor::setA2DParameters().
     * @param bipolar: 0=unipolar, 1=bipolar
     */
    virtual void setA2DParameters(int ichan,int gain,int bipolar)
        throw(nidas::util::InvalidParameterException);

    /**
     * Get the current gain and bipolar parameters for a channel.
     */
    virtual void getA2DParameters(int ichan,int& gain,int& bipolar) const;

    /**
     * Get the current gain for a channel.
     */
    int getGain(int ichan) const;

    /**
     * Get the current bipolar parameter for a channel.
     * @return 1: bipolar, 0: unipolar, -1: unknown
     */
    int getBipolar(int ichan) const;

    /**
     * Return the current linear conversion for a channel. The
     * A2D parameters of gain and bipolar should have already
     * been set. If they have not been set, derived classes should
     * return FloatNAN for those values.
     */
    virtual void getBasicConversion(int ichan,float& intercept, float& slope) const = 0;

    /**
     * Set the values for a linear correction to the basic conversion.
     * An intercept of 0. and a slope of 1. would result in no
     * additional correction.
     */
    virtual void setConversionCorrection(int ichan,float intercept,
        float slope) throw(nidas::util::InvalidParameterException);


    /**
     * Get the values for a linear correction to the basic conversion.
     */
    void getConversion(int ichan,float& intercept, float& slope) const;

    /**
     * Get the current conversion slope, which includes any 
     * correction as set by setConversionCorrection().
     */
    float getSlope(int ichan) const
    {
        if (ichan < 0 || ichan >= _maxNChannels) return floatNAN;
        return _convSlopes[ichan];
    }

    /**
     * Get the current conversion intercept, which includes any 
     * correction as set by setConversionCorrection().
     */
    float getIntercept(int ichan) const
    {
        if (ichan < 0 || ichan >= _maxNChannels) return floatNAN;
        return _convIntercepts[ichan];
    }

protected:

    /**
     * A2D configuration information that is sent to the A2D device module.
     * This is a C++ wrapper for struct nidas_a2d_sample_config
     * providing a virtual destructor. The filterData in this
     * configuration is empty, with a nFilterData of zero.
     */
    class A2DSampleConfig
    {
    public:
        A2DSampleConfig(): _cfg()  {}
        virtual ~A2DSampleConfig() {}
        virtual nidas_a2d_sample_config& cfg() { return _cfg; }
    private:
        nidas_a2d_sample_config _cfg;
        // No copying or assignment
        A2DSampleConfig(const A2DSampleConfig& x);
        A2DSampleConfig& operator=(const A2DSampleConfig& rhs);
    };

    /**
     * A2D configuration for box-car averaging of A2D samples.
     * filterData[] contains the number of samples in the average.
     */
    class A2DBoxcarConfig: public A2DSampleConfig
    {
    public:
        A2DBoxcarConfig(int n): A2DSampleConfig(),npts(n)
        {
            // make sure there is no padding or extra bytes
            // between the end of nidas_a2d_sample_config and npts.
            // The driver C code will interpret npts as filterData[].
            assert((void*)&(cfg().filterData[0]) == (void*)&npts);
            cfg().nFilterData = sizeof(int);
        }
        int npts;
    private:
        // No copying or assignment
        A2DBoxcarConfig(const A2DBoxcarConfig& x);
        A2DBoxcarConfig& operator=(const A2DBoxcarConfig& rhs);
    };

    /**
     * A2D configuration for time-based averaging of A2D samples.
     * filterData[] contains the desired output rate.
     */
    class A2DTimeAvgConfig: public A2DSampleConfig
    {
    public:
        A2DTimeAvgConfig(int n): A2DSampleConfig(),rate(n)
        {
            // make sure there is no padding or extra bytes
            // between the end of nidas_a2d_sample_config and npts.
            // The driver C code will interpret npts as filterData[].
            assert((void*)&(cfg().filterData[0]) == (void*)&rate);
            cfg().nFilterData = sizeof(int);
        }
        int rate;
    private:
        // No copying or assignment
        A2DTimeAvgConfig(const A2DTimeAvgConfig& x);
        A2DTimeAvgConfig& operator=(const A2DTimeAvgConfig& rhs);
    };

    std::vector<A2DSampleConfig*> _sampleCfgs;

    /**
     * Information needed to intepret the samples that are
     * received from the A2D device.
     */
    class A2DSampleInfo
    {
    public:
        A2DSampleInfo(int n)
            : nvars(n),nvalues(0),stag(0),channels(nvars) {}
        A2DSampleInfo(const A2DSampleInfo& x): nvars(x.nvars),nvalues(x.nvalues),
            stag(x.stag),channels(x.channels)
        {
        }
        A2DSampleInfo& operator= (const A2DSampleInfo& rhs)
        {
            if (&rhs != this) {
                nvars = rhs.nvars;
                nvalues = rhs.nvalues;
                stag = rhs.stag;
                channels = rhs.channels;
            }
            return *this;
        }

        ~A2DSampleInfo() { }
        int nvars;
        int nvalues;
        SampleTag* stag;
        std::vector<int> channels;
    };

    std::vector<A2DSampleInfo> _sampleInfos;

    /**
     * Counter of number of raw samples of wrong size.
     */
    size_t _badRawSamples;

protected:
    void initParameters();

    int _maxNChannels;

    /**
     * Conversion factor for each channel when converting from A2D
     * counts to voltage.
     * The gain is accounted for in this conversion, so that
     * the resultant voltage value is an estimate of the actual
     * input voltage, before any A2D gain was applied.
     */
    float* _convSlopes;

    /**
     * Conversion offset for each A2D channel when converting from A2D
     * counts to voltage.
     * The polarity is accounted for in this conversion, so that
     * the resultant voltage value should be the actual input voltage.
     */
    float* _convIntercepts;

private:
    /**
     * Requested A2D sample rate before decimation.
     */
    int _scanRate;

    int _prevChan;

    int* _gains;

    int* _bipolars;

    /** No copying */
    A2DSensor(const A2DSensor&);

    /** No assignment */
    A2DSensor& operator=(const A2DSensor&);
};

}}	// namespace nidas namespace dynld

#endif
