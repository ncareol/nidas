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

#ifndef NIDAS_CORE_SITE_H
#define NIDAS_CORE_SITE_H

#include "DOMable.h"
#include "DSMConfig.h"
#include "Parameter.h"
#include "Dictionary.h"

#include <list>
#include <map>

namespace nidas { namespace core {

class Project;
class DSMServer;
class DSMConfig;
class DSMSensor;

/**
 * A measurement site. It could be an Aircraft, or a grouping of
 * measurement systems (e.g. "meadow" site).
 */
class Site : public DOMable {
public:
    Site();

    virtual ~Site();

    /**
     * Set the name of the Site.
     */
    void setName(const std::string& val) { _name = val; }

    const std::string& getName() const { return _name; }

    /**
     * Identify the Site by number. The site number
     * can be used for things like a NetCDF station
     * dimension. 
     * @param Site number, 0 means no number is associated with the site.
     */
    void setNumber(int val) { _number = val; }

    int getNumber() const { return _number; }

    /**
     * Equivalence operator for Site, checks name.
     */
    bool operator == (const Site& x) const
    {
        if (this == &x) return true;
        return _name == x._name &&
            _suffix == x._suffix &&
            _number == x._number;
    }

    /**
     * Non-equivalence operator for Site.
     */
    bool operator != (const Site& x) const
    {
        return !operator == (x);
    }

    /**
     * Less than operator for Site, compares the names.
     */
    bool operator < (const Site& x) const
    {
        if (operator == (x)) return false;
        return _name.compare(x._name) < 0;
    }

    /**
     * Set the suffix for the Site. All variable names from this
     * site will have the suffix.
     */
    void setSuffix(const std::string& val) { _suffix = val; }

    const std::string& getSuffix() const { return _suffix; }

    /**
     * Provide pointer to Project.
     */
    const Project* getProject() const { return _project; }

    Project* getProject() { return _project; }

    /**
     * Set the current project for this Site.
     */
    void setProject(Project* val) { _project = val; }

    /**
     * A Site contains one or more DSMs.  Site will
     * own the pointer and will delete the DSMConfig in its
     * destructor.
     */
    void addDSMConfig(DSMConfig* dsm)
    {
        _dsms.push_back(dsm);
        _ncDsms.push_back(dsm);
    }

    void removeDSMConfig(DSMConfig* dsm)
    {
        std::list<const DSMConfig*>::iterator di;
        for (di = _dsms.begin(); di != _dsms.end(); ) 
            if (dsm == *di) di = _dsms.erase(di);
            else ++di;
        for (std::list<DSMConfig*>::iterator di = _ncDsms.begin();
             di != _ncDsms.end(); )
            if (dsm == *di) {
                DSMConfig* deletableDSMConfig =  *di;
                di = _ncDsms.erase(di);
                // The DSM configuration has been removed from both lists, now delete the object.
                delete deletableDSMConfig;
            }
            else ++di;
    }

    const std::list<const DSMConfig*>& getDSMConfigs() const
    {
        return _dsms;
    }

    const std::list<DSMConfig*>& getDSMConfigs()
    {
        return _ncDsms;
    }

    /**
     * A Site has one or more DSMServers.
     */
    void addServer(DSMServer* srvr) { _servers.push_back(srvr); }

    const std::list<DSMServer*>& getServers() const { return _servers; }

    /**
     * Look for a server on this aircraft that either has no name or whose
     * name matches hostname.  If none found, remove any domain names
     * and try again.
     */
    DSMServer* findServer(const std::string& hostname) const;

    /**
     * Find a DSM by id.
     */
    const DSMConfig* findDSM(unsigned int id) const;

    /**
     * Find a DSM by name.
     */
    const DSMConfig* findDSM(const std::string& name) const;

    /**
     * Find a DSMSensor by the full id, both the DSM id and the sensor id.
     */
    DSMSensor* findSensor(unsigned int id) const;

    /**
     * Initialize all sensors for a Site.
     *
     * @throws nidas::util::IOException
     */
    void initSensors();

    /**
     * Initialize all sensors for a given dsm.
     *
     * @throws nidas::util::IOException
     */
    void initSensors(DSMConfig* dsm);

    /**
     * Add a parameter to this Site. Site
     * will then own the pointer and will delete it
     * in its destructor.
     */
    virtual void addParameter(Parameter* val);

    virtual const Parameter* getParameter(const std::string& name) const;

    virtual const std::list<const Parameter*>& getParameters() const;

    /**
     * Do we want DSMSensor::process methods at this site to apply
     * variable conversions?  Currently on raf.Aircraft we don't
     * want process methods to apply the conversions.
     */
    virtual bool getApplyVariableConversions() const
    {
        return _applyCals;
    }

    /**
     * Utility function to expand ${TOKEN} or $TOKEN fields
     * in a string.
     */
    /**
     * Utility function to expand ${TOKEN} or $TOKEN fields
     * in a string with their value from getTokenValue().
     * If curly brackets are not used, then the TOKEN should
     * be delimited by a '/', a '.' or the end of string,
     * e.g.:  xxx/yyy/$ZZZ.dat
     */
    std::string expandString(const std::string& input) const
    {
        return _dictionary.expandString(input);
    }

    /**
     * Implement a lookup for tokens that I know about, like $SITE, and
     * $AIRCRAFT.  For other tokens, call getProject()->getTokenValue(token,value);
     */
    bool getTokenValue(const std::string& token,std::string& value) const
    {
        return _dictionary.getTokenValue(token,value);
    }

    const Dictionary& getDictionary() const
    {
        return _dictionary;
    }

    DSMServerIterator getDSMServerIterator() const;

    DSMServiceIterator getDSMServiceIterator() const;

    ProcessorIterator getProcessorIterator() const;

    DSMConfigIterator getDSMConfigIterator() const;

    SensorIterator getSensorIterator() const;

    SampleTagIterator getSampleTagIterator() const;

    VariableIterator getVariableIterator() const;

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void validate();

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void fromDOMElement(const xercesc::DOMElement*);

    /**
     * @throws xercesc::DOMException
     **/
    xercesc::DOMElement*
    toDOMParent(xercesc::DOMElement* parent, bool complete) const;

    /**
     * @throws xercesc::DOMException
     **/
    xercesc::DOMElement*
    toDOMElement(xercesc::DOMElement* node, bool complete) const;

private:

    /**
     * Pointer back to my project.
     */
    Project* _project;
	
    std::string _name;

    int _number;

    std::string _suffix;

    class MyDictionary : public Dictionary {
    public:
        MyDictionary(Site* site): _site(site) {}
        MyDictionary(const MyDictionary& x): Dictionary(),_site(x._site) {}
        MyDictionary& operator=(const MyDictionary& rhs)
        {
            if (&rhs != this) {
                *(Dictionary*) this = rhs;
                _site = rhs._site;
            }
            return *this;
        }
        bool getTokenValue(const std::string& token, std::string& value) const;
    private:
        Site* _site;
    } _dictionary;

    std::list<const DSMConfig*> _dsms;

    std::list<DSMConfig*> _ncDsms;

    std::list<DSMServer*> _servers;

    /**
     * Mapping of Parameters, by name.
     */
    std::map<std::string,Parameter*> _parameterMap;

    /**
     * List of const pointers to Parameters for providing via
     * getParameters().
     */
    std::list<const Parameter*> _constParameters;

    /**
     * Copy not supported. See Project copy constructor: Project(const Project&);
     */
    Site(const Site&);

    /**
     * Assignment not supported.
     */
    Site& operator=(const Site&);

protected:
    /**
     * Should NIDAS apply calibrations, or defer them to other processing.
     */
    bool _applyCals;

};

}}	// namespace nidas namespace core

#endif
