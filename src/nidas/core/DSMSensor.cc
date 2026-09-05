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
#include "Sample.h"

#include "SamplePool.h"
#include "CalFile.h"

#include <nidas/util/Logger.h>
#include <nidas/util/util.h>

#include <cmath>
#include <cfloat>
#include <iostream>
#include <iomanip>
#include <string>
#include <memory>

using namespace std;
using namespace nidas::core;

namespace n_u = nidas::util;

CustomMetaData::iterator MetaDataBase::findCustomMetaData(const std::string& rFirst)
{
    VLOG(("MetaDataBase::findCustomMetaData(): starting at the beginning..."));
    CustomMetaData::iterator iter = customMetaData.begin();
    while (iter != customMetaData.end()) {
        VLOG(("MetaDataBase::findCustomMetaData(): testing customMetaData item ")
              << iter->first << " against " << rFirst);
        if (iter->first == rFirst) {
            VLOG(("MetaDataBase::findCustomMetaData(): Found a match!"));
            break;
        }
        iter++;
    }
    if (iter == customMetaData.end()) {
        VLOG(("MetaDataBase::findCustomMetaData(): No match found!"));
    }

    return iter;
}

void MetaDataBase::addMetaDataItem(const MetaDataItem& rItem)
{
    if (findCustomMetaData(rItem.first) == customMetaData.end()) {
        VLOG(("MetaDataBase::addMetaDataItem(): Didn't find ")
              << rItem.first << " so adding it.");
        customMetaData.push_back(rItem);
    }
    else {
        VLOG(("MetaDataBase::addMetaDataItem(): ")
              << rItem.first << " already exists so not adding it.");
    }
}

/* static */
const dsm_time_t DSMSensor::DEFAULT_QC_CHECK_PERIOD =
    (uint64_t)USECS_PER_SEC * 3660; //SECS_PER_HOUR;

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
    _scanner(0),_dsm(0),_id(0),
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
    _nTimeouts(0),
	_lag(0),_station(-1),
	_manufMetaData(), _configMetaData(),
	_nSamplesToTest(DEFAULT_NUM_SAMPLES_TO_TEST),
	_nSamplesRead(0),
    _nSamplesTested(0), _nSamplesGood(0),
    _qcCheckPeriod(DEFAULT_QC_CHECK_PERIOD),
	_lastSampleSurveillance(0),
    _sensorState(SENSOR_CLOSED),
    _prefix(),
    _prefixEnabled(false)
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
    throw(n_u::InvalidParameterException)
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

void DSMSensor::removeSampleTag(SampleTag* val) throw()
{
    list<SampleTag*>::iterator si = find(_sampleTags.begin(),_sampleTags.end(),val);
    if (si != _sampleTags.end()) {
        _sampleTags.erase(si);
        _source.removeSampleTag(val);
    }
    delete val;
}

void DSMSensor::addSampleTag(const SampleTag*)
    throw(n_u::InvalidParameterException)
{
    assert(false);
}

void DSMSensor::removeSampleTag(const SampleTag*) throw()
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
    if (_dsm) return _dsm->getSite();
    return 0;
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
    if (_heightString.length() > 0)
        setFullSuffix(_suffix + string(".") + _heightString);
    else if (_depthString.length() > 0)
        setFullSuffix(_suffix + string(".") + _depthString);
    else
        setFullSuffix(_suffix);
}

void DSMSensor::setHeight(const std::string& val)
{
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
	throw(n_u::IOException,n_u::InvalidParameterException)
{
    if (!_iodev) _iodev = buildIODevice();
    _iodev->setName(getDeviceName());

    NLOG(("opening: ") << getDeviceName());
    _iodev->open(flags);

    DLOG(("Building and initializing SampleScanner object"));
    if (!_scanner) _scanner = buildSampleScanner();
    _scanner->init();
    DLOG(("SampleScanner object built and initialized"));
}

void DSMSensor::close() throw(n_u::IOException)
{
    NLOG(("closing: %s, #timeouts=%d", getDeviceName().c_str(), getTimeoutCount()));
    if (_iodev) _iodev->close();

    setSensorState(SENSOR_CLOSED);
}

void DSMSensor::init() throw(n_u::InvalidParameterException)
{
    DLOG(("Calling DSMSensor::init() as subclass does not override."));
}


Sample* DSMSensor::nextSample()
{
    Sample* sample = _scanner->nextSample(this);

    // no prefix checking needed if it has never been set.
    if (_prefixEnabled)
    {
        sample = prefixSample(sample);
    }
    return sample;
}


Sample* DSMSensor::prefixSample(Sample* sample)
{
    // Inject a prefix if specified, rather than trying to modify the
    // complicated SampleScanner state and logic, except that it requires
    // possibly switching samples to make room for the prefix.  Make sure this
    // uses a copy of the prefix rather than the _prefix member, so it can't
    // change while being used to calculate sample lengths.  The other option
    // is to lock the looper mutex.
    std::string prefix = getPrefix();
    if (sample && prefix.size())
    {
        unsigned int newlen = sample->getDataLength() + prefix.size();
        Sample* newsamp = sample;
        VLOG(("") << "prefix " << prefix << ": sample has alloc length "
                  << sample->getAllocLength() 
                  << ", data length " << sample->getDataLength()
                  << ", need " << newlen
                  << ", data='" << n_u::addBackslashSequences(std::string((char*)sample->getVoidDataPtr(),
                                                              (char*)sample->getVoidDataPtr() + sample->getDataLength()))
                  << "'");
        if (sample->getAllocLength() < newlen)
        {
            // Replace this sample with one possibly from a different pool.
            newsamp = getSample<char>(newlen);
            newsamp->setTimeTag(sample->getTimeTag());
            newsamp->setId(sample->getId());
        }
        // First make room for the prefix in case the same sample is being
        // used, then copy in the prefix.  Use the mem functions because the
        // prefix string, like the prompt, could contain null bytes.
        memmove((char *)newsamp->getVoidDataPtr() + prefix.size(),
                (char *)sample->getVoidDataPtr(), sample->getDataLength());
        memcpy((char *)newsamp->getVoidDataPtr(), prefix.data(), prefix.size());
        newsamp->setDataLength(newlen);
        VLOG(("") << "new data after prefix inserted: '"
                  << n_u::addBackslashSequences(std::string((char*)newsamp->getVoidDataPtr(),
                                                            (char*)newsamp->getVoidDataPtr() + newlen))
                  << "'");
        if (sample != newsamp)
        {
            sample->freeReference();
            sample = newsamp;
        }
    }
    return sample;
}


bool DSMSensor::readSamples() throw(nidas::util::IOException)
{
    bool exhausted = readBuffer();

    // Disable sensor health checks, until we can actually do something useful
    // with them, if ever.  See JIRA DSM3 issue.
    if (false)
        testCheckHealthInterval();

    // process all data in buffer, pass samples onto clients
    for (Sample* samp = nextSample(); samp; samp = nextSample()) {
        _rawSource.distribute(samp);
        _nSamplesRead++;

        // These messages are too verbose for INFO logging, and probably not
        // useful for debug either, especially since the interval is based on
        // sample number rather than time, and sensors can report at vastly
        // different rates.
        if (false && (_nSamplesRead > 0) && ((_nSamplesRead %100) == 0)) {
        	ILOG(("%s:%s collected %lu samples...", getName().c_str(), getClassName().c_str(), _nSamplesRead));
        }
    }

    return exhausted;
}

bool DSMSensor::receive(const Sample *samp) throw()
{
    list<const Sample*> results;
    bool processOK = process(samp,results);
    checkSensorHealth(processOK);
    if (!processOK)
        DLOG(("") << getName() << ":" << getClassName() << ": raw sample contents: " << (char*)samp->getConstVoidDataPtr());

    _source.distribute(results);	// distribute does the freeReference
    return true;
}


void
DSMSensor::
setPrefix(const std::string& prefix)
{
    n_u::Synchronized autosync(_looperMutex);
    _prefix = prefix;
    _prefixEnabled = true;
}


std::string
DSMSensor::
getPrefix()
{
    n_u::Synchronized autosync(_looperMutex);
    return _prefix;
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

void DSMSensor::printStatusHeader(std::ostream& ostr) throw()
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
<tr>\
<th>name</th>\
<th>samp/sec</th>\
<th>byte/sec</th>\
<th>min&nbsp;samp<br>length</th>\
<th>max&nbsp;samp<br>length</th>\
<th>bad<br>timetags</th>\
<th>extended&nbsp;status</th>\
<tbody align=center>" << endl;	// default alignment in table body
}

void DSMSensor::printStatusTrailer(std::ostream& ostr) throw()
{
    ostr << "</tbody></table>" << endl;
}
void DSMSensor::printStatus(std::ostream& ostr) throw()
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

void DSMSensor::calcNumQCSamples(double sampleRate)
{
	double secsPerSample = 1/sampleRate;
	assert(secsPerSample != doubleNAN && secsPerSample > 0.0 && secsPerSample != DBL_MAX);
	uint32_t samplesPerQCPeriod = _qcCheckPeriod/(secsPerSample * USECS_PER_SEC);
    DLOG(("") << getName() << ":" << getClassName() << " - samplesPerQCPeriod = " << samplesPerQCPeriod);
	uint32_t numQCSamples = std::max((uint32_t)1, samplesPerQCPeriod/1000);
	DLOG(("") << getName() << ":" << getClassName() << " - numQCSamples = " << numQCSamples);
	setNumQCSamples(numQCSamples);
}

std::string DSMSensor::sensorStateToString(const DSM_SENSOR_STATE sensorState) const
{
	switch (sensorState) {
		case SENSOR_CLOSED:
			return std::string("SENSOR_CLOSED");
			break;

		case SENSOR_OPEN:
			return std::string("SENSOR_OPEN");
			break;

        case SENSOR_CONFIGURING:
            return std::string("SENSOR_CONFIGURING");
            break;

        case SENSOR_CONFIGURE_SUCCEEDED:
            return std::string("SENSOR_CONFIGURE_SUCCEEDED");
            break;

        case SENSOR_CONFIGURE_FAILED:
            return std::string("SENSOR_CONFIGURE_FAILED");
            break;

		case SENSOR_ACTIVE:
			return std::string("SENSOR_ACTIVE");
			break;

		case SENSOR_CHECKING_HEALTH:
			return std::string("SENSOR_CHECKING_HEALTH");
			break;

		case SENSOR_UNHEALTHY:
			return std::string("SENSOR_UNHEALTHY");
			break;

		case SENSOR_REQUEST_RESTART:
			return std::string("SENSOR_REQUEST_RESTART");
			break;

		case SENSOR_HEALTHY:
			return std::string("SENSOR_HEALTHY");
			break;

		default:
			break;

	}

	return std::string("");
}

void DSMSensor::testCheckHealthInterval(bool sensorStartup)
{
	if (getSensorState() != SENSOR_CHECKING_HEALTH) {
		dsm_time_t now = nidas::util::getSystemTime();
		if (sensorStartup || (now -_lastSampleSurveillance) > DEFAULT_QC_CHECK_PERIOD) {
			ILOG(("Starting health check on ") << getName() << ":" << getClassName());
			setSensorState(SENSOR_CHECKING_HEALTH);
			_lastSampleSurveillance = now;
            _nSamplesTested = 0;
            _nSamplesGood = 0;
			addRawSampleClient(this);
		}
	}
}

void DSMSensor::checkSensorHealth(bool processSuccess)
{
    if (getSensorState() == SENSOR_CHECKING_HEALTH) {
		++_nSamplesTested;
    	if (processSuccess)
    		++_nSamplesGood;
    	testHealthCheckDone();
    }
}

void DSMSensor::testHealthCheckDone()
{
	bool retVal = _nSamplesTested >= _nSamplesToTest;
	if (retVal)
	{
		removeRawSampleClient(this);
		float pctGood = _nSamplesGood/_nSamplesTested*100;
		if (pctGood > 90.0) {
			setSensorState((SENSOR_HEALTHY));
		}
		else if (pctGood > 75.0) {
			setSensorState((SENSOR_UNHEALTHY));
		}

		else {
			setSensorState(SENSOR_REQUEST_RESTART);
		}

		ILOG(("Health check complete for ") << getName() << ":" << getClassName() << ". Score: " << pctGood);
	}
}

/* static */
const string DSMSensor::getClassName(const xercesc::DOMElement* node,const Project* project)
    throw(n_u::InvalidParameterException)
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
    throw(n_u::InvalidParameterException)
{
    VLOG(("DSMSensor::fromDOMElement(): entry..."));

    /*
     * The first time DSMSensor::fromDOMElement is called for
     * a DSMSensor, it is with the "actual" child DOMElement
     * within a <dsm> tag, not the DOMElement from the catalog entry.
     * We set the critical attributes (namely: id) from the actual
     * element, then parse the catalog element, then do a full parse
     * of the actual element.
     */
    if (getSite())
    {
        setStation(getSite()->getNumber());
        /* Check the site as to whether we apply variable conversions. */
        setApplyVariableConversions(getSite()->getApplyVariableConversions());
    }

    XDOMElement xnode(node);

    // Set device name before scanning catalog entry,
    // so that error messages have something useful in the name.
    string dname = expandString(xnode.getAttributeValue("devicename"));
    if (dname.length() > 0) {
        VLOG(("DSMSensor::fromDOMElement() aname==\"devicename\": ") << dname);
        setDeviceName(dname);
    }

    // set id before scanning catalog entry
    const string& idstr = xnode.getAttributeValue("id");
    if (idstr.length() > 0) {
        istringstream ist(idstr);
        // If you unset the dec flag, then a leading '0' means
        // octal, and 0x means hex.
        ist.unsetf(ios::dec);
        unsigned int val;
        ist >> val;
        if (ist.fail())
            throw n_u::InvalidParameterException(
                    string("sensor on dsm ") + getDSMConfig()->getName(),
                    "id",idstr);
        setSensorId(val);
    }
    const string& idref = xnode.getAttributeValue("IDREF");
    // scan catalog entry
    if (idref.length() > 0) {
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
        VLOG(("DSMSensor::fromDOMElement(): calling myself recursively and virtually????"));
        fromDOMElement(cnode);
    }

    // Now the main entry attributes will override the catalog entry attributes
    if(node->hasAttributes()) {
        // clear suffix attribute for 2nd call wherein it no longer exists
        //  - supports config editor, there may be other "blankable" attributes
        // setSuffix("");

        // get all the attributes of the node
        xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
        int nSize = pAttributes->getLength();
        for(int i=0;i<nSize;++i) {
            XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
            const string& aname = attr.getName();
            const string aval = expandString(attr.getValue());
            // get attribute name
            if (aname == "devicename") {
                VLOG(("DSMSensor::fromDOMElement() aname==\"devicename\": ") << aval);
                setDeviceName(aval);
            }
            else if (aname == "id");	// already scanned
            else if (aname == "IDREF");		// already parsed
            else if (aname == "class") {
                if (getClassName().length() == 0)
                    setClassName(aval);
            else if (getClassName() != aval)
                n_u::Logger::getInstance()->log(LOG_WARNING,
                    "class attribute=%s does not match getClassName()=%s\n",
                aval.c_str(),getClassName().c_str());
            }
            else if (aname == "location") setLocation(aval);
            else if (aname == "latency") {
                istringstream ist(aval);
                float val;
                ist >> val;
                if (ist.fail())
                    throw n_u::InvalidParameterException("sensor",
                        aname,aval);
                setLatency(val);
            }
            else if (aname == "height")
                setHeight(aval);
            else if (aname == "depth")
                setDepth(aval);
            else if (aname == "suffix")
                setSuffix(aval);
            else if (aname == "type") setTypeName(aval);
            else if (aname == "duplicateIdOK") {
                istringstream ist(aval);
                bool val;
                ist >> boolalpha >> val;
                if (ist.fail()) {
                    ist.clear();
                    ist >> noboolalpha >> val;
                    if (ist.fail())
                    throw n_u::InvalidParameterException(
                        getName(),aname,aval);
                }
                setDuplicateIdOK(val);
            }
            else if (aname == "timeout") {
                istringstream ist(aval);
                float val;
                ist >> val;
                if (ist.fail()) throw n_u::InvalidParameterException(getName(),aname,aval);
                setTimeoutMsecs((int)rint(val * MSECS_PER_SEC));
            }
            else if (aname == "readonly") {
                istringstream ist(aval);
                bool val;
                ist >> boolalpha >> val;
                if (ist.fail()) {
                    ist.clear();
                    ist >> noboolalpha >> val;
                    if (ist.fail())
                    throw n_u::InvalidParameterException(
                        getName(),aname,aval);
                }
                if (val) setDefaultMode((getDefaultMode() & ~O_ACCMODE) | O_RDONLY);
                else setDefaultMode((getDefaultMode() & ~O_ACCMODE) | O_RDWR);
            }
            else if (aname == "station") {
                istringstream ist(aval);
                int val;
                ist >> val;
                if (ist.fail()) throw n_u::InvalidParameterException(getName(),aname,aval);
                setStation(val);
            }
            else if (aname == "xml:base" || aname == "xmlns") {}
        }
    }

    xercesc::DOMNode* child;
    for (child = node->getFirstChild(); child != 0;
	    child=child->getNextSibling())
    {
        if (child->getNodeType() != xercesc::DOMNode::ELEMENT_NODE) continue;
        XDOMElement xchild((xercesc::DOMElement*) child);
        const string& elname = xchild.getNodeName();

        if (elname == "sample") {
            std::unique_ptr<SampleTag> newtag(new SampleTag(this));
            // add sensor name to any InvalidParameterException thrown by sample.
            try {
                newtag->fromDOMElement((xercesc::DOMElement*)child);
            }
            catch (const n_u::InvalidParameterException& e) {
                throw n_u::InvalidParameterException(getName() + ": " + e.what());
            }
            if (newtag->getSampleId() == 0)
                newtag->setSampleId(static_cast<unsigned int>(getSampleTags().size()+1));

            list<SampleTag*>::const_iterator si = _sampleTags.begin();
            for ( ; si != _sampleTags.end(); ++si) {
                SampleTag* stag = *si;
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
            if (newtag.get())
                addSampleTag(newtag.release());
        }
        else if (elname == "parameter") {
            Parameter* parameter =
                    Parameter::createParameter((xercesc::DOMElement*)child,&_dictionary);
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
    _rawSampleTag.setSensorId(getSensorId());
    _rawSampleTag.setDSMId(getDSMId());
    _rawSampleTag.setDSMSensor(this);
    _rawSampleTag.setDSMConfig(getDSMConfig());
    _rawSampleTag.setSuffix(getFullSuffix());
    _rawSampleTag.setStation(getStation());

    // sensors in the catalog may not have any sample tags
    // so at this point it is OK if sampleTags.size() == 0.

    // Check that sample ids are unique for this sensor.
    // Estimate the rate of the raw sample as the max of
    // the rates of the processed samples.
    double rawRate = 0.0;
    set<unsigned int> ids;
    list<SampleTag*>::const_iterator si = _sampleTags.begin();
    for ( ; si != _sampleTags.end(); ++si) {
        SampleTag* stag = *si;

        stag->setSensorId(getSensorId());
        stag->setSuffix(getFullSuffix());

        if (getSensorId() == 0) throw n_u::InvalidParameterException(
        getName(),"id","zero or missing");

        pair<set<unsigned int>::const_iterator,bool> ins =
            ids.insert(stag->getId());
        if (!ins.second) {
            ostringstream ost;
            ost << stag->getDSMId() << ',' << stag->getSpSId();
            throw n_u::InvalidParameterException(
                getName(),"duplicate sample id", ost.str());
        }
        rawRate = std::max(rawRate,stag->getRate());
    }

    _rawSampleTag.setRate(rawRate);
    _rawSource.addSampleTag(&_rawSampleTag);

    // Not useful if sensor health checks are disabled.
    if (false)
        calcNumQCSamples(rawRate);

#ifdef DEBUG
    cerr << getName() << ", suffix=" << getSuffix() << ": ";
    VariableIterator vi = getVariableIterator();
    for ( ; vi.hasNext(); ) {
        const Variable* var = vi.next();
        cerr << var->getName() << ',';
    }
    cerr << endl;
#endif

    VLOG(("DSMSensor::fromDOMElement(): exit..."));
}

void DSMSensor::validate() throw(nidas::util::InvalidParameterException)
{
    if (getDeviceName().length() == 0)
	throw n_u::InvalidParameterException(getName(),
            "no device name","");

    if (getSensorId() == 0)
	throw n_u::InvalidParameterException(
	    getDSMConfig()->getName() + ": " + getName(),
	    "id is zero","");
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


xercesc::DOMElement* DSMSensor::toDOMParent(xercesc::DOMElement* parent,bool complete) const
    throw(xercesc::DOMException)
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

xercesc::DOMElement* DSMSensor::toDOMElement(xercesc::DOMElement* /* elem */,bool /* complete */) const
    throw(xercesc::DOMException)
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


