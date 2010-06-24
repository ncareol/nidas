/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate$

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL$
 ********************************************************************

*/

#ifndef NIDAS_CORE_DSMSERVER_H
#define NIDAS_CORE_DSMSERVER_H

#include <nidas/core/DSMService.h>
#include <nidas/core/Project.h>
#include <nidas/core/DOMable.h>
#include <nidas/core/StatusThread.h>
#include <nidas/core/XMLException.h>

#include <list>

namespace nidas { namespace core {

// class Site;

/**
 * A provider of services to a DSM.
 */
class DSMServer: public DOMable {
public:

    DSMServer();

    virtual ~DSMServer();

    const std::string& getName() const { return _name; }

    void setName(const std::string& val) { _name = val; }

    const std::string& getXMLConfigFileName() const { return _xmlFileName; }

    void setXMLConfigFileName(const std::string& val) { _xmlFileName = val; }

    void addService(DSMService* service) { _services.push_back(service); }

    const std::list<DSMService*>& getServices() const { return _services; }

    void setSite(Site* val) { _site = val; }

    const Site* getSite() const { return _site; }

    DSMServiceIterator getDSMServiceIterator() const;

    ProcessorIterator getProcessorIterator() const;

    SensorIterator getSensorIterator() const;

    SampleTagIterator getSampleTagIterator() const;

    void addThread(nidas::util::Thread* thrd);

    void scheduleServices() throw(nidas::util::Exception);

    void interruptServices() throw();

    void joinServices() throw();

    void setStatusSocketAddr(const nidas::util::SocketAddress& val)
    {
        delete _statusSocketAddr;
        _statusSocketAddr = val.clone();
    }

    const nidas::util::SocketAddress& getStatusSocketAddr() const
    {
        return *_statusSocketAddr;
    }

    void fromDOMElement(const xercesc::DOMElement*)
        throw(nidas::util::InvalidParameterException);

private:

    /**
     * Name of this server. This should correspond to a hostname
     * of a machine.
     */
    std::string _name;

    /**
     * What Site to I serve?  Can be NULL if this DSMServer is not for a specific Site,
     * but serves the Project.
     */
    Site* _site;

    /**
     * The DSMServices that we've been configured to start.
     */
    std::list<DSMService*> _services;

    std::string _xmlFileName;

    nidas::util::SocketAddress* _statusSocketAddr;

    /**
     * Copy not supported.
     */
    DSMServer(const DSMServer&);

    /**
     * Assignment not supported.
     */
    DSMServer& operator=(const DSMServer&);

};

}}	// namespace nidas namespace core

#endif
