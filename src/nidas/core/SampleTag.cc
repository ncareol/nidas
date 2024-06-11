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

#include "SampleTag.h"
#include "Project.h"
#include "Site.h"
#include "DSMConfig.h"
#include "CalFile.h"
#include "Parameter.h"
#include "Variable.h"
#include "DSMSensor.h"

#include <sstream>
#include <iostream>
#include <iomanip>

using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

SampleTag::SampleTag(const DSMSensor* sensor):
    _id(0),_sampleId(0),_sensorId(0),_suffix(),
    _station(-1),
    _rate(0.0),_processed(true),
    _dsm(0),_sensor(sensor),
    _constVariables(),_variables(),_variableNames(),
    _scanfFormat(),_promptString(), _promptOffset(0.0),
    _parameters(), _constParameters(),_enabled(true),
     _ttAdjustVal(-1.0)
{
    if (_sensor)
    {
        _station = sensor->getStation();
        setSensorId(_sensor->getId());
        _dsm = sensor->getDSMConfig();
    }
    if (_dsm)
        setDSMId(_dsm->getId());
}

/* copy constructor */
SampleTag::SampleTag(const SampleTag& x):
    DOMable(),
    _id(x._id),_sampleId(x._sampleId),_sensorId(x._sensorId),
    _suffix(x._suffix),
    _station(x._station),
    _rate(x._rate),_processed(x._processed),
    _dsm(x._dsm),
    _sensor(x._sensor),
    _constVariables(),_variables(),_variableNames(),
    _scanfFormat(x._scanfFormat),
    _promptString(x._promptString),
    _promptOffset(x._promptOffset),
    _parameters(), _constParameters(),_enabled(x._enabled),
    _ttAdjustVal(x._ttAdjustVal)
{
    const vector<const Variable*>& vars = x.getVariables();
    vector<const Variable*>::const_iterator vi;
    for (vi = vars.begin(); vi != vars.end(); ++vi) {
        const Variable* var = *vi;
	Variable* newv = new Variable(*var);
	addVariable(newv);
    }

    const list<const Parameter*>& params = x.getParameters();
    list<const Parameter*>::const_iterator pi;
    for (pi = params.begin(); pi != params.end(); ++pi) {
        const Parameter* parm = *pi;
	Parameter* newp = parm->clone();
	addParameter(newp);
    }
}

SampleTag& SampleTag::operator=(const SampleTag& rhs)
{
    if (&rhs != this) {
        *(DOMable*) this = rhs;
	_id = rhs._id;
        _sampleId = rhs._sampleId;
        _sensorId = rhs._sensorId;
	_suffix = rhs._suffix;
	_station = rhs._station;
	_rate = rhs._rate;
        _processed = rhs._processed;
	_dsm = rhs._dsm;
	_sensor = rhs._sensor;
	_scanfFormat = rhs._scanfFormat;
        _promptString = rhs._promptString;
        _promptOffset = rhs._promptOffset;
        _enabled = rhs._enabled;

        const vector<const Variable*>& vars = rhs.getVariables();
        vector<const Variable*>::const_iterator vi;
        for (vi = vars.begin(); vi != vars.end(); ++vi) {
            const Variable* var = *vi;
            Variable* newv = new Variable(*var);
            addVariable(newv);
        }

        const list<const Parameter*>& params = rhs.getParameters();
        list<const Parameter*>::const_iterator pi;
        for (pi = params.begin(); pi != params.end(); ++pi) {
            const Parameter* parm = *pi;
            Parameter* newp = parm->clone();
            addParameter(newp);
        }
    }
    return *this;
}

SampleTag::~SampleTag()
{
    for (vector<Variable*>::const_iterator vi = _variables.begin();
    	vi != _variables.end(); ++vi) delete *vi;

    list<Parameter*>::const_iterator pi;
    for (pi = _parameters.begin(); pi != _parameters.end(); ++pi)
    	delete *pi;
}

void SampleTag::addVariable(Variable* var)
{
    _variables.push_back(var);
    _constVariables.push_back(var);
    var->setSampleTag(this);
}

void SampleTag::setDSMSensor(const DSMSensor* val)
{
    _sensor = val;
    if (_sensor)
    {
        setDSMConfig(_sensor->getDSMConfig());
        for (auto var: _variables) {
            var->setSite(_sensor->getSite());
        }
        setSensorId(_sensor->getSensorId());
        setDSMId(_sensor->getDSMId());
        setSuffix(_sensor->getFullSuffix());
        setStation(_sensor->getStation());
    }
}

void SampleTag::setStation(int val)
{
    _station = val;
    vector<Variable*>::iterator vi = _variables.begin();
    for ( ; vi != _variables.end(); ++vi) {
        Variable* var = *vi;
        var->setStation(val);
    }
}

const Site* SampleTag::getSite() const
{
    const Site* site = 0;
    // probably sensor is never null, and probably it will have a dsm pointer
    // if this tag does, but this is just in case since it's not always clear
    // where and how sample tags get constructed.
    if (_sensor) site = _sensor->getSite();
    if (_dsm) site = _dsm->getSite();
    return site;
}

void SampleTag::removeVariable(const Variable* var)
       //throw(n_u::InvalidParameterException)
{
    Variable *deleteableVar = 0;
    for (unsigned int i = 0; i < _variables.size(); i++)
        if (_variables[i]->getName() == var->getName() &&
            _variables[i] == var) {
            deleteableVar = _variables[i];
            _variables.erase(_variables.begin() + i);
        }

    for (unsigned int i = 0; i < _constVariables.size(); i++)
        if (_constVariables[i]->getName() == var->getName() &&
            _constVariables[i] == var) 
            _constVariables.erase(_constVariables.begin() + i);

    if (deleteableVar) 
        delete deleteableVar;
}

void SampleTag::setSuffix(const std::string& val)
{
    _suffix = val;
    for (auto var: _variables) {
        var->setSuffix(_suffix);
    }
}

const std::vector<const Variable*>& SampleTag::getVariables() const
{
    return _constVariables;
}

VariableIterator SampleTag::getVariableIterator() const
{
    return VariableIterator(this);
}

unsigned int SampleTag::getDataIndex(const Variable* var) const
{
    unsigned int i = 0;
    std::vector<const Variable*>::const_iterator vi = _constVariables.begin();
    for ( ; vi != _constVariables.end(); ++vi) {
        if (*vi == var) return i;
        i += (*vi)->getLength();
    }
    return UINT_MAX;
}

void SampleTag::addParameter(Parameter* val)
{
    list<Parameter*>::iterator pi;
    list<const Parameter*>::iterator pi2 = _constParameters.begin();
    for (pi = _parameters.begin(); pi != _parameters.end(); ) {
        Parameter* param = *pi;
    	if (param->getName() == val->getName()) {
            pi = _parameters.erase(pi);
            pi2 = _constParameters.erase(pi2);
            delete param;
        }
        else {
            ++pi;
            ++pi2;
        }
    }
    _parameters.push_back(val);
    _constParameters.push_back(val);
}

const Parameter* SampleTag::getParameter(const string& name) const
{
    list<const Parameter*>::const_iterator pi;
    for (pi = _constParameters.begin(); pi != _constParameters.end(); ++pi) {
        const Parameter* param = *pi;
    	if (param->getName() == name) return param;
    }
    return 0;
}

void SampleTag::fromDOMElement(const xercesc::DOMElement* node)
{
    const Site* site = getSite();

    string suffix;

    XDOMElement xnode(node);
    if(node->hasAttributes()) {
    // get all the attributes of the node
	xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
	int nSize = pAttributes->getLength();
	for(int i=0;i<nSize;++i) {
	    XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
	    // get attribute name
	    const std::string& aname = attr.getName();
	    const std::string& aval = attr.getValue();
            std::string sval;
            if (getDSMSensor()) sval = getDSMSensor()->expandString(aval);
            else sval = Project::getInstance()->expandString(aval);

	    if (aname == "id") {
                istringstream ist(sval);
		unsigned int val;
		// If you unset the dec flag, then a leading '0' means
		// octal, and 0x means hex.
		ist.unsetf(ios::dec);
		ist >> val;
		if (ist.fail())
		    throw n_u::InvalidParameterException("sample",
		    	aname,sval);
		setSampleId(val);
		// cerr << "attr=" << sval << " id=" << val << endl;
	    }
	    else if (aname == "rate") {
                istringstream ist(sval);
		double rate;
		ist >> rate;
		if (ist.fail() || rate < 0.0)            
                {
                    ostringstream ost;
                    ost << "sample id=" << getDSMId() << ',' << getSpSId();
		    throw n_u::InvalidParameterException(ost.str(),
		    	aname,sval);
                }
		setRate(rate);
	    }
	    else if (aname == "period") {
                istringstream ist(sval);
		double period;
		ist >> period;
		if (ist.fail() || period < 0.0) {
                    ostringstream ost;
                    ost << "sample id=" << GET_DSM_ID(getId()) << ',' << GET_SPS_ID(getId());
		    throw n_u::InvalidParameterException(ost.str(),
		    	aname,sval);
                }
		setPeriod(period);
	    }
	    else if (aname == "scanfFormat")
		setScanfFormat(aval);   // Don't do $ expansion on this in case it might contain a $
	    else if (aname == "process") {
                istringstream ist(sval);
		bool process;
		ist >> boolalpha >> process;
		if (ist.fail()) {
		    ist.clear();
		    ist >> noboolalpha >> process;
		    if (ist.fail()) {
                        ostringstream ost;
                        ost << "sample id=" << GET_DSM_ID(getId()) << ',' << GET_SPS_ID(getId());
			throw n_u::InvalidParameterException(ost.str(),
			    aname,sval);
                    }
		}
		setProcessed(process);
		// cerr << "processed=" << process << endl;
            }
	    else if (aname == "suffix")
	    	suffix = sval;
	    else if (aname == "station") {
                int station;
                istringstream ist(sval);
		ist >> station;
		if (ist.fail()) {
                    ostringstream ost;
                    ost << "sample id=" << GET_DSM_ID(getId()) << ',' << GET_SPS_ID(getId());
		    throw n_u::InvalidParameterException(ost.str(),
		    	aname,sval);
                }
                setStation(station);
            }
	    else if (aname == "enable" || aname == "disable") {
		std::istringstream ist(sval);
		ist >> boolalpha;
		bool val;
		ist >> val;
		if (ist.fail()) {
                    ostringstream ost;
                    ost << "sample id=" << getDSMId() << ',' << getSpSId();
                    throw n_u::InvalidParameterException(ost.str(),aname,sval);
                }
                if (aname == "enable") setEnabled(val);
                else setEnabled(!val);
            }
	    else if (aname == "ttadjust") {
                std::istringstream ist(sval);
		float val;
		ist >> val;
		if (ist.fail()) {
                    ostringstream ost;
                    ost << "sample id=" << getDSMId() << ',' << getSpSId();
                    throw n_u::InvalidParameterException(ost.str(),aname,sval);
                }
                setTimetagAdjust(val);
	    }
            else if (aname != "xmlns") {
                // XMLConfigWriter seems to add xmlns attributes
                ostringstream ost;
                ost << "sample id=" << getDSMId() << ',' << getSpSId();
                throw n_u::InvalidParameterException(ost.str(),
		    	"unknown attribute",aname);
            }
	}
    }
    unsigned int nvars = 0;
    xercesc::DOMNode* child;
    for (child = node->getFirstChild(); child != 0;
	    child=child->getNextSibling())
    {
	if (child->getNodeType() != xercesc::DOMNode::ELEMENT_NODE) continue;
	XDOMElement xchild((xercesc::DOMElement*) child);
	const string& elname = xchild.getNodeName();

        // A SampleTag for a DSMSensor can be configured more than
        // once in the XML. The successive configurations for a SampleTag
        // with the same id in a DSMSensor add or override the attributes
        // of the original SampleTag.
        // The list of variables for a SampleTag is presumed to be
        // in the same sequence in every definition, so that, for 
        // example, the attributes of the second variable in a
        // SampleTag will be overridden with the attributes of the
        // second variable in successive definition of the SampleTag
        // with the same id.
	if (elname == "variable") {
	    Variable* var;
	    if (nvars == _variables.size()) var = new Variable();
	    else var = _variables[nvars];

	    var->setStation(getStation());
	    var->setSite(site);
            var->setSampleTag(this);

            // add the variable if it is new
	    if (nvars == _variables.size()) addVariable(var);

	    var->fromDOMElement((xercesc::DOMElement*)child);

            VariableConverter* cvtr = var->getConverter();
            if (_sensor && cvtr && cvtr->getCalFile())
                cvtr->getCalFile()->setDSMSensor(_sensor);
	    nvars++;
	}
	else if (elname == "parameter")  {
            const Dictionary* dict = 0;
            if (getDSMSensor()) dict = &getDSMSensor()->getDictionary();
	    Parameter* parameter =
	    	Parameter::createParameter((xercesc::DOMElement*)child,dict);
	    addParameter(parameter);
	}
        else if (elname == "prompt") {
            std::string prompt = xchild.getAttributeValue("string");
            setPromptString(prompt);
            istringstream ist(xchild.getAttributeValue("rate"));
            double promptrate;
            ist >> promptrate;
	    if (ist.fail() || promptrate < 0.0 || (getRate() != 0 && getRate() != promptrate)) {
                ostringstream ost;
                ost << "sample id=" << GET_DSM_ID(getId()) << ',' << GET_SPS_ID(getId());
                throw n_u::InvalidParameterException(ost.str(),
                    "prompt rate", xchild.getAttributeValue("rate"));
            }
            setRate(promptrate);

            std::string offsetStr = xchild.getAttributeValue("offset");
            if (!offsetStr.empty()) {
                istringstream ist(offsetStr);
                double offset;
                ist >> offset;
                if (ist.fail()) {
                    ostringstream ost;
                    ost << "sample id=" << GET_DSM_ID(getId()) << ',' << GET_SPS_ID(getId());
                    throw n_u::InvalidParameterException(ost.str(),
                        "prompt offset", offsetStr);
                }
                setPromptOffset(offset);
            }
        }
	else {
            ostringstream ost;
            ost << "sample id=" << GET_DSM_ID(getId()) << ',' << GET_SPS_ID(getId());
            throw n_u::InvalidParameterException(ost.str(),
		"unknown child element of sample",elname);
        }
    }
    if (suffix.length() > 0) setSuffix(suffix);
}

xercesc::DOMElement* SampleTag::toDOMParent(xercesc::DOMElement* parent,
                                            bool complete) const
{
    xercesc::DOMElement* elem =
        parent->getOwnerDocument()->createElementNS(
            DOMable::getNamespaceURI(),
            (const XMLCh*)XMLStringConverter("sample"));
    parent->appendChild(elem);
    return toDOMElement(elem,complete);
}

xercesc::DOMElement* SampleTag::toDOMElement(xercesc::DOMElement* elem,
                                             bool complete) const
{
    if (complete) return 0; // not supported yet

    dsm_sample_id_t id = getId();
    ostringstream ost;
    ost << id;
    XDOMElement xelem(elem);
    xelem.setAttributeValue("id",ost.str());
    for (VariableIterator vi = getVariableIterator(); vi.hasNext(); ) {
        const Variable* var = vi.next();
        var->toDOMParent(elem,complete);
    }
    return elem;
}


std::string SampleTag::toString() const {
    std::ostringstream out;
    out << "SampleTag(" << GET_DSM_ID(_id) << "," << GET_SPS_ID(_id) << "): ";
    out << "suffix=" << _suffix << "; station=" << _station << "; "
        << "dsm=" << (_dsm ? _dsm->getName() : "")
        << "; sensor=" << (_sensor ? _sensor->getName() : "")
        << "; variables=";
    int nvar{0};
    for (auto vp: _variables)
        out << (nvar++ ? "," : "") << vp->getName();
    return out.str();
}
