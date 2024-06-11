// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:

/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2004, Copyright University Corporation for Atmospheric Research
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

#include "DSMSensor.h"
#include "Project.h"
#include "DSMConfig.h"
#include "Site.h"
#include "NidsIterators.h"
#include "Parameter.h"
#include "SensorCatalog.h"
#include "Looper.h"
#include "Variable.h"

#include "SamplePool.h"
#include "CalFile.h"

#include <nidas/util/Logger.h>

#include <cmath>
#include <iostream>
#include <iomanip>
#include <string>
#include <memory>

using namespace std;
using namespace nidas::core;
using nidas::util::LogContext;
using nidas::util::LogMessage;

namespace n_u = nidas::util;

/* static */
bool DSMSensor::zebra = false;

DSMSensor::DSMSensor() :
    _openable(true),
    _devname(),
    _dictionary(this),
    _iodev(0),_defaultMode(O_RDONLY),
    _className(),_catalogName(),
    _suffix(),_heightString(),_depthString(),
    _height(floatNAN),
    _fullSuffix(),_location(),
    _scanner(0),_dsm(0),_site(0),_id(0),
    _rawSampleTag(),
    _sampleTags(),
    _rawSource(true),
    _source(false),
    _latency(0.1),	// default sensor latency, 0.1 secs
    _parameters(),_constParameters(),
    _calFiles(),_typeName(),
    _timeoutMsecs(0),
    _duplicateIdOK(false),
    _applyVariableConversions(),
    _driverTimeTagUsecs(USECS_PER_TMSEC),
    _nTimeouts(0),_lag(0),_station(-1)
{
}

DSMSensor::~DSMSensor()
{
    for (list<SampleTag*>::const_iterator si = _sampleTags.begin();
        si != _sampleTags.end(); ++si) {
        delete *si;
    }
    delete _scanner;
    delete _iodev;

    map<std::string,Parameter*>::const_iterator pi;
    for (pi = _parameters.begin(); pi != _parameters.end(); ++pi)
	delete pi->second;

    removeCalFiles();
}

void DSMSensor::setDSMConfig(const DSMConfig* val)
{
    _dsm = val;
    if (_dsm) setDSMId(_dsm->getId());
}

void DSMSensor::addSampleTag(SampleTag* val)
{
    if (find(_sampleTags.begin(),_sampleTags.end(),val) == _sampleTags.end()) {
        _sampleTags.push_back(val);
        // Set the DSMSensor on the sample tag. This is done in fromDOMElement,
        // but the sample tag may have been created in some other way.
        val->setDSMSensor(this);
        _source.addSampleTag(val);
    }
    else {
        n_u::Logger::getInstance()->log(LOG_WARNING,
            "%s: duplicate sample tag pointer: %d,%d (added twice?)",
            getName().c_str(),GET_DSM_ID(val->getId()),GET_SHORT_ID(val->getId()));
    }
}

void DSMSensor::removeSampleTag(SampleTag* val)
{
    list<SampleTag*>::iterator si = find(_sampleTags.begin(),_sampleTags.end(),val);
    if (si != _sampleTags.end()) {
        _sampleTags.erase(si);
        _source.removeSampleTag(val);
    }
    delete val;
}

void DSMSensor::addSampleTag(const SampleTag*)
{
    assert(false);
}

void DSMSensor::removeSampleTag(const SampleTag*)
{
    assert(false);
}

VariableIterator DSMSensor::getVariableIterator() const
{
    return VariableIterator(this);
}

/*
 * What Site am I associated with?
 */
const Site* DSMSensor::getSite() const
{
    if (_site) return _site;
    if (_dsm) return _dsm->getSite();
    return 0;
}

void DSMSensor::setSite(Site* site)
{
    _site = site;
    DLOG(("") << getName() << ": site set to " << _site->getName());

    for (auto& stag: _sampleTags)
        stag->setDSMSensor(this);
}


void DSMSensor::setSite(const std::string& site_name)
{
    // resolve the site name and set that as the Site.
    std::ostringstream msg;
    std::string error;
    const DSMConfig* dsm = getDSMConfig();
    const Project* project{dsm ? dsm->getProject() : 0};
    Site* site{nullptr};
    if (!dsm || !project)
    {
        error = "cannot be resolved without a DSM and a Project";
    }
    else if (!(site = project->findSite(site_name)))
    {
        error = "not found";
    }
    if (!site)
    {
        msg << getName() << ": site=" << site_name << " " << error;
        throw n_u::InvalidParameterException(msg.str());
    }
    setSite(site);
}


/**
 * Fetch the DSM name.
 */
const std::string& DSMSensor::getDSMName() const {
    static std::string unk("unknown");
    if (_dsm) return _dsm->getName();
    return unk;
}

/*
 * If location is an empty string, return DSMConfig::getLocation()
 */
const std::string& DSMSensor::getLocation() const {
    if (_location.length() == 0 && _dsm) return _dsm->getLocation();
    return _location;
}

void DSMSensor::setStation(int val)
{
    _station = val;
    list<SampleTag*>::const_iterator si = _sampleTags.begin();
    for ( ; si != _sampleTags.end(); ++si) {
        SampleTag* stag = *si;
        stag->setStation(val);
    }
}

void DSMSensor::setSuffix(const std::string& val)
{
    _suffix = val;
    VLOG(("") << getName() << ": setSuffix(" << val << ")");
    if (_heightString.length() > 0)
        setFullSuffix(_suffix + string(".") + _heightString);
    else if (_depthString.length() > 0)
        setFullSuffix(_suffix + string(".") + _depthString);
    else
        setFullSuffix(_suffix);
}

void DSMSensor::setHeight(const std::string& val)
{
    VLOG(("") << getName() << ": setHeight(" << val << ")");
    _heightString = val;
    _depthString = "";
    if (_heightString.length() > 0) {
	float h;
	istringstream ist(val);
	ist >> h;
	if (ist.fail()) _height = floatNAN;
	else if (!ist.eof()) {
	    string units;
	    ist >> units;
	    if (!ist.fail()) {
		if (units == "cm") h /= 10.0;
		else if (units != "m") h = floatNAN;
	    }
	    _height = h;
	}
	setFullSuffix(getSuffix() + string(".") + _heightString);
    }
    else {
	_height = floatNAN;
	setFullSuffix(getSuffix());
    }
}


void DSMSensor::setFullSuffix(const std::string& val)
{
    VLOG(("") << getName() << ": set full suffix: " << val);
    _fullSuffix = val;
}


void DSMSensor::setHeight(float val)
{
    _height = val;
    if (! isnan(_height)) {
	ostringstream ost;
	ost << _height << 'm';
	_heightString = ost.str();
	setFullSuffix(getSuffix() + string(".") + _heightString);
        _depthString = "";
    }
    else setFullSuffix(getSuffix());
}

void DSMSensor::setDepth(const std::string& val)
{
    _depthString = val;
    _heightString = "";
    if (_depthString.length() > 0) {
	float d;
	istringstream ist(val);
	ist >> d;
	if (ist.fail()) _height = floatNAN;
	else if (!ist.eof()) {
	    string units;
	    ist >> units;
	    if (!ist.fail()) {
		if (units == "cm") d /= 10.0;
		else if (units != "m") d = floatNAN;
	    }
	    _height = -d;
	}
	setFullSuffix(getSuffix() + string(".") + _depthString);
    }
    else {
	_height = floatNAN;
        setFullSuffix(getSuffix());
    }
}

void DSMSensor::setDepth(float val)
{
    _height = -val;
    if (! isnan(_height)) {
	ostringstream ost;
	ost << val * 10.0 << "cm";
	_depthString = ost.str();
	setFullSuffix(getSuffix() + string(".") + _depthString);
        _heightString = "";
    }
    else setFullSuffix(getSuffix());
}

void DSMSensor::addCalFile(CalFile* val)
{
    map<string,CalFile*>::iterator ci = _calFiles.find(val->getName());
    if (ci != _calFiles.end()) delete ci->second;
    _calFiles[val->getName()] = val;
}

CalFile* DSMSensor::getCalFile(const std::string& name)
{
    //  use find, rather than _calFiles[name], which will
    //  create a NULL entry if not found (which isn't a big issue...)
    map<string,CalFile*>::iterator ci = _calFiles.find(name);
    if (ci != _calFiles.end()) return ci->second;
    return 0;
}

void DSMSensor::removeCalFiles()
{
    while (!_calFiles.empty()) {
        map<string,CalFile*>::iterator ci = _calFiles.begin();
	delete ci->second;
        _calFiles.erase(ci);
    }
}

/*
 * Add a parameter to my map, and list.
 */
void DSMSensor::addParameter(Parameter* val)
{
    map<string,Parameter*>::iterator pi = _parameters.find(val->getName());
    if (pi == _parameters.end()) {
        _parameters[val->getName()] = val;
	_constParameters.push_back(val);
    }
    else {
	// parameter with name exists. If the pointers aren't equal
	// delete the old parameter.
	Parameter* p = pi->second;
	if (p != val) {
	    // remove it from constParameters list
	    list<const Parameter*>::iterator cpi = _constParameters.begin();
	    for ( ; cpi != _constParameters.end(); ) {
		if (*cpi == p) cpi = _constParameters.erase(cpi);
		else ++cpi;
	    }
	    delete p;
	    pi->second = val;
	    _constParameters.push_back(val);
	}
    }
}

const Parameter* DSMSensor::getParameter(const std::string& name) const
{
    map<string,Parameter*>::const_iterator pi = _parameters.find(name);
    if (pi == _parameters.end()) return 0;
    return pi->second;
}

/*
 * Open the device. flags are a combination of O_RDONLY, O_WRONLY.
 */
void DSMSensor::open(int flags)
{
    if (!_iodev) _iodev = buildIODevice();
    _iodev->setName(getDeviceName());

    NLOG(("opening: ") << getDeviceName());

    _iodev->open(flags);
    if (!_scanner) _scanner = buildSampleScanner();
    _scanner->init();
}

void DSMSensor::close()
{
    NLOG(("closing: %s, #timeouts=%d",
        getDeviceName().c_str(),getTimeoutCount()));
    if (_iodev) _iodev->close();
}

void DSMSensor::init()
{
}

bool DSMSensor::readSamples()
{
    bool exhausted = readBuffer();

    // process all data in buffer, pass samples onto clients
    for (Sample* samp = nextSample(); samp; samp = nextSample()) {
        _rawSource.distribute(samp);
#ifdef DEBUG
        const Project* project = getDSMConfig()->getProject();
        assert(project);
        if (project->getName() == "test" &&
            getDSMId() == 1 && getSensorId() == 10) {
            DLOG(("%s: ",getName().c_str()) << ", samp=" <<
                string((const char*)samp->getConstVoidDataPtr(),samp->getDataByteLength()));
        }
#endif

#ifdef DEBUG
        nsamp++;
#endif
    }
#ifdef DEBUG
    cerr << "nsamp=" << nsamp << endl;
#endif

    return exhausted;
}

Sample* DSMSensor::readSample()
{
    Sample* samp = nextSample();

    while (!samp) {
        readBuffer();
        samp = nextSample();
    }
    return samp;
}

bool DSMSensor::receive(const Sample *samp)
{
    list<const Sample*> results;
    process(samp,results);
    _source.distribute(results);	// distribute does the freeReference
    return true;
}


void
DSMSensor::
trimUnparsed(SampleTag* stag, SampleT<float>* outs, int nparsed)
{
    float* fp = outs->getDataPtr();
    const vector<Variable*>& vars = stag->getVariables();
    int nd = 0;
    for (unsigned int iv = 0; iv < vars.size(); iv++)
    {
        Variable* var = vars[iv];
        for (unsigned int id = 0; id < var->getLength(); id++, nd++, fp++)
        {
            if (nd >= nparsed) *fp = floatNAN;  // this value not parsed
        }
    }
    // Trim the length of the sample to match the variables and lengths
    // in the SampleTag.
    outs->setDataLength(nd);
}


void DSMSensor::applyConversions(SampleTag* stag, SampleT<float>* outs,
                                 float* results)
{
    if (!stag || !outs)
        return;
    float* fp = outs->getDataPtr();
    const vector<Variable*>& vars = stag->getVariables();
    for (unsigned int iv = 0; iv < vars.size(); iv++)
    {
        Variable* var = vars[iv];
        float* start = fp;
        fp = var->convert(outs->getTimeTag(), start, 0, results);
        // Advance the results pointer as much as the values pointer.
        if (results)
        {
            results += (fp - start);
        }
    }
}


#ifdef IMPLEMENT_PROCESS
/**
 * Default implementation of process discards data.
 */
bool DSMSensor::process(const Sample* s, list<const Sample*>& result) throw()
{
    return false;
}
#endif

bool DSMSensor::MyDictionary::getTokenValue(const string& token,string& value) const
{
    if (token == "HEIGHT") {
        value = _sensor->getHeightString();
        return true;
    }
    // same as HEIGHT, but replace . with _ (for file names)
    if (token == "HEIGHT_") {
        string val =  _sensor->getHeightString();
        string::size_type nc = val.find('.');
        if (nc != string::npos) val[nc] = '_';
        value = val;
        return true;
    }
    if (token == "SUFFIX") {
        value = _sensor->getSuffix();
        return true;
    }
    // same as SUFFIX, but replace . with _ (for file names)
    if (token == "SUFFIX_") {
        string val =  _sensor->getSuffix();
        for (;;) {
            string::size_type nc = val.find('.');
            if (nc == string::npos) break;
            val[nc] = '_';
        }
        value = val;
        return true;
    }
    if (_sensor->getDSMConfig())
        return _sensor->getDSMConfig()->getTokenValue(token,value);
    return false;
}

void DSMSensor::printStatusHeader(std::ostream& ostr)
{
  static const char *glyph[] = {"\\","|","/","-"};
  static int anim=0;
  if (++anim == 4) anim=0;
  zebra = false;

  string dsm_name(getDSMConfig()->getName());
  string dsm_lctn(getDSMConfig()->getLocation());

    ostr <<
"<table id=status>\
<caption>"+dsm_lctn+" ("+dsm_name+") "+glyph[anim]+"</caption>\
<thead>\
<tr>\
<th>name</th>\
<th>samp/sec</th>\
<th>byte/sec</th>\
<th>min&nbsp;samp<br>length</th>\
<th>max&nbsp;samp<br>length</th>\
<th>bad<br>timetags</th>\
<th>extended&nbsp;status</th>\
</tr></thead>\
<tbody align=center>" << endl;	// default alignment in table body
}

void DSMSensor::printStatusTrailer(std::ostream& ostr)
{
    ostr << "</tbody></table>" << endl;
}
void DSMSensor::printStatus(std::ostream& ostr)
{
    string oe(zebra?"odd":"even");
    zebra = !zebra;
    bool warn = fabs(getObservedSamplingRate()) < 0.0001;

    ostr <<
        "<tr class=" << oe << "><td align=left>" <<
                getDeviceName() << ',' <<
		(getCatalogName().length() > 0 ?
			getCatalogName() : getClassName()) <<
		"</td>" << endl <<
	(warn ? "<td><font color=red><b>" : "<td>") <<
        fixed << setprecision(2) <<
		getObservedSamplingRate() <<
	(warn ? "</b></font></td>" : "</td>") << endl <<
        "<td>" << setprecision(0) <<
		getObservedDataRate() << "</td>" << endl <<
	"<td>" << getMinSampleLength() << "</td>" << endl <<
	"<td>" << getMaxSampleLength() << "</td>" << endl <<
	"<td>" << getBadTimeTagCount() << "</td>" << endl;
}

/* static */
const string DSMSensor::getClassName(const xercesc::DOMElement* node,
                                     const Project* project)
{
    XDOMElement xnode(node);
    const string& idref = xnode.getAttributeValue("IDREF");
    if (idref.length() > 0) {
	if (!project->getSensorCatalog())
	    throw n_u::InvalidParameterException(
		"sensor",
		"cannot find sensorcatalog for sensor with IDREF",
		idref);

	const xercesc::DOMElement* cnode =
            project->getSensorCatalog()->find(idref);
	if (!cnode)
		throw n_u::InvalidParameterException(
	    "sensor",
	    "sensorcatalog does not contain a sensor with ID",
	    idref);
	const string classattr = getClassName(cnode,project);
	if (classattr.length() > 0) return classattr;
    }
    return project->expandString(xnode.getAttributeValue("class"));
}

void DSMSensor::fromDOMElement(const xercesc::DOMElement* node)
{
    handledAttributes({"xml:base", "xmlns"});
    logNode(node);

    /*
     * The first time DSMSensor::fromDOMElement is called for
     * a DSMSensor, it is with the "actual" child DOMElement
     * within a <dsm> tag, not the DOMElement from the catalog entry.
     * We set the critical attributes (namely: id) from the actual
     * element, then parse the catalog element, then do a full parse
     * of the actual element.
     */
    std::string site_name;
    std::string aval;
    if (getAttribute(node, "site", aval))
    {
        // resolve the site name if specified.
        setSite(expandString(aval));
    }

    if (getSite())
    {
        setStation(getSite()->getNumber());
        /* Check the site as to whether we apply variable conversions. */
        setApplyVariableConversions(getSite()->getApplyVariableConversions());
    }

    // Set device name before scanning catalog entry,
    // so that error messages have something useful in the name.
    string dname;
    if (getAttribute(node, "devicename", dname) && !dname.empty()) {
        dname = expandString(dname);
        DLOG(("") << "setting device name to " << dname);
        setDeviceName(dname);
    }
    addContext(getName());

    // set id before scanning catalog entry
    std::string idstr;
    if (getAttribute(node, "id", idstr) && !idstr.empty())
    {
        setSensorId(asInt(idstr));
    }

    // scan catalog entry
    string idref;
    if (getAttribute(node, "IDREF", idref) && !idref.empty()) {
        const Project* project = getDSMConfig()->getProject();
        assert(project);
        if (!project->getSensorCatalog())
            throw n_u::InvalidParameterException(
                string("dsm") + ": " + getName(),
                "cannot find sensorcatalog for sensor with IDREF",
                idref);

        map<string,xercesc::DOMElement*>::const_iterator mi;

        const xercesc::DOMElement* cnode =
                        project->getSensorCatalog()->find(idref);
        if (!cnode)
                throw n_u::InvalidParameterException(
            string("dsm") + ": " + getName(),
            "sensorcatalog does not contain a sensor with ID",
            idref);
        // read catalog entry
        setCatalogName(idref);
        fromDOMElement(cnode);
    }

    if (getAttribute(node, "devicename", aval))
        setDeviceName(expandString(aval));

    if (getAttribute(node, "class", aval)) {
        if (getClassName().length() == 0)
            setClassName(aval);
        else if (getClassName() != aval)
            WLOG(("") << "class attribute=" << aval
                      << " does not match getClassName()=" << getClassName());
    }

    if (getAttribute(node, "location", aval))
        setLocation(expandString(aval));

    if (getAttribute(node, "latency", aval))
        setLatency(asFloat(expandString(aval)));
    if (getAttribute(node, "height", aval))
        setHeight(expandString(aval));
    if (getAttribute(node, "depth", aval))
        setDepth(expandString(aval));
    if (getAttribute(node, "suffix", aval))
        setSuffix(expandString(aval));

    if (getAttribute(node, "type", aval))
        setTypeName(aval);
    if (getAttribute(node, "duplicateIdOK", aval))
        setDuplicateIdOK(asBool(aval));
    if (getAttribute(node, "timeout", aval))
    {
        float fval{ asFloat(expandString(aval)) };
        setTimeoutMsecs((int)rint(fval * MSECS_PER_SEC));
    }
    if (getAttribute(node, "readonly", aval)) {
        if (asBool(aval))
            setDefaultMode((getDefaultMode() & ~O_ACCMODE) | O_RDONLY);
        else
            setDefaultMode((getDefaultMode() & ~O_ACCMODE) | O_RDWR);
    }
    if (getAttribute(node, "station", aval))
        setStation(asInt(aval));

    xercesc::DOMNode* child;
    for (child = node->getFirstChild(); child != 0;
        child=child->getNextSibling())
    {
        if (child->getNodeType() != xercesc::DOMNode::ELEMENT_NODE)
            continue;
        XDOMElement xchild((xercesc::DOMElement*) child);
        const string& elname = xchild.getNodeName();

        if (elname == "sample") {
            auto newtag{ std::unique_ptr<SampleTag>(new SampleTag(this)) };
            // add sensor name to any InvalidParameterException thrown by sample.
            try {
                newtag->fromDOMElement((xercesc::DOMElement*)child);
            }
            catch (const n_u::InvalidParameterException& e) {
                throw n_u::InvalidParameterException(getName() + ": " + e.what());
            }
            if (newtag->getSampleId() == 0)
                newtag->setSampleId(getSampleTags().size()+1);

            for (auto stag: _sampleTags) {
                // If a sample id matches a previous one (most likely
                // from the catalog) then update it from this DOMElement.
                if (stag->getSampleId() == newtag->getSampleId()) {
                    try {
                        stag->fromDOMElement((xercesc::DOMElement*)child);
                    }
                    catch (const n_u::InvalidParameterException& e) {
                        throw n_u::InvalidParameterException(getName() + ": " + e.what());
                    }
                    newtag.reset();
                    break;
                }
            }
            if (newtag) addSampleTag(newtag.release());
        }
        else if (elname == "parameter") {
            Parameter* parameter =
                Parameter::createParameter((xercesc::DOMElement*)child, &_dictionary);
            addParameter(parameter);
            if (parameter->getName() == "lag") {
                if ((parameter->getType() != Parameter::FLOAT_PARAM &&
                        parameter->getType() != Parameter::INT_PARAM) ||
                        parameter->getLength() != 1)
                    throw n_u::InvalidParameterException(getName(),"lag","must be float or integer of length 1");
                setLagSecs(parameter->getNumericValue(0));
            }
        }
        else if (elname == "calfile") {
            CalFile* cf = new CalFile();
            cf->setDSMSensor(this);
            cf->fromDOMElement((xercesc::DOMElement*)child);
            addCalFile(cf);
        }
    }

    _rawSampleTag.setSampleId(0);
    _rawSampleTag.setDSMSensor(this);

    // Update the sample tags with any changes to this DSMSensor.  Estimate
    // the rate of the raw sample as the max of the rates of the processed
    // samples.
    double rawRate = 0.0;
    for (auto stag: _sampleTags) {
        stag->setDSMSensor(this);
        rawRate = std::max(rawRate,stag->getRate());
    }
    _rawSampleTag.setRate(rawRate);
    _rawSource.addSampleTag(&_rawSampleTag);

    // previously at this point there was a check for unique sample IDs, but
    // that been moved into the actual validate() method.  it is not safe to
    // call validate() here also, because some sensors (at least A2DSensor)
    // may depend on validate() being called exactly once.

    // validate();
    {
        static LogContext lp(LOG_VERBOSE);
        if (lp.active())
        {
            LogMessage msg;
            msg << "after " << context() << ": ";
            for (auto stag: _sampleTags) {
                msg << stag->toString() << " ";
            }
        }
    }
}


void DSMSensor::validate()
{
    if (getDeviceName().length() == 0)
        throw n_u::InvalidParameterException(getName(),
            "no device name","");

    if (getSensorId() == 0)
        throw n_u::InvalidParameterException(
            getDSMConfig()->getName() + ": " + getName(),
            "id is zero","");

    // Check that sample ids are unique for this sensor.
    set<unsigned int> ids;
    for (auto stag: _sampleTags)
    {
        auto ins = ids.insert(stag->getId());
        if (!ins.second) {
            ostringstream ost;
            ost << stag->getDSMId() << ',' << stag->getSpSId();
            throw n_u::InvalidParameterException(
                getName(), "duplicate sample id", ost.str());
        }
    }
}


VariableIndex
DSMSensor::
findVariableIndex(const std::string& vprefix)
{
    unsigned int vlen = vprefix.length();
    list<SampleTag*>& tags = getSampleTags();

    list<SampleTag*>::const_iterator ti;
    VariableIndex idx;

    for (ti = tags.begin(); !idx && ti != tags.end(); ++ti)
    {
        SampleTag* tag = *ti;
        const vector<Variable*>& vars = tag->getVariables();
        vector<Variable*>::const_iterator vi = vars.begin();
        for (unsigned int i = 0; vi != vars.end(); ++vi, ++i)
        {
            Variable* var = *vi;
            if (var->getName().substr(0, vlen) == vprefix)
            {
                idx = VariableIndex(var, i);
                break;
            }
        }
    }
    return idx;
}


xercesc::DOMElement* DSMSensor::toDOMParent(xercesc::DOMElement* parent,
                                            bool complete) const
{
    xercesc::DOMElement* elem = 0;
    if (complete) {
        elem = parent->getOwnerDocument()->createElementNS(
            DOMable::getNamespaceURI(),
            (const XMLCh*)XMLStringConverter("sensor"));
        parent->appendChild(elem);
        return toDOMElement(elem,complete);
    }
    else {
        for (SampleTagIterator sti = getSampleTagIterator(); sti.hasNext(); ) {
            const SampleTag* tag = sti.next();
            elem = tag->toDOMParent(parent,complete);
        }
    }
    return elem;
}

xercesc::DOMElement* DSMSensor::toDOMElement(xercesc::DOMElement* /* elem */,
                                             bool /* complete */) const
{
    return 0; // not supported yet
}

/* static */
Looper* DSMSensor::_looper = 0;

/* static */
n_u::Mutex DSMSensor::_looperMutex;

/* static */
Looper* DSMSensor::getLooper()
{
    n_u::Synchronized autosync(_looperMutex);
    if (!_looper) {
        _looper = new Looper();
        try {
            _looper->setThreadScheduler(n_u::Thread::NU_THREAD_RR,51);
        }
        catch(const n_u::Exception& e) {
            n_u::Logger::getInstance()->log(LOG_WARNING,
                "DSMSensor Looper thread cannot be realtime %s: %s",
                n_u::Thread::getPolicyString(n_u::Thread::NU_THREAD_RR).c_str(),
                e.what());
        }
    }
    return _looper;
}

/* static */
void DSMSensor::deleteLooper()
{
    n_u::Synchronized autosync(_looperMutex);
    if (_looper) {
        delete _looper;
        _looper = 0;
    }
}


