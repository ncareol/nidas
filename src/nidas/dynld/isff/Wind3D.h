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

#ifndef NIDAS_DNYLD_ISFF_WIND3D_H
#define NIDAS_DNYLD_ISFF_WIND3D_H

#include <nidas/core/SerialSensor.h>
#include <nidas/core/AdaptiveDespiker.h>
#include <nidas/Config.h>
#include "WindOrienter.h"
#include "WindTilter.h"
#include "WindRotator.h"
#include <memory>

namespace nidas {

namespace dynld { namespace isff {


class Wind3D_impl;

/**
 * A class for performing the common processes on
 * wind data from a 3D sonic anemometer.
 */
class Wind3D: public nidas::core::SerialSensor
{
public:

    Wind3D();

    ~Wind3D();

    /**
     * Basic process method for sonic anemometer wind plus temperature:
     * u,v,w,tc parsed from an ASCII sample. Applies despiking,
     * orientation corrections, bias, tilts and horizontal rotations, as configured.
     */
    bool process(const nidas::core::Sample* samp, 
        std::list<const nidas::core::Sample*>& results);

    void setBias(int i, double val);

    double getBias(int i) const
    {
        return _bias[i];
    }

    double getVazimuth() const
    {
	return _rotator.getAngleDegrees();
    }

    /**
     * Wind vectors in geographic coordinates are expressed
     * by U, the component of the wind blowing toward the east,
     * and V, the component of the wind blowing toward the north.
     * If the V axis of a sonic anemometer is pointing
     * north then no rotation is necessary to convert from
     * sonic coordinates to geographic.  So the compass azimuth
     * (0=north,90=east, etc) of the sonic +V axis is the
     * angle between geographic and sonic coordinates.
     */
    void setVazimuth(double val)
    {
	_rotator.setAngleDegrees(val);
    }

    double getLeanDegrees() const
    {
	return _tilter.getLeanDegrees();
    }
    void setLeanDegrees(double val)
    {
	_tilter.setLeanDegrees(val);
    }

    double getLeanAzimuthDegrees() const
    {
	return _tilter.getLeanAzimuthDegrees();
    }
    void setLeanAzimuthDegrees(double val)
    {
	_tilter.setLeanAzimuthDegrees(val);
    }

    void setDespike(bool val)
    {
        _despike = val;
    }

    bool getDespike() const
    {
        return _despike;
    }

    void setMetek(int ismetek) {
        _metek = (ismetek == 1);
    }

    void setOutlierProbability(double val)
    {
	for (int i = 0; i < 4; i++)
	    _despiker[i].setOutlierProbability(val);
    }

    double getOutlierProbability() const
    {
        return _despiker[0].getOutlierProbability();
    }

    void setDiscLevelMultiplier(double val)
    {
	for (int i = 0; i < 4; i++)
	    _despiker[i].setDiscLevelMultiplier(val);
    }

    double getDiscLevelMultiplier() const
    {
        return _despiker[0].getDiscLevelMultiplier();
    }

    double getDiscLevel() const
    {
        return _despiker[0].getDiscLevel();
    }

    void setTcOffset(double val) 
    {
        _tcOffset = val;
    }

    double getTcOffset() const
    {
        return _tcOffset;
    }

    void setTcSlope(double val) 
    {
        _tcSlope = val;
    }

    double getTcSlope() const
    {
        return _tcSlope;
    }

    /**
     * Should 2D horizontal rotations of U,V be applied?
     */
    void setDoHorizontalRotation(bool val)
    {
        _horizontalRotation = val;
    }
        
    /**
     * Should 3D tilt corrections be applied?
     */
    void setDoTiltCorrection(bool val)
    {
        _tiltCorrection = val;
    }

    void despike(nidas::core::dsm_time_t tt,float* uvwt,int n, bool* spikeOrMissing)
    	throw();

    /**
     * Do standard bias removal, tilt correction and horizontal rotation of
     * 3d sonic anemometer data.
     *
     * @tt time tag of the data, used to search for a parameter
     *    a file containing a calibration time series.
     * @param uvwt Pointer to an array of 4 floats, containing
     *    u,v,w and tc(virtual temperature). New values
     *    are written back via the pointers.
     */
    void offsetsTiltAndRotate(nidas::core::dsm_time_t tt, float* uvwt) throw();

    /**
     * Apply orientation changes to the wind components.
     **/
    void applyOrientation(nidas::core::dsm_time_t tt, float* uvwt) throw();

    /**
     * Update the settings from the offsets and angles calibration file, if
     * any.
     **/
    void readOffsetsAnglesCalFile(nidas::core::dsm_time_t tt) throw();

    /**
     * Validate the configuration of this sensor. Calls the base class
     * validate(), parseParameters(), and checkSampleTags().
     */
    void validate();

    /**
     * Warn user if number of scanf fields does not match
     * number expected from variables in sample.
     */
    void validateSscanfs();

    /**
     * Parse the list of nidas::core::Parameter that are associated with this sensor.
     * This also checks the parameters "wind3d_tilt_correction",
     * and "wind3d_horiz_rotation" that may have been set on the Project
     * singleton, calls setDoTiltCorrection() and setDoHorizontalRotation()
     * accordingly.
     */
    virtual void parseParameters();

    /**
     * Check the SampleTags that are defined for this sensor.
     */
    virtual void checkSampleTags();

    /**
     * Read 3x3 matrix to be used for the transformation of wind vectors in ABC
     * transducer coordinates to orthoganal UVW coordinates. These values are typically
     * in a CalFile.
     */
    virtual void getTransducerRotation(nidas::core::dsm_time_t tt);

    virtual void transducerShadowCorrection(nidas::core::dsm_time_t, float *);

protected:

    typedef nidas::dynld::isff::WindOrienter WindOrienter;
    typedef nidas::dynld::isff::WindRotator WindRotator;
    typedef nidas::dynld::isff::WindTilter WindTilter;

    static const int DATA_GAP_USEC = 60000000;

    nidas::core::dsm_time_t _ttlast[4];

    double _bias[3];

    bool _allBiasesNaN;

    bool _despike;
    
    bool _metek;

    nidas::core::AdaptiveDespiker _despiker[4];
 
    WindRotator _rotator;

    WindTilter _tilter;

    WindOrienter _orienter;

    double _tcOffset;

    double _tcSlope;

    /**
     * Should horizontal rotation of U,V be performed?
     */
    bool _horizontalRotation;

    /**
     * Should 3D tilt correction be applied?
     */
    bool _tiltCorrection;

    /**
     * Id of output sample.
     */
    nidas::core::dsm_sample_id_t _sampleId;

    /**
     * If user requests "diag" or "status", its index
     * in the output sample.
     */
    int _diagIndex;

    /**
     * If user requests "ldiag", its index
     * in the output sample.
     */
    int _ldiagIndex;

    /**
     * If user requests wind speed, variable name "spd", its index
     * in the output sample.
     */
    int _spdIndex;

    /**
     * If user requests wind direction, variable name "dir", its index
     * in the output sample.
     */
    int _dirIndex;

    unsigned int _noutVals;

    /**
     * Number of variables that are parsed from input, i.e.
     * not derived.
     */
    unsigned int _numParsed;

    /**
     * CalFile containing wind offsets and rotation angles.
     */
    nidas::core::CalFile* _oaCalFile;

    /**
     * CalFile containing the transducer geometry matrix for rotation
     * to transducer coordinates, which is necessary for transducer
     * shadowing correction.
     */
    nidas::core::CalFile* _atCalFile;

    /**
     * Axes transformation matrix, from non-orthogonal ABC to orthogonal UVW coordinates.
     */
    double _atMatrix[3][3];
    double _atInverse[3][3];

    /**
     * Transducer shadow (aka flow distortion) correction factor.
     * This value can be set in the XML with a sensor parameter called
     * "shadowFactor".
     */
    double _shadowFactor;

    std::unique_ptr<Wind3D_impl> _impl;

private:

    // no copying
    Wind3D(const Wind3D& x);

    // no assignment
    Wind3D& operator=(const Wind3D& x);
    
};

}}}	// namespace nidas namespace dynld namespace isff

#endif
