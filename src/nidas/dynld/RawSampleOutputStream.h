// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2005, Copyright University Corporation for Atmospheric Research
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

#ifndef NIDAS_DYNLD_RAWSAMPLEOUTPUTSTREAM_H
#define NIDAS_DYNLD_RAWSAMPLEOUTPUTSTREAM_H

#include "SampleOutputStream.h"

namespace nidas { namespace dynld {

class RawSampleOutputStream: public SampleOutputStream
{
public:

    RawSampleOutputStream();

    RawSampleOutputStream(IOChannel* iochan,SampleConnectionRequester* rqstr=0);

    virtual ~RawSampleOutputStream();

    bool isRaw() const { return true; }

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void fromDOMElement(const xercesc::DOMElement* node);

protected:

    RawSampleOutputStream* clone(IOChannel* iochannel);

    RawSampleOutputStream(RawSampleOutputStream&,IOChannel*);

private:

    RawSampleOutputStream(const RawSampleOutputStream&);

};

}}	// namespace nidas namespace core

#endif
