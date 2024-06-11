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

#include <nidas/Config.h>   // HAVE_BLUETOOTH_RFCOMM_H

#include "CharacterSensor.h"
#include "AsciiSscanf.h"
#include "IODevice.h"
#include "TCPSocketIODevice.h"
#include "UDPSocketIODevice.h"
#include "BluetoothRFCommSocketIODevice.h"
#include "Variable.h"
#include "Parameter.h"
#include "UnixIODevice.h"
#include "TimetagAdjuster.h"

#include <nidas/util/Logger.h>

#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;
using namespace nidas::core;

namespace n_u = nidas::util;

CharacterSensor::CharacterSensor():
    _ttadjusters(),
    _messageSeparator(),
    _separatorAtEOM(true),
    _messageLength(0),
    _prompts(),
    _promptString(),
    _promptRate(0.0),
    _promptOffset(0.0),
    _sscanfers(),
    _nextSscanfer(),
    _maxScanfFields(0),
    _scanfFailures(0),
    _scanfPartials(0),
    _prompted(false),
    _initString(),_emptyString()
{
}

CharacterSensor::~CharacterSensor() {
    std::list<AsciiSscanf*>::iterator si;
    for (si = _sscanfers.begin(); si != _sscanfers.end(); ++si) {
        AsciiSscanf* sscanf = *si;
	delete sscanf;
    }

    for (map<const SampleTag*, TimetagAdjuster*>::const_iterator tti =
            _ttadjusters.begin();
	tti != _ttadjusters.end(); ++tti) {
        TimetagAdjuster* tta = tti->second;
        if (tta) {
            tta->log(nidas::util::LOGGER_INFO, this);
        }
        delete tta;
    }
}

void CharacterSensor::setMessageParameters(unsigned int len,
                                           const std::string& sep, bool eom)
{
    if (sep.length() == 0 && len == 0)
        throw n_u::InvalidParameterException(getName(),"message","no message separator and message length equals 0");
    _messageSeparator = n_u::replaceBackslashSequences(sep);
    _separatorAtEOM = eom;
    _messageLength = len;
    if (getSampleScanner()) getSampleScanner()->setMessageParameters(len,_messageSeparator,eom);
}

void CharacterSensor::open(int flags)
{
    DSMSensor::open(flags);
    // Cannot sendInitString yet.
    // DSMSerialSensors are not yet fully initialized,
    // so it must be done in a derived class open(), i.e.
    // DSMSerialSensor.
}

void CharacterSensor::sendInitString()
{
    if (getInitString().length() > 0) {
	DLOG(("sending init string '") << getInitString()
	     << "' to " << getDeviceName());
        string newstr = n_u::replaceBackslashSequences(getInitString());
        write(newstr.c_str(),newstr.length());
    }
}

IODevice* CharacterSensor::buildIODevice()
{
    if (getDeviceName().find("inet:") == 0)
        return new TCPSocketIODevice();
    else if (getDeviceName().find("sock:") == 0)
        return new TCPSocketIODevice();
    else if (getDeviceName().find("usock:") == 0)
        return new UDPSocketIODevice();
#ifdef HAVE_BLUETOOTH_RFCOMM_H
    else if (getDeviceName().find("btspp:") == 0)
        return new BluetoothRFCommSocketIODevice();
#endif
    else return new UnixIODevice();
}

SampleScanner* CharacterSensor::buildSampleScanner()
{
    SampleScanner* scanr;
    if (getDeviceName().find("usock:") == 0) {
        DatagramSampleScanner* dscanr;
        scanr = dscanr = new DatagramSampleScanner();
        dscanr->setNullTerminate(doesAsciiSscanfs());
    }
    else {
        MessageStreamScanner* mscanr;
        scanr = mscanr = new MessageStreamScanner();
        mscanr->setNullTerminate(doesAsciiSscanfs());

        scanr->setMessageParameters(getMessageLength(),
            getMessageSeparator(),getMessageSeparatorAtEOM());
    }
    return scanr;
}

bool CharacterSensor::doesAsciiSscanfs()
{
    for (SampleTagIterator si = getSampleTagIterator(); si.hasNext(); ) {
	const SampleTag* tag = si.next();
	const string& sfmt = tag->getScanfFormat();
	if (sfmt.length() > 0) return true;
    }
    return false;
}

void CharacterSensor::init()
{
    DSMSensor::init();

    const list<SampleTag*>& tags = getSampleTags();
    list<SampleTag*>::const_iterator si = tags.begin();

    for ( ; si != tags.end(); ++si) {
	SampleTag* tag = *si;
	const string& sfmt = tag->getScanfFormat();
	if (sfmt.length() > 0) {
	    AsciiSscanf* sscanf = new AsciiSscanf();
	    try {
	       sscanf->setFormat(n_u::replaceBackslashSequences(sfmt));
	    }
	    catch (n_u::ParseException& pe) {
		throw n_u::InvalidParameterException(getName(),
		       "setScanfFormat",pe.what());
	    }

            int nd = 0;
            for (unsigned int iv = 0; iv < tag->getVariables().size(); iv++) {
                const Variable* var = tag->getVariables()[iv];
                nd += var->getLength();
            }

	    sscanf->setSampleTag(tag);
	    _sscanfers.push_back(sscanf);
	    _maxScanfFields = std::max(std::max(_maxScanfFields,sscanf->getNumberOfFields()),nd);
	}

        if (tag->getTimetagAdjust() > 0.0 && tag->getRate() > 0.0) {
            _ttadjusters[tag] = new TimetagAdjuster(tag->getId(), tag->getRate());
        }
    }

    if (!_sscanfers.empty()) _nextSscanfer = _sscanfers.begin();
    validateSscanfs();
}

void CharacterSensor::validateSscanfs()
{
    /* default implementation */
    std::list<AsciiSscanf*>::const_iterator si = _sscanfers.begin();
    for ( ; si != _sscanfers.end(); ++si) {
        AsciiSscanf* sscanf = *si;
        const SampleTag* tag = sscanf->getSampleTag();

        int nd = 0;
        for (unsigned int iv = 0; iv < tag->getVariables().size(); iv++) {
            const Variable* var = tag->getVariables()[iv];
            nd += var->getLength();
        }

        /* could turn this into an InvalidParameterException at some point */
        if (sscanf->getNumberOfFields() < nd)
            n_u::Logger::getInstance()->log(LOG_WARNING,
                "%s: number of scanf fields (%d) is less than the number of variable values (%d)",
                getName().c_str(),sscanf->getNumberOfFields(),nd);
    }
}

void CharacterSensor::fromDOMElement(const xercesc::DOMElement* node)
{
    DSMSensor::fromDOMElement(node);

    std::string aval;
    if (getAttribute(node, "init_string", aval))
        setInitString(aval);

    // accepted but no longer used.
    handledAttributes({"nullterm"});

    xercesc::DOMNode* child;
    for (child = node->getFirstChild(); child != 0;
	    child=child->getNextSibling())
    {
	if (child->getNodeType() != xercesc::DOMNode::ELEMENT_NODE) continue;
	XDOMElement xchild((xercesc::DOMElement*) child);
	const string& elname = xchild.getNodeName();

	if (elname == "message") {
	    const string& str = xchild.getAttributeValue("position");
            bool eom = true;
	    if (str == "beg") eom = false;
	    else if (str == "end") eom = true;
	    else if (str != "") throw n_u::InvalidParameterException
			(getName(),"messageSeparator position",str);

	    istringstream ist(xchild.getAttributeValue("length"));
	    unsigned int len;
	    ist >> len;
	    if (ist.fail())
		throw n_u::InvalidParameterException(getName(),
		    "message length", xchild.getAttributeValue("length"));

            // The signature of this method indicates that it can throw IOException,
            // but it won't actually, since the device isn't opened yet.
            try {
                setMessageParameters(len,xchild.getAttributeValue("separator"),eom);
            }
            catch(const n_u::IOException& e) {
                throw n_u::InvalidParameterException(e.what());
            }
	}
	else if (elname == "prompt") {

            xercesc::DOMNamedNodeMap *promptAttrs = child->getAttributes();
            int nSize = promptAttrs->getLength();

            for(int i=0;i<nSize;++i) {
                XDOMAttr attr((xercesc::DOMAttr*) promptAttrs->item(i));
                const string& aname = attr.getName();
                const string aval = attr.getValue();
                // get attribute name
                if (aname == "string") {
                    setPromptString(aval);
                }
                else if (aname == "rate") {
                    istringstream ist(aval);
                    double rate;
                    ist >> rate;
                    if (ist.fail())
                        throw n_u::InvalidParameterException(getName(),
                            "prompt rate", aval);

                    if (rate < 0.0)
                        throw n_u::InvalidParameterException
                                (getName(),"prompt rate", aval);
                    setPromptRate(rate);
                }
                else if (aname == "offset") {
                    istringstream ist(aval);
                    double offset;
                    ist >> offset;
                    if (ist.fail())
                        throw n_u::InvalidParameterException(getName(),
                            "prompt offset", aval);

                    if (offset < 0.0)
                        throw n_u::InvalidParameterException
                                (getName(),"prompt offset", aval);
                    setPromptOffset(offset);
                }
            }
	}
    }
}

void CharacterSensor::validate()
{
    DSMSensor::validate();

    if (!getPromptString().empty()) addPrompt(getPromptString(),
            getPromptRate(), getPromptOffset());

    /* determine if any of the samples have associated prompts */
    const list<SampleTag*>& tags = getSampleTags();
    list<SampleTag*>::const_iterator si = tags.begin();
    for ( ; si != tags.end(); ++si) {
	SampleTag* samp = *si;
	if (samp->getRate() == 0.0 && getPromptRate() > 0.0)
	    samp->setRate(getPromptRate());
	if (!samp->getPromptString().empty()) {
	    addPrompt(samp->getPromptString(),
                    samp->getRate(), samp->getPromptOffset());
	    if (samp->getRate() <= 0.0)
	        throw n_u::InvalidParameterException(
		    getName() + " prompted sensor has sample rate <= 0.0");
        }
    }

    _prompted = !getPrompts().empty();
}


int
CharacterSensor::
scanSample(AsciiSscanf* sscanf, const char* inputstr, float* data_ptr)
{
    return sscanf->sscanf(inputstr, data_ptr, sscanf->getNumberOfFields());
}



SampleT<float>*
CharacterSensor::
searchSampleScanners(const Sample* samp, SampleTag** stag_out) throw()
{
    // Note: sscanfers can be empty here, if a CharacterSensor was configured
    // with no samples, and hence no scanf strings.  For example,
    // a differential GPS, where nidas is supposed to take the
    // data for later use, but doesn't (currently) parse it.
    if (_sscanfers.empty())
    {
        return 0;
    }
    assert(samp->getType() == CHAR_ST);

    const char* inputstr = (const char*)samp->getConstVoidDataPtr();
    int slen = samp->getDataByteLength();

    // if sample is not null terminated, create a new null-terminated sample
    if (inputstr[slen-1] != '\0')
    {
        SampleT<char>* newsamp = getSample<char>(slen+1);
        newsamp->setTimeTag(samp->getTimeTag());
        newsamp->setId(samp->getId());
        char* newstr = (char*)newsamp->getConstVoidDataPtr();
        ::memcpy(newstr,inputstr,slen);
        newstr[slen] = '\0';

        SampleT<float>* res = searchSampleScanners(newsamp, stag_out);
        newsamp->freeReference();
        return res;
    }

    SampleT<float>* outs = getSample<float>(_maxScanfFields);
    // Output sample always defaults to time of raw sample.
    outs->setTimeTag(samp->getTimeTag());

    SampleTag* stag = 0;
    int nparsed = 0;
    unsigned int ntry = 0;
    AsciiSscanf* sscanf = 0;
    list<AsciiSscanf*>::const_iterator checkdone = _nextSscanfer;
    for ( ; ; ntry++)
    {
        sscanf = *_nextSscanfer;
        nparsed = scanSample(sscanf, inputstr, outs->getDataPtr());
        if (++_nextSscanfer == _sscanfers.end())
            _nextSscanfer = _sscanfers.begin();
        if (nparsed > 0) {
            stag = sscanf->getSampleTag();
            outs->setId(stag->getId());
            if (nparsed != sscanf->getNumberOfFields()) _scanfPartials++;
            break;
        }
        if (_nextSscanfer == checkdone) break;
    }
    static n_u::LogContext lp(LOG_DEBUG);

    if (lp.active() && nparsed != sscanf->getNumberOfFields())
    {
        n_u::LogMessage msg;
        msg << (nparsed > 0 ? "partial" : "failed")
            << " scanf; tried " << (ntry+(nparsed>0))
            << "/" << _sscanfers.size() << " formats.\n";
        msg << "input:'" << inputstr << "'\n"
            << "last format tried: " << (sscanf ? sscanf->getFormat() : "X")
            << "\n";
        msg << "; nparsed=" << nparsed
            << "; scanfFailures=" << _scanfFailures
            << "; scanfPartials=" << _scanfPartials;
        lp.log(msg);
    }

    if (!nparsed)
    {
        _scanfFailures++;
        outs->freeReference();  // remember!
        return 0;               // no sample
    }

    // Fill and trim for unparsed values.
    trimUnparsed(stag, outs, nparsed);
    if (stag_out)
        *stag_out = stag;
    return outs;
}

bool
CharacterSensor::
process(const Sample* samp, list<const Sample*>& results)
{
    // Try to scan the variables of a sample tag from the raw sensor
    // message.
    SampleTag* stag = 0;
    SampleT<float>* outs = searchSampleScanners(samp, &stag);
    if (!outs)
    {
        return false;
    }

    // Apply any time tag adjustments.
    adjustTimeTag(stag, outs);

    // Apply any variable conversions.  Note this has to happen after the
    // time is adjusted, since the calibrations are keyed by time.
    applyConversions(stag, outs);

    results.push_back(outs);
    return true;
}

void
CharacterSensor::
adjustTimeTag(SampleTag* stag, SampleT<float>* outs)
{
    // Correct for a known sampling or sensor response lag.
    // Then, if requested, reduce latency jitter in the time tags.
    outs->setTimeTag(outs->getTimeTag() - getLagUsecs());
    TimetagAdjuster* ttadj = _ttadjusters[stag];
    if (ttadj)
    {
        outs->setTimeTag(ttadj->adjust(outs->getTimeTag()));
    }
}
