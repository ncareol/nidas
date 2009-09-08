/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate$

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL$
 ********************************************************************

*/

#ifndef NIDAS_DYNLD_ISFF_NETCDFRPCOUTPUT_H
#define NIDAS_DYNLD_ISFF_NETCDFRPCOUTPUT_H

#include <nidas/core/SampleOutput.h>

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
    NetcdfRPCOutput(IOChannel* ioc);

    /**
     * Destructor.
     */
    ~NetcdfRPCOutput();

    /**
     * Request a connection, but don't wait for it.  Requester will be
     * notified via SampleConnectionRequester interface when the connection
     * has been made.
     */
    void requestConnection(SampleConnectionRequester*) throw();

    SampleOutput* connected(IOChannel* ioc) throw();

    /**
    * Raw write not supported.
    */
    size_t write(const void* buf, size_t len)
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
