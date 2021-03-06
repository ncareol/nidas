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

#include <nidas/Config.h> 

#ifdef HAVE_LIBNC_SERVER_RPC

#ifndef NIDAS_DYNLD_ISFF_NETCDFRPCOUTPUT_H
#define NIDAS_DYNLD_ISFF_NETCDFRPCOUTPUT_H

#include <nidas/core/SampleOutput.h>
#include <nidas/util/UTime.h>

namespace nidas { namespace dynld { namespace isff {

using namespace nidas::core;

class NetcdfRPCChannel;

/**
 * A perversion of a simple IOChannel.  This sends data
 * to a NetCDF via RPC calls.
 */
class NetcdfRPCOutput: public SampleOutputBase {

public:

    NetcdfRPCOutput();

    /**
     * Constructor.
     */
    NetcdfRPCOutput(IOChannel* ioc,SampleConnectionRequester* rqstr=0);

    /**
     * Destructor.
     */
    ~NetcdfRPCOutput();

    /**
     * Implementation of SampleClient::flush().
     */
    void flush() throw() {}

    /**
     * Request a connection, but don't wait for it.  Requester will be
     * notified via SampleConnectionRequester interface when the connection
     * has been made.
     */
    void requestConnection(SampleConnectionRequester*) throw(nidas::util::IOException);

    SampleOutput* connected(IOChannel* ioc) throw();

    /**
    * Raw write not supported.
    */
    size_t write(const void*, size_t)
    	throw (nidas::util::IOException)
    {
	throw nidas::util::IOException(getName(),"default write","not supported");
    }

    /**
     * Send a data record to the RPC server.
     */
    bool receive(const Sample*) throw ();

    void fromDOMElement(const xercesc::DOMElement* node)
	throw(nidas::util::InvalidParameterException);

    /**
     * The NetcdfRPCOutput can have a time window which clips the samples
     * outside the window.  Only samples at or after @p startTime and
     * before @p endTime will be passed along.  One or both of start and
     * end time can be zero, in which case only the non-zero times are used
     * to clip samples.
     **/
    void
    setTimeClippingWindow(const nidas::util::UTime& startTime,
                          const nidas::util::UTime& endTime);

protected:

    /**
     * Clone invokes default copy constructor.
     */
    NetcdfRPCOutput* clone(IOChannel* iochannel)
    {
        return new NetcdfRPCOutput(*this,iochannel);
    }

    /**
     * Copy constructor, with a new IOChannel. Used by clone().
     */
    NetcdfRPCOutput(NetcdfRPCOutput&,IOChannel*);

    void setIOChannel(IOChannel* val);

private:

    NetcdfRPCChannel* _ncChannel;

    /**
     * Clipping time window.  Samples outside the given time window will
     * not pass.
     **/
    dsm_time_t _startTime;
    dsm_time_t _endTime;

    /**
     * No copy.
     */
    NetcdfRPCOutput(const NetcdfRPCOutput&);

    /**
     * No assignment.
     */
    NetcdfRPCOutput& operator=(const NetcdfRPCOutput&);

};

}}}	// namespace nidas namespace dynld namespace isff

#endif

#endif  // HAVE_LIBNC_SERVER_RPC
