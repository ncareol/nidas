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
#ifndef NIDAS_CORE_NIDSITERATORS_H
#define NIDAS_CORE_NIDSITERATORS_H

#include <list>
// #include <set>
#include <vector>
#include <iterator>

namespace nidas { namespace core {

class Project;
class DSMServer;
class DSMService;
class Site;
class Variable;

/**
 * Class for iterating over the Sites of a Project,
 * or the Sites served by a DSMServer.
 */
class SiteIterator
{
public:
    SiteIterator(const Project*);

    SiteIterator();

    bool hasNext();

    Site* next() { return *_siteItr++; }

private:

    const std::list<Site*>* _sites;

    std::list<Site*>::const_iterator _siteItr;
};

/**
 * Class for iterating over the DSMServers of a Project.
 */
class DSMServerIterator
{
public:
    DSMServerIterator(const Project*);

    DSMServerIterator(const Site*);

    DSMServerIterator();

    bool hasNext();

    DSMServer* next() { return *_serverItr++; }

private:

    const std::list<DSMServer*>* _servers;

    std::list<DSMServer*>::const_iterator _serverItr;

    SiteIterator _siteIterator;
};

/**
 * Class for iterating over the DSMServices of a DSMServer.
 */
class DSMServiceIterator
{
public:
    DSMServiceIterator(const Project*);

    DSMServiceIterator(const Site*);

    DSMServiceIterator(const DSMServer*);

    DSMServiceIterator();

    bool hasNext();

    DSMService* next() { return *_dsmServiceItr++; }

private:

    DSMServerIterator _dsmServerIterator;

    const std::list<DSMService*>* _services;

    std::list<DSMService*>::const_iterator _dsmServiceItr;
};

class SampleIOProcessor;

class DSMConfig;


/**
 * Class for iterating over the DSMConfigs of a Project or
 * Site.
 */
class DSMConfigIterator
{
public:
    DSMConfigIterator(const Project*);

    DSMConfigIterator(const Site*);

    DSMConfigIterator();

    bool hasNext();

    const DSMConfig* next() { return *_dsmItr++; }

private:

    SiteIterator _siteIterator;

    const std::list<const DSMConfig*>* _dsms;

    std::list<const DSMConfig*>::const_iterator _dsmItr;
};

/**
 * Class for iterating over the Processors of a DSMServer or DSMConfig.
 */
class ProcessorIterator
{
public:
    ProcessorIterator(const Project*);

    ProcessorIterator(const Site*);

    ProcessorIterator(const DSMServer*);

    ProcessorIterator(const DSMService*);

    ProcessorIterator(const DSMConfig*);

    ProcessorIterator();

    bool hasNext();

    SampleIOProcessor* next() { return *_procItr++; }

private:

    DSMServiceIterator _serviceIterator;

    DSMConfigIterator _dsmIterator;

    const std::list<SampleIOProcessor*>* _processors;

    std::list<SampleIOProcessor*>::const_iterator _procItr;
};

class DSMSensor;

/**
 * Class for iterating over the DSMSensors of a Project,
 * Site, or DSMConfig.
 */
class SensorIterator
{
public:
    SensorIterator(const Project*);

    SensorIterator(const Site*);

    SensorIterator(const DSMConfig*);

    SensorIterator();

    bool hasNext();

    DSMSensor* next() { return *_sensorItr++; }

private:

    DSMConfigIterator _dsmIterator;

    const std::list<DSMSensor*>* _sensors;

    std::list<DSMSensor*>::const_iterator _sensorItr;
};

class SampleSource;
class SampleIOProcessor;
class SampleTag;

/**
 * Class for iterating over the SampleTags of a Project,
 * Site, DSMConfig, or a SampleSource.
 */
class SampleTagIterator
{
public:

    SampleTagIterator(const Project*);

    SampleTagIterator(const DSMServer*);

    SampleTagIterator(const DSMService*);

    SampleTagIterator(const Site*);

    SampleTagIterator(const DSMConfig*);

    SampleTagIterator(const SampleSource*);

    SampleTagIterator();

    bool hasNext();

    const SampleTag* next() { return *std::next(_stags.begin(), _i++); }

private:

    SensorIterator _sensorIterator;

    ProcessorIterator _processorIterator;

    std::list<const SampleTag*> _stags{};

    size_t _i{0};
};

/**
 * Class for iterating over the Variables of a Project,
 * Site, DSMConfig, DSMSensor, or SampleTag.
 */
class VariableIterator
{
public:

    VariableIterator(const Project*);

    VariableIterator(const DSMServer*);

    VariableIterator(const DSMService*);

    VariableIterator(const Site*);

    VariableIterator(const DSMConfig*);

    VariableIterator(const SampleSource*);

    VariableIterator(const SampleTag*);

    bool hasNext();

    const Variable* next() { return _variables[_i++]; }

private:

    SampleTagIterator _sampleTagIterator;

    std::vector<const Variable*> _variables{};

    size_t _i{0};
};

}}	// namespace nidas namespace core

#endif
