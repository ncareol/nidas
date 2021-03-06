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

#ifndef NIDAS_DYNLD_ISFF_PACKETINPUTSTREAM_H
#define NIDAS_DYNLD_ISFF_PACKETINPUTSTREAM_H

#include "Packets.h"
#include <nidas/dynld/SampleInputStream.h>

namespace nidas {

namespace core {
class Sample;
class SampleTag;
class IOChannel;
class IOStream;
}

namespace dynld { namespace isff {

class GOESProject;

class PacketInputStream: public nidas::dynld::SampleInputStream
{

public:

    /**
     * Constructor.
     * @param iochannel The IOChannel that we use for data input.
     *   SampleInputStream will own the pointer to the IOChannel,
     *   and will delete it in ~SampleInputStream(). If 
     *   it is a null pointer, then it must be set within
     *   the fromDOMElement method.
     */
    PacketInputStream(nidas::core::IOChannel* iochannel = 0)
	    throw(nidas::util::InvalidParameterException);

    /**
     * Copy constructor, with a new, connected IOChannel.
     */
    PacketInputStream(const PacketInputStream& x,nidas::core::IOChannel* iochannel);

    /**
     * Create a clone, with a new, connected IOChannel.
     */
    virtual PacketInputStream* clone(nidas::core::IOChannel* iochannel);

    virtual ~PacketInputStream();

    std::string getName() const;

    std::list<const nidas::core::SampleTag*> getSampleTags() const;

    void init() throw();

    /**
     * Read the next sample from the InputStream. The caller must
     * call freeReference on the sample when they're done with it.
     * This method may perform zero or more reads of the IOChannel.
     */
    nidas::core::Sample* readSample() throw(nidas::util::IOException)
    {
        throw nidas::util::IOException(getName(),"readSample","not supported");
    }

    /**
     * Read a buffer of data, serialize the data into samples,
     * and distribute() samples to the receive() method of my
     * SampleClients and DSMSensors.
     * This will perform only one physical read of the underlying
     * IOChannel and so is appropriate to use when a select()
     * has determined that there is data available on our file
     * descriptor.
     */
    bool readSamples() throw(nidas::util::IOException);

    void close() throw(nidas::util::IOException);

    /** 
     * Implementation of SampleSource::flush().
     */
    void flush() throw() {}

private:

    const nidas::core::SampleTag* findSampleTag(int configId, int goesId, int sampleId)
	    throw(nidas::util::InvalidParameterException);

    const GOESProject* getGOESProject(int configid) const
    	throw(nidas::util::InvalidParameterException);

    nidas::core::IOChannel* _iochan;

    nidas::core::IOStream* _iostream;

    PacketParser* _packetParser;

    mutable std::map<int,GOESProject*> _projectsByConfigId;

    /**
     * No copy.
     */
    PacketInputStream(const PacketInputStream&);

    /**
     * No assignment.
     */
    PacketInputStream& operator=(const PacketInputStream&);

};

}}}	// namespace nidas namespace dynld namespace isff

#endif
