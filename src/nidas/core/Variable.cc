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

#include "Project.h"
#include "Site.h"
#include "Variable.h"
#include "SampleTag.h"
#include "Parameter.h"
#include "DSMSensor.h"

#include <nidas/util/Logger.h>

#include <sstream>
#include <limits>
#include <algorithm>

using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

Variable::Variable():
    _name(),
    _site(0),
    _station(-1),
    _nameWithoutSite(),_prefix(),_suffix(),_siteSuffix(),
    _longname(),
    _sampleTag(0),
    _A2dChannel(-1),_units(),
    _type(CONTINUOUS),
    _length(1),
    _converter(0),_parameters(),_constParameters(),
    _missingValue(1.e37),
    _minValue(-numeric_limits<float>::max()),
    _maxValue(numeric_limits<float>::max()),
    _dynamic(false)
{
    _plotRange[0] = floatNAN;
    _plotRange[1] = floatNAN;
}

/* copy constructor */
Variable::Variable(const Variable& x):
    DOMable(),
    _name(x._name),
    _site(x._site),
    _station(x._station),
    _nameWithoutSite(x._nameWithoutSite),
    _prefix(x._prefix),
    _suffix(x._suffix),
    _siteSuffix(x._siteSuffix),
    _longname(x._longname),
    _sampleTag(0),
    _A2dChannel(x._A2dChannel),
    _units(x._units),
    _type(x._type),
    _length(x._length),
    _converter(0),_parameters(),_constParameters(),
    _missingValue(x._missingValue),
    _minValue(x._minValue),
    _maxValue(x._maxValue),
    _dynamic(x._dynamic)
{
    if (x._converter) _converter = x._converter->clone();
    const list<const Parameter*>& params = x.getParameters();
    list<const Parameter*>::const_iterator pi;
    for (pi = params.begin(); pi != params.end(); ++pi) {
        const Parameter* parm = *pi;
	Parameter* newp = parm->clone();
	addParameter(newp);
    }
    _plotRange[0] = x._plotRange[0];
    _plotRange[1] = x._plotRange[1];
}

/* assignment */
Variable& Variable::operator=(const Variable& rhs)
{
    // do not assign sampleTag
    if (this != &rhs) {
        *(DOMable*) this = rhs;
        _name = rhs._name;
        _site = rhs._site;
        _station = rhs._station;
        _nameWithoutSite = rhs._nameWithoutSite;
        _prefix = rhs._prefix;
        _suffix  = rhs._suffix;
        _siteSuffix  = rhs._siteSuffix;
        _longname = rhs._longname;
        _A2dChannel = rhs._A2dChannel;
        _units = rhs._units;
        _type = rhs._type;
        _length = rhs._length;
        _missingValue = rhs._missingValue;
        _minValue = rhs._minValue;
        _maxValue = rhs._maxValue;
        _plotRange[0] = rhs._plotRange[0];
        _plotRange[1] = rhs._plotRange[1];
        _dynamic = rhs._dynamic;

        // this invalidates the previous pointer to the converter, hmm.
        // don't want to create a virtual assignment op for converters.
        if (rhs._converter) {
            delete _converter;
            _converter = rhs._converter->clone();
        }

        // If a Parameter from x matches in type and name,
        // assign our Parameter to it, otherwise add it.
        // We're trying to keep the pointers to Parameters valid
        // (avoiding deleting and cloning), in case someone
        // has done a getParameters() on this variable.
        const list<const Parameter*>& xparams = rhs.getParameters();
        list<const Parameter*>::const_iterator xpi;
        for (xpi = xparams.begin(); xpi != xparams.end(); ++xpi) {
            const Parameter* xparm = *xpi;
            ParameterNameTypeComparator comp(xparm);
            list<Parameter*>::iterator pi =
                    std::find_if(_parameters.begin(),_parameters.end(),comp);
            if (pi != _parameters.end()) (*pi)->assign(*xparm);
            else addParameter(xparm->clone());
        }
    }
    return *this;
}

Variable::~Variable()
{
    delete _converter;
    list<Parameter*>::const_iterator pi;
    for (pi = _parameters.begin(); pi != _parameters.end(); ++pi)
    	delete *pi;
}

void Variable::setSiteSuffix(const string& val)
{
    VLOG(("") << "setSiteSuffix(" << val << ")");
    // don't repeat site suffix, in case
    // user has only set full name with setName().
    if (_siteSuffix.length() == 0 && _suffix.length() == 0) {
        unsigned nl = _name.length();
        unsigned vl = val.length();
        if (vl > 0 && nl > vl && _name.substr(nl-vl,vl) == val)
            _prefix = _name.substr(0,nl-vl);
    }
    _siteSuffix = val;
    updateName();
}


void Variable::updateName()
{
    string site = _siteSuffix;
    string suffix = _suffix;
    // if the site suffix begins with ?, then do not append it
    // if this variable already has a suffix.
    if (site.length() > 0 && site[0] == '?')
    {
        if (_suffix.length()) {
            site = "";
        }
        else {
            site = site.substr(1);
        }
    }
    if (suffix.length() && suffix[0] == '!') {
        site = "";
        suffix = suffix.substr(1);
    }
    _name = _prefix + suffix + site;
    _nameWithoutSite = _prefix + suffix;
    VLOG(("") << "prefix=" << _prefix << ", suffix=" << _suffix
              << ", site=" << _siteSuffix << ": name => " << _name);
}



void Variable::setSampleTag(const SampleTag* val)
{ 
    _sampleTag = val;
    // if the Variable's site is undefined, set it from the sample
    if (!getSite()) setSite(_sampleTag->getSite());
    // if the Variable's station number is undefined, set it from the sample
    if (getStation() < 0) setStation(_sampleTag->getStation());
}

bool Variable::operator == (const Variable& x) const
{
    if (getLength() != x.getLength()) return false;

    if (_nameWithoutSite != x._nameWithoutSite && _name != x._name) return false;

    const Site* s1 = getSite();
    const Site* s2 = x.getSite();
    if (!s1) {
        if (!s2) return _station == x._station;   // both unknown (NULL) sites
        return false;           // site 1 unknown (NULL), site 2 known
    }
    else if (!s2) return false; // site 1 known, site 2 unknown (NULL)

    // both known sites, check Site equivalence
    if (*s1 != *s2) return false;

    // same known site, check station of variables
    // return _station == x._station;

    return true;
}

bool Variable::operator != (const Variable& x) const
{
    return !operator == (x);
}

bool Variable::closeMatch(const Variable& x) const
{
    if (*this == x) return true;
    if (getLength() != x.getLength()) return false;


    const Site* s1 = getSite();
    const Site* s2 = x.getSite();

    if (!s1) {

        // both sites unspecified, compare names
        if (!s2) return _nameWithoutSite == x._nameWithoutSite;

        // site of this Variable unknown, site of x known. Check against
        // name of x with and without site suffix
        else return _name == x._name || _name == x._nameWithoutSite;
    }
    else {
        // site of this Variable known, site of x unknown. Check 
        // name of x with this Variable's name, with and without site suffix.
        if (!s2) return _name == x._name || _nameWithoutSite == x._name;
        else {
            // If both sites are known then the == operator was sufficient.
            return false;
        }
    }
}

float Variable::getSampleRate() const {
    if (!_sampleTag) return 0.0;
    else return _sampleTag->getRate();
}

const Parameter* Variable::getParameter(const std::string& name) const
{
    std::list<const Parameter*>::const_iterator pi;
    for (pi = _constParameters.begin(); pi != _constParameters.end(); ++pi)
      if ((*pi)->getName().compare(name) == 0)
        return *pi;
    return 0;
}

void Variable::fromDOMElement(const xercesc::DOMElement* node)
{
    XDOMElement xnode(node);
    if(node->hasAttributes()) {
        // get all the attributes of the node
        xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
        int nSize = pAttributes->getLength();
        for(int i=0;i<nSize;++i) {
            XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
            const string& aname = attr.getName();
            string aval;
            if (_sampleTag && _sampleTag->getDSMSensor())
                aval = _sampleTag->getDSMSensor()->expandString(attr.getValue());
            else
                aval = Project::getInstance()->expandString(attr.getValue());
            // get attribute name
            if (aname == "name")
                setPrefix(aval);
            else if (aname == "longname")
                setLongName(aval);
            else if (aname == "units")
                setUnits(aval);
            else if (aname == "length") {
                istringstream ist(aval);
                unsigned int val;
                ist >> val;
                if (ist.fail())
                    throw n_u::InvalidParameterException
                        (string("variable ") + getName(),aname,aval);
                setLength(val);
            }
            else if (aname == "missingValue" ||
                     aname == "minValue" ||
                     aname == "maxValue") {
                istringstream ist(aval);
                float val;
                ist >> val;
                if (ist.fail())
                    throw n_u::InvalidParameterException
                        (string("variable") + getName(),aname,aval);
                string sname = aname.substr(0,3);
                if (sname == "mis") setMissingValue(val);
                else if (sname == "min") setMinValue(val);
                else if (sname == "max") setMaxValue(val);
            }
            else if (aname == "count") {
                if (aval == "true")
                    setType(Variable::COUNTER);
            }
            else if (aname == "plotrange") {
                // environment variables are expanded above.
                std::istringstream ist(aval);
                float prange[2] = { -10.0,10.0 };
                // if plotrange value starts with '$' ignore error.
                if (aval.length() < 1 || aval[0] != '$') {
                    int i;
                    for (i = 0; i < 2 ; i++) {
                        if (ist.eof()) break;
                        ist >> prange[i];
                        if (ist.fail()) break;
                    }
                    // Don't throw exception on poorly formatted plotranges
                    if (i < 2)  {
                        n_u::InvalidParameterException e(string("variable ") +
                                                         getName(),aname,aval);
                        WLOG(("%s",e.what()));
                    }
                }
                setPlotRange(prange[0],prange[1]);
            }
            else if (aname == "dynamic") {
                istringstream ist(aval);
                bool val;
                ist >> boolalpha >> val;
                if (ist.fail()) {
                    ist.clear();
                    ist >> noboolalpha >> val;
                    if (ist.fail())
                        throw n_u::InvalidParameterException
                            (string("variable ") + getName(),aname,aval);
                }
                setDynamic(val);
            }
        }
    }

    int nconverters = 0;
    xercesc::DOMNode* child;
    for (child = node->getFirstChild(); child != 0;
         child=child->getNextSibling())
    {
        if (child->getNodeType() != xercesc::DOMNode::ELEMENT_NODE) continue;

        XDOMElement xchild((xercesc::DOMElement*) child);
        const string& elname = xchild.getNodeName();
        if (elname == "parameter")  {
            const Dictionary* dict = 0;
            if (getSampleTag() && getSampleTag()->getDSMSensor())
                dict = &getSampleTag()->getDSMSensor()->getDictionary();
            Parameter* parameter =
                Parameter::createParameter((xercesc::DOMElement*)child,dict);
            addParameter(parameter);
        }
        else if (elname == "linear" || elname == "poly" ||
                 elname == "converter") {
            if (nconverters > 0)
                throw n_u::InvalidParameterException
                    (getName(),
                     "only one child converter allowed, <linear>, <poly> etc",
                     elname);
            VariableConverter* cvtr =
                VariableConverter::createVariableConverter(xchild);
            if (!cvtr) throw n_u::InvalidParameterException
                           (getName(), "unsupported child element", elname);
            cvtr->setVariable(this);
            cvtr->setUnits(getUnits());
            cvtr->fromDOMElement((xercesc::DOMElement*)child);
            setConverter(cvtr);
            nconverters++;
        }
        else throw n_u::InvalidParameterException
                 (string("variable ") + getName(),
                  "unsupported child element", elname);
    }
}

xercesc::DOMElement* Variable::toDOMParent(xercesc::DOMElement* parent,
                                           bool complete) const
{
    xercesc::DOMElement* elem =
        parent->getOwnerDocument()->createElementNS(
            DOMable::getNamespaceURI(),
            (const XMLCh*)XMLStringConverter("variable"));
    parent->appendChild(elem);
    return toDOMElement(elem,complete);
}

xercesc::DOMElement* Variable::toDOMElement(xercesc::DOMElement* elem,
                                            bool complete) const
{
    if (complete) return 0; // not supported yet

    XDOMElement xelem(elem);

    string units = getUnits();
    const VariableConverter* cvtr = getConverter();
    if (cvtr) units = cvtr->getUnits();

    xelem.setAttributeValue("name",getName());
    if (getLongName().length() > 0)
        xelem.setAttributeValue("longname",getLongName());
    if (units.length() > 0)
        xelem.setAttributeValue("units",units);
    if (getLength() > 1) {
        ostringstream ost;
        ost << getLength();
        xelem.setAttributeValue("length",ost.str());
    }
    if (isDynamic()) {
        xelem.setAttributeValue("dynamic","true");
    }

    float pmin = _plotRange[0];
    if (std::isnan(pmin)) pmin = -10.0;
    float pmax = _plotRange[1];
    if (std::isnan(pmax)) pmax = 10.0;
    ostringstream ost;
    ost << pmin << ' ' << pmax;
    xelem.setAttributeValue("plotrange",ost.str());
    return elem;
}



float*
Variable::
convert(dsm_time_t ttag, float* values, int nvalues, float* results)
{
    if (nvalues == 0)
        nvalues = getLength();
    if (!results)
        results = values;
    for (int id = 0; id < nvalues; id++, values++, results++)
    {
        float val = *values;
        /* check for missing value before conversion. This
         * is for sensors that put out something like -9999
         * for a missing value, which should be checked before
         * any conversion, and for which an exact equals check
         * should work.  Doing a equals check on a numeric after a
         * conversion is problematic.
         */
        if (val == getMissingValue())
        {
            val = floatNAN;
        }
        else if (!_site || _site->getApplyVariableConversions())
        {
            VariableConverter* conv = getConverter();
            if (conv)
            {
                val = conv->convert(ttag, val);
            }
            if (val < getMinValue() || val > getMaxValue())
            {
                val = floatNAN;
            }
        }
        *results = val;
    }
    return values;
}

