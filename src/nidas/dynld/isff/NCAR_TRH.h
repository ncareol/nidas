// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2014, Copyright University Corporation for Atmospheric Research
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

#ifndef NIDAS_DYNLD_ISFF_NCAR_TRH_H
#define NIDAS_DYNLD_ISFF_NCAR_TRH_H

#include <vector>

#include <nidas/core/SerialSensor.h>
#include <nidas/core/VariableConverter.h>

namespace nidas { namespace dynld { namespace isff {

/**
 * Sensor class for the NCAR hygrothermometer, built at EOL.
 * This hygrothermometer has an electrical aspiration fan, and reports
 * the current used by the fan in its output message, along with the values
 * for temperature and relativef humidity. If the fan current is outside
 * an acceptable range, as specified by the getMinValue(),getMaxValue()
 * settings of the Ifan variable, then any value other than the fan current
 * is set to the missing value. The idea is to discard temperature and relative
 * humidity when aspiration is not sufficient.
 */
class NCAR_TRH: public nidas::core::SerialSensor
{
public:

    NCAR_TRH();

    ~NCAR_TRH();

    void validate() throw(nidas::util::InvalidParameterException);

    bool process(const nidas::core::Sample* samp,
                 std::list<const nidas::core::Sample*>& results) throw();

    void
    ifanFilter(std::list<const nidas::core::Sample*>& results);

    double
    tempFromRaw(double traw);

    double
    rhFromRaw(double rhraw, double temp_cal);
    
private:

    /**
     * In the validate() method the variables generated by this sensor
     * class are scanned, and if one matches "Ifan" then its index, min and
     * max values are copied to these class members. The min, max limit
     * checks for the Ifan variable are then removed (actually expanded to
     * the limits for a float value), so that the Ifan variable is itself
     * not overwritten with floatNAN if it exceeds the limit checks, but
     * the T and RH values are.
     */
    unsigned int _ifanIndex;

    float _minIfan;

    float _maxIfan;

    /**
     * Coefficients for calculating T and RH from the raw counts.
     **/
    std::vector<float> _Ta;
    std::vector<float> _Ha;

    // no copying
    NCAR_TRH(const NCAR_TRH& x);

    // no assignment
    NCAR_TRH& operator=(const NCAR_TRH& x);


};

}}} // namespace nidas namespace dynld namespace isff

#endif
