// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*-
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

#include "Site.h"
#include "DSMServer.h"
#include "DSMConfig.h"
#include "DSMSensor.h"
#include "SampleTag.h"
#include "Variable.h"
#include "Project.h"

#include <iostream>
#include <set>

using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

Site::Site():
    _project(0),_name(),_number(0),_suffix(),
    _dictionary(this),
    _dsms(),_ncDsms(),_servers(),
    _parameterMap(),_constParameters(),
    _applyCals(true)
{
}

Site::~Site()
{
    map<string,Parameter*>::iterator pi;

    for (pi = _parameterMap.begin(); pi != _parameterMap.end(); ++pi)
    	delete pi->second;

    // cerr << "deleting DSMServers" << endl;
    for (list<DSMServer*>::iterator is = _servers.begin();
    	is != _servers.end(); ++is) delete *is;

    // cerr << "deleting DSMConfigs" << endl;
    for (list<DSMConfig*>::iterator it = _ncDsms.begin();
    	it != _ncDsms.end(); ++it) delete *it;

}

DSMServerIterator Site::getDSMServerIterator() const
{
    return DSMServerIterator(this);
}

DSMServiceIterator Site::getDSMServiceIterator() const
{
    return DSMServiceIterator(this);
}

ProcessorIterator Site::getProcessorIterator() const
{
    return ProcessorIterator(this);
}

DSMConfigIterator Site::getDSMConfigIterator() const
{
    return DSMConfigIterator(this);
}

SensorIterator Site::getSensorIterator() const
{
    return SensorIterator(this);
}

SampleTagIterator Site::getSampleTagIterator() const
{
    return SampleTagIterator(this);
}

VariableIterator Site::getVariableIterator() const
{
    return VariableIterator(this);
}

/**
 * Initialize all sensors for a Site.
 */
void Site::initSensors()
{
    const list<DSMConfig*>& dsms = getDSMConfigs();
    list<DSMConfig*>::const_iterator di;
    for (di = dsms.begin(); di != dsms.end(); ++di) {
	DSMConfig* dsm = *di;
    	dsm->initSensors();
    }
}

/**
 * Initialize all sensors for a given dsm.
 */
void Site::initSensors(DSMConfig* dsm)
{
    const list<DSMConfig*>& dsms = getDSMConfigs();
    list<DSMConfig*>::const_iterator di;
    for (di = dsms.begin(); di != dsms.end(); ++di) {
	DSMConfig* d = *di;
    	if (d == dsm) dsm->initSensors();
    }
}

/**
 * Add a parameter to this Site. Site
 * will then own the pointer and will delete it
 * in its destructor.
 */
void Site::addParameter(Parameter* val)
{
    _parameterMap.insert(make_pair(val->getName(),val));
    _constParameters.push_back(val);
}

const Parameter* Site::getParameter(const string& name) const
{
    map<string,Parameter*>::const_iterator pi = _parameterMap.find(name);
    if (pi == _parameterMap.end()) return 0;
    return pi->second;
}

const list<const Parameter*>& Site::getParameters() const
{
    return _constParameters;
}

void Site::fromDOMElement(const xercesc::DOMElement* node)
{
    XDOMElement xnode(node);
    if (xnode.getNodeName() != "site" &&
    	xnode.getNodeName() != "aircraft")
	    throw n_u::InvalidParameterException("Site::fromDOMElement",
                                             "xml node name",
                                             xnode.getNodeName());
		    
    if(node->hasAttributes()) {
        // get all the attributes of the node
        xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
        int nSize = pAttributes->getLength();
        for(int i=0;i<nSize;++i) {
            XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
            string aname = attr.getName();
            string aval = expandString(attr.getValue());
            if (aname == "name") setName(aval);
            else if (aname == "suffix") setSuffix(aval);
            else if (aname == "number") {
                istringstream ist(aval);
                int num;
                ist >> num;
                if (ist.fail()) 
                    throw n_u::InvalidParameterException
                        (((getName().length() == 0) ? "site" : getName()),
                         aname, aval);
                setNumber(num);
            }
            else if (aname == "applyCals") {
                istringstream ist(aval);
                bool val;
                ist >> boolalpha >> val;
                if (ist.fail()) {
                    ist.clear();
                    ist >> noboolalpha >> val;
                    if (ist.fail())
                        throw n_u::InvalidParameterException
                            (((getName().length() == 0) ? "site" : getName()),
                             aname, aval);
                }
                _applyCals = val;
            }
            else if (aname == "xml:base" || aname == "xmlns") {}
        }
    }

    // keep a set of DSM ids to make sure they are unique
    set<int> dsm_ids;
    // likewise with dsm names
    set<string> dsm_names;

    xercesc::DOMNode* child;
    for (child = node->getFirstChild(); child != 0;
         child=child->getNextSibling())
    {
        if (child->getNodeType() != xercesc::DOMNode::ELEMENT_NODE) continue;
        XDOMElement xchild((xercesc::DOMElement*) child);
        const string& elname = xchild.getNodeName();
        // cerr << "element name=" << elname << endl;

        if (elname == "dsm") {
            DSMConfig* dsm = new DSMConfig();
            dsm->setSite(this);
            try {
                dsm->fromDOMElement((xercesc::DOMElement*)child);
            }
            catch(const n_u::InvalidParameterException& e) {
                delete dsm;
                throw;
            }
            if (!dsm_ids.insert(dsm->getId()).second) {
                ostringstream ost;
                ost << dsm->getId();
                delete dsm;
                throw n_u::InvalidParameterException("dsm id",
                                                     ost.str(),"is not unique");
            }
            if (!dsm_names.insert(dsm->getName()).second) {
                const string& dsmname = dsm->getName();
                delete dsm;
                throw n_u::InvalidParameterException("dsm name",
                                                     dsmname,"is not unique");
            }
            addDSMConfig(dsm);
        }
        else if (elname == "server") {
            DSMServer* server = new DSMServer();
            server->setProject(const_cast<Project*>(getProject()));
            server->setSite(this);
            try {
                server->fromDOMElement((xercesc::DOMElement*)child);
            }
            catch(const n_u::InvalidParameterException& e) {
                delete server;
                throw;
            }
            addServer(server);
        }
        else if (elname == "parameter")  {
            Parameter* parameter =
                Parameter::createParameter((xercesc::DOMElement*)child,
                                           &_dictionary);
            addParameter(parameter);
        }
    }
}

void Site::validate()
{
    // Check that variables are unique. Loop over dsms and
    // sensors so that you can report the dsm and sensor name
    // of duplicate variable.
    vector<Variable> varset;
    vector<Variable> dupvarset;

    // keep a set of DSM ids to make sure they are unique
    set<int> dsm_ids;
    // likewise with dsm names
    set<string> dsm_names;

    const std::list<DSMConfig*>& dsms = getDSMConfigs();
    std::list<DSMConfig*>::const_iterator di = dsms.begin();

    for ( ; di != dsms.end(); ++di) {
        DSMConfig* dsm = *di;
	if (!dsm_ids.insert(dsm->getId()).second) {
	    ostringstream ost;
	    ost << dsm->getId();
	//    delete dsm;
	    throw n_u::InvalidParameterException("dsm id",
		ost.str(),"is not unique");
        }
        //insert = dsm_names.insert(dsm->getName());
        //if (!insert.second) {
        if (!dsm_names.insert(dsm->getName()).second) {
	    const string& dsmname = dsm->getName();
	//    delete dsm;
	    throw n_u::InvalidParameterException("dsm name",
		dsmname,"is not unique");
        }
        dsm->validate();
        for (SensorIterator si = dsm->getSensorIterator(); si.hasNext(); ) {
            const DSMSensor* sensor = si.next();
	    for (VariableIterator vi = sensor->getVariableIterator();
		vi.hasNext(); ) {
		const Variable* var = vi.next();
		if (sensor->getDuplicateIdOK()) {
		    auto vi = std::find(varset.begin(), varset.end(), *var);
		    if (vi != varset.end()) {
			ostringstream ost;
			ost << var->getName() << " from sensor=" <<
			    sensor->getName() << '(' <<
			    sensor->getDSMId() << ',' <<
			    sensor->getSensorId() << ')';
			throw n_u::InvalidParameterException("variable",
			    ost.str(),"is not unique");
		    }
		    dupvarset.push_back(*var);
		}
		else {
		    auto found = std::find(varset.begin(), varset.end(), *var);
		    auto it = std::find(dupvarset.begin(), dupvarset.end(), *var);
		    if (found != varset.end() || it != dupvarset.end()) {
			ostringstream ost;
                ost << var->getName() << " from sensor=" <<
			    sensor->getName() << '(' <<
			    sensor->getDSMId() << ',' <<
			    sensor->getSensorId() << ')';
			throw n_u::InvalidParameterException("variable",
			    ost.str(),"is not unique");
		    }
                    varset.push_back(*var);
		}
	    }
        }
    }
}

xercesc::DOMElement*
Site::
toDOMParent(xercesc::DOMElement* parent, bool complete) const
{
    xercesc::DOMElement* elem =
        parent->getOwnerDocument()->createElementNS(
            DOMable::getNamespaceURI(),
            (const XMLCh*)XMLStringConverter("site"));
    parent->appendChild(elem);
    return toDOMElement(elem,complete);
}

xercesc::DOMElement* Site::toDOMElement(xercesc::DOMElement* elem,
                                        bool complete) const
{
    if (complete) return 0; // not supported yet

    XDOMElement xelem(elem);
    xelem.setAttributeValue("name",getName());

    ostringstream ost;
    ost << getNumber();
    ost << ends;
    xelem.setAttributeValue("number",ost.str());

    for (DSMConfigIterator di = getDSMConfigIterator(); di.hasNext(); ) {
        const DSMConfig* dsm = di.next();
        dsm->toDOMParent(elem,complete);
    }
    return elem;
}

/**
 * Look for a server on this aircraft that either has no name or whose
 * name matches hostname.  If none found, remove any domain names
 * and try again.
 */
DSMServer* Site::findServer(const string& hostname) const
{
    DSMServer* server = 0;
    for (list<DSMServer*>::const_iterator si=_servers.begin();
	si != _servers.end(); ++si) {
	DSMServer* srvr = *si;
	if (srvr->getName().length() == 0 ||
	    srvr->getName() == hostname) {
	    server = srvr;
	    break;
	}
    }
    if (server) return server;

    // Not found, remove domain name, try again
    int dot = hostname.find('.');
    for (list<DSMServer*>::const_iterator si=_servers.begin();
	si != _servers.end(); ++si) {
	DSMServer* srvr = *si;
	const string& sname = srvr->getName();
	int sdot = sname.find('.');
	if (!sname.compare(0,sdot,hostname,0,dot)) {
	    server = srvr;
	    break;
	}
    }
    return server;
}

const DSMConfig* Site::findDSM(unsigned int id) const
{
    for (list<const DSMConfig*>::const_iterator di=_dsms.begin();
	di != _dsms.end(); ++di) {
	const DSMConfig* dsm = *di;
    VLOG(("Checking dsm ") << dsm->getName() << " for id=" << id);
	if (dsm->getId() == id) return dsm;
    }
    return 0;
}

const DSMConfig* Site::findDSM(const std::string& name) const
{
    for (list<const DSMConfig*>::const_iterator di=_dsms.begin();
	di != _dsms.end(); ++di) {
	const DSMConfig* dsm = *di;
        VLOG(("Checking dsm ") << dsm->getName() << " for name=" << name);
	if (dsm->getName() == name) return dsm;
    }

    string::size_type dot = name.find('.');
    if (dot != string::npos) {
        // recursive
        return findDSM(name.substr(0, dot));
    }

    return 0;
}

DSMSensor* Site::findSensor(unsigned int id) const
{
    SensorIterator si = getSensorIterator();
    for ( ; si.hasNext(); ) {
	DSMSensor* sensor = si.next();

    VLOG(("Site::findSensor, ") << getName() << ", getId="
         << sensor->getDSMId() << ',' << sensor->getSensorId()
         << " against id=" << GET_DSM_ID(id) << ',' << GET_SPS_ID(id));
	if (sensor->getId() == id) return sensor;
        SampleTagIterator sti = sensor->getSampleTagIterator();
        for ( ; sti.hasNext(); ) {
            const SampleTag* stag = sti.next();
            if (stag->getId() == id) return sensor;
        }
    }
    return 0;
}

bool Site::MyDictionary::getTokenValue(const string& token,string& value) const
{
    if (token == "AIRCRAFT" || token == "SITE") {
        value = _site->getName();
        return true;
    }
    
    if (_site->getProject()) {
        return _site->getProject()->getTokenValue(token,value);
    }
    return false;
}

