// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/* SidsNetSensor.h
 *
 * Copyright 2012 UCAR, NCAR, All Rights Reserved
 * Revisions:
 * $LastChangedRevision: $
 * $LastChangedDate:  $
 * $LastChangedBy: cjw $
 * $HeadURL: http://svn/svn/nidas/trunk/src/nidas/dynid/SidsNetSensor.h $
 */


#ifndef NIDAS_DYNLD_RAF_SIDSNETSENSOR_H
#define NIDAS_DYNLD_RAF_SIDSNETSENSOR_H

#include <iostream>
#include <iomanip>

#include <nidas/dynld/UDPSocketSensor.h>
#include <nidas/util/EndianConverter.h>

#include <nidas/util/InvalidParameterException.h>

namespace nidas { namespace dynld { namespace raf {

using namespace nidas::core;

/**
 * Sensor class supporting the NCAR/EOL SID2H modifications via Ethernet
 * UDP connection.  Nicknamed SIDS.  This sensor will send particle by
 * particle information blocked up 100 particles at a time.  Data will
 * consist of 40 bit 12MHz clock counter, 8 bit height, 16 bit length,
 * 8 bit rejected particle counter, and an 8 bit sync word (0x55).
 *
 * We will accumulate the data and produce a one second histogram and a
 * total rejected per second.
 */
class SidsNetSensor : public CharacterSensor
{
public:
    static const int SIDS_HISTOGRAM_SIZE = 70;
    static const unsigned char SIDS_SYNC_WORD = 0x55;

    SidsNetSensor();
    ~SidsNetSensor();

    bool process(const Sample *samp,std::list<const Sample *>& results)
        throw();

    /**
     * Returns histogram size.
     */
    virtual int NumberOfDiodes() const { return SIDS_HISTOGRAM_SIZE; }


protected:
    class Particle
    {
    public:
        Particle() : height(0), width(0), area(0) { } ;
        void zero() { height = width = area = 0; }

        /// Max particle height, along diode array.
        unsigned int height;
        /// Max particle length, along flight path.
        unsigned int width;
        /**
         * Actual number of shadowed diodes.  This can be misleading if there
         * are holes in the particle, poisson spot, etc.
         */
        unsigned int area;
    } ;


    /**
     * Called by post-processing code 
     */
    void init() throw(nidas::util::InvalidParameterException);

    /**
     * Look at particle stats/info and decide whether to accept or reject.
     * @param p is the particle information.
     */
    virtual void countParticle(const Particle& p);

    /**
     * Accept/reject criteria are in these functions.
     * @param p is particle info class.
     * @returns boolean whether the particle should be rejected.
     */
    virtual bool acceptThisParticle1D(const Particle& p) const;

//@{
    /**
     * Send derived data and reset.  The process() method for image data is
     * to build size-distribution histograms for 1 second of data and then
     * send that.
     * @param results is the output results.
     * @param nextTimeTag is the timetag of a sample that is past
     *  the end of the current histogram period.  The end
     */
    virtual void createSamples(dsm_time_t nextTimeTag,std::list<const Sample *>&results) throw();

    /**
     * Clear size_dist arrays.
     */
    virtual void clearData();
//@}

    /**
     * Array for size-distribution histograms.
     */
    unsigned int *_size_dist_1D;

    /// Total rejected particles from probe per second.
    unsigned int _rejected;

    /**
     * Time from previous record.  Time belongs to end of record it came with,
     * or start of the next record.  Save it so we can use it as a start.
     */
    dsm_time_t _prevTime;

//@{
    /**
     * Statistics variables for processRecord().
     */
    unsigned int _totalRecords;
    unsigned int _totalParticles;
    unsigned int _rejected1D_Cntr,
    unsigned int _overSizeCount;
    unsigned int _misAligned;
    unsigned int _recordsPerSecond;
//@}

    /// The end time of the current histogram.
    long long _histoEndTime;

    /**
     * Number of output values excluding histogram.
     * Currently just number rejects per seocnd.
     */
    const int _nextraValues;

    static const nidas::util::EndianConverter *_fromLittle;


private:
    /** No copying. */
    SidsNetSensor(const SidsNetSensor&);

    /** No assignment. */
    SidsNetSensor& operator=(const SidsNetSensor&);
};

}}}

#endif
