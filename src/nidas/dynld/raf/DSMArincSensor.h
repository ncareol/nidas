// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2004, Copyright University Corporation for Atmospheric Research
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
#ifndef NIDAS_DYNLD_RAF_DSMARINCSENSOR_H
#define NIDAS_DYNLD_RAF_DSMARINCSENSOR_H

#include <nidas/linux/arinc/arinc.h>
#include <nidas/core/DSMSensor.h>
#include <nidas/core/TimetagAdjuster.h>
#include <nidas/core/VariableConverter.h>
#include <nidas/util/InvalidParameterException.h>

// Significant bits masks
//
// 32|31 30|29 28 27 26 25 24 23 22 21 20 19 18 17 16 15 14 13 12 11|10  9| 8  7  6  5  4  3  2  1
// --+-----+--------------------------------------------------------+-----+-----------------------
// P | SSM |                                                        | SDI |      8-bit label

// bitmask for the Sign Status Matrix
#define SSM 0x60000000
#define NCD 0x20000000
#define TST 0x40000000

#define NLABELS 256

namespace nidas { namespace dynld { namespace raf {

using namespace nidas::core;

// inHg to mBar
const float INHG_MBAR  = 33.8639;

// NM to meter.
const float NM_MTR  = 1.0 / 1852.0;

// ft to meter.
const float FT_MTR  = 0.3048;

// G to m/s2 (ACINS).
const float G_MPS2   = 9.7959;

// knot to m/s
const float KTS_MS = 0.514791;

// ft/min to m/s (VSPD)
const float FPM_MPS  = 0.00508;

// radian to degree.
const float RAD_DEG = 180.0 / 3.14159265358979;


/**
 * This is sorts a list of Sample tags by rate (highest first)
 * then by label.
 */
class SortByRateThenLabel {
public:
    bool operator() (const SampleTag* x, const SampleTag* y) const {
        if ( x->getRate() > y->getRate() ) return true;
        if ( x->getRate() < y->getRate() ) return false;
        if ( x->getId()   < y->getId()   ) return true;
        return false;
    }
};

/**
 * A sensor connected to an ARINC port.
 */
class DSMArincSensor : public DSMSensor
{

public:

    /**
     * No arg constructor.  Typically the device name and other
     * attributes must be set before the sensor device is opened.
     */
    DSMArincSensor();
    ~DSMArincSensor();

    /**
     * @throws nidas::util::IOException
     **/
    IODevice* buildIODevice();

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    SampleScanner* buildSampleScanner();

    /** 
     * This opens the associated device.
     *
     * @throws nidas::util::IOException
     * @throws nidas::util::InvalidParameterException
     */
    void open(int flags);

    /** 
     * This closes the associated device.
     *
     * @throws nidas::util::IOException
     */
    void close();

    /**
     * Perform any initialization necessary for process method.
     *
     * @throws nidas::util::InvalidParameterException
     */
    void init();

    /** 
     * Process a raw sample, which in this case means create a list of
     * samples with each sample containing a timetag.
     */
    bool process(const Sample*, std::list<const Sample*>& result);

    virtual bool processAlta(const dsm_time_t, unsigned char *, int, std::list<const Sample*> &result);

    /** Display some status information gathered by the driver. */
    void printStatus(std::ostream& ostr);

    /** 
     * This contains a switch case for processing all labels.
     */
    virtual double processLabel(const int data, sampleType* stype) = 0;

    /**
     * Extract the ARINC configuration elements from the XML header.
     *
     * Example XML:
     * @code
     * <arincSensor ID="GPS-GV" class="GPS_HW_HG2021GB02" speed="low" parity="odd">
     * @endcode
     *
     * @throws nidas::util::InvalidParameterException
     */
    void fromDOMElement(const xercesc::DOMElement*);

    int getInt32TimeTagUsecs() const
    {
        return USECS_PER_MSEC;
    }

    unsigned int Speed()    { return _speed; }


protected:
    /**
     * If this is the Alta UDP setup, then register with that sensor.
     */
    void registerWithUDPArincSensor();

    /// A list of which samples are processed.
    int _processed[NLABELS];
    int _observedLabelCnt[NLABELS];

    bool _altaEnetDevice;

private:

    /** channel configuration */
    unsigned int _speed;
    unsigned int _parity;

    std::map<dsm_sample_id_t,VariableConverter*> _converters;

    std::map<dsm_sample_id_t, TimetagAdjuster*> _ttadjusters;

};

// typedef SampleT<unsigned int> ArincSample;

}}}	// namespace nidas namespace dynld namespace raf

#endif
