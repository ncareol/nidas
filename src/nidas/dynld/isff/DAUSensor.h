// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2017, Copyright University Corporation for Atmospheric Research
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

#ifndef NIDAS_DYNLD_ISFF_DAUSENSOR_H
#define NIDAS_DYNLD_ISFF_DAUSENSOR_H

#include <nidas/core/SerialSensor.h>
#include <nidas/util/EndianConverter.h>

namespace nidas { namespace dynld { namespace isff {

using namespace nidas::core;

class DAUSensor: public nidas::core::SerialSensor
{

public:

    DAUSensor();

    ~DAUSensor();
    
    void init();

    void
    addSampleTag(SampleTag* stag);

    bool
    process(const Sample* samp,std::list<const Sample*>& results);

    void
    fromDOMElement(const xercesc::DOMElement* node);

protected:
    const nidas::util::EndianConverter* _cvtr;

private:
    dsm_time_t _prevTimeTag;
        
    //vector to hold prev. message
    std::vector<unsigned char> _prevData;
    
    int _prevOffset;

    DAUSensor(const DAUSensor&);
    DAUSensor& operator=(const DAUSensor&);

};

}}}	// namespace nidas namespace dynld namespace isff

#endif // NIDAS_DYNLD_ISFF_TILTSENSOR_H
