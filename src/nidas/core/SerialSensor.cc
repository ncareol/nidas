// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2011, Copyright University Corporation for Atmospheric Research
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

#include "SerialSensor.h"

#include "Looper.h"
#include "Prompt.h"

#include <nidas/core/HardwareInterface.h>
#include <nidas/util/Logger.h>
#include <nidas/util/SPoll.h>

#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <locale>

using namespace std;
using namespace nidas::core;
using namespace nidas::util;

namespace n_u = nidas::util;

namespace nidas { namespace core {

SerialSensor::SerialSensor():
    _autoConfigSupported(false), _autoConfigEnabled(false),
    _portconfig(), _portconfigs(), _pcindex(0),
    _autoConfigState(AUTOCONFIG_UNSUPPORTED),
    _serialState(AUTOCONFIG_UNSUPPORTED),_scienceState(AUTOCONFIG_UNSUPPORTED),
    _deviceState(AUTOCONFIG_UNSUPPORTED),_configMode(NOT_ENTERED),
    _serialDevice(0), _prompters(), _prompting(false)
{
    // XXX
    //
    // I'm not sure this is the right way to do this.  Before autoconfig the
    // default mode was just O_RDWR.  O_NOCTTY gets added in open(), so I
    // don't think that has any effect here.  Autoconfig also added the
    // _blocking flag and api to SerialPortIODevice, so adding O_NDELAY seems
    // redundant or superfluous to this, if a SerialSensor device will always
    // be opened non-blocking anyway.  Really with judicious use of a select()
    // or poll() based api the blocking setting becomes entirely irrelevant.
    setDefaultMode(O_RDWR|O_NOCTTY|O_NDELAY);

}

SerialSensor::~SerialSensor()
{
    list<Prompter*>::const_iterator pi = _prompters.begin();
    for (; pi != _prompters.end(); ++pi) delete *pi;

    _serialDevice = 0;
}

SampleScanner* SerialSensor::buildSampleScanner()
    throw(n_u::InvalidParameterException)
{
    SampleScanner* scanr = CharacterSensor::buildSampleScanner();
    DLOG(("%s: usec/byte=%d",getName().c_str(),getUsecsPerByte()));
    scanr->setUsecsPerByte(getUsecsPerByte());
    return scanr;
}

IODevice* SerialSensor::buildIODevice() throw(n_u::IOException)
{
    IODevice* device = overrideIODevice();

    if (!device)
    {
        DLOG(("creating SerialPortIODevice on ") << getDeviceName());

        // activate the first port config
        setPortConfig(getFirstPortConfig());
        DLOG(("") << "set first port config as active config on "
                  << getDeviceName() << ": " << _portconfig);

        _serialDevice = new SerialPortIODevice(getDeviceName(), _portconfig);
        device = _serialDevice;
    }

    return device;
}

int SerialSensor::getUsecsPerByte() const
{
    if (_serialDevice) return _serialDevice->getUsecsPerByte();
    return 0;
}

void SerialSensor::powerOn()
{
    auto port = HardwareDevice::lookupDevice(getDeviceName());
    if (auto ipower = port.iOutput())
    {
        ILOG(("Power on: ") << getDeviceName());
        ipower->on();
    }
}

void SerialSensor::powerOff()
{
    auto port = HardwareDevice::lookupDevice(getDeviceName());
    if (auto ipower = port.iOutput())
    {
        ILOG(("Power off: ") << getDeviceName());
        ipower->off();
    }
}

void SerialSensor::open(int flags)
    throw(n_u::IOException,n_u::InvalidParameterException)
{
    // If this port has hardware power control, make sure it is turned on. Do
    // this on every open in case the sensor was turned off deliberately to be
    // re-opened after a timeout.  See the note in close() about why power is
    // _not_ turned off there, even though that would have better symmetry.
    powerOn();

    flags |= O_NOCTTY;
    CharacterSensor::open(flags);

    serPortFlush();

    doAutoConfig();

    sendInitString();

    // FSSPs set up their message parameters in the sendInitString() method, so we can't check
    // for this problem until now.
    if (getDeviceName().find("usock:") != 0 &&
        getMessageLength() + getMessageSeparator().length() == 0)
            throw n_u::InvalidParameterException(getName(),"message","must specify a message separator or a non-zero message length");

    initPrompting();
}

void SerialSensor::sendInitString() throw(nidas::util::IOException)
{
    NLOG(("%s:%s: Putting sensor into measurement mode", getName().c_str(), getClassName().c_str()));
    if (getAutoConfigEnabled()) {
        exitConfigMode();
    }
    // XXXXX This looks suspicious.  Why can't an "autoconfig" sensor also send
    // an init string before sampling?
    else {
        if (getInitString().length() != 0) {
            CharacterSensor::sendInitString();
        }
    }
}


void SerialSensor::serPortFlush(const int flags) 
{
    // used to hold the port access flags, whether passed in or read from the device.
    // if reading from the device, the device must be open
    int attrFlags = 0;

    if (_serialDevice && getReadFd() && ::isatty(getReadFd())) {
        if (!flags) {
            attrFlags = fcntl(getReadFd(), F_GETFL, 0);
            // TODO: handle error attrFlags < 0
        }
        else {
            attrFlags = flags;
        }

        int accmode = attrFlags & O_ACCMODE;
        if (accmode == O_RDONLY) {
             _serialDevice->flushInput();
             DLOG(("Flushed serial port input on device: ") << getName());
        }
        else if (accmode == O_WRONLY) {
            _serialDevice->flushOutput();
             DLOG(("Flushed serial port output on device: ") << getName());
        }
        else {
            _serialDevice->flushBoth();
             DLOG(("Flushed serial port input and output on device: ") << getName());
        }
    }
}

// void SerialSensor::setMessageParameters(unsigned int len, const string& sep, bool eom)
//     throw(n_u::InvalidParameterException, n_u::IOException)
// {
//     CharacterSensor::setMessageParameters(len,sep,eom);

//     // Note we don't change _termios here.
//     // Termios is set to to raw mode, len=1, in the constructor.
//     // Very old NIDAS code did a _termio.setRawLength() to the
//     // message length, but not any more. I don't think it made
//     // things any more efficient, and may have reduced the accuracy of
//     // time-tagging.
// }

void SerialSensor::close() throw(n_u::IOException)
{
    // It might make sense to turn off the hardware power when closing the
    // sensor, except that has not been past behavior and may cause confusion.
    // Instead, the only time a sensor is powered off is when powerOff() is
    // explicitly called, as is done when a sensor is being reopened after a
    // timeout.
    shutdownPrompting();
    DSMSensor::close();
}


SerialSensor::PortConfigList
SerialSensor::
getPortConfigs()
{
    return _portconfigs;
}

PortConfig
SerialSensor::
getFirstPortConfig()
{
    if (_portconfigs.size())
        return _portconfigs.front();
    return PortConfig();
}

void
SerialSensor::
setPortConfigIndex(int idx)
{
    if (idx == -1)
    {
        _pcindex = _portconfigs.size();
    }
    else if (0 <= idx && idx <= (int)_portconfigs.size())
    {
        _pcindex = idx;
    }
    else
    {
        _pcindex = 0;
    }
}

void
SerialSensor::
addPortConfig(const PortConfig& pc)
{
    auto it = _portconfigs.begin() + _pcindex;
    _portconfigs.insert(it, pc);
    ++_pcindex;
}

void
SerialSensor::
replacePortConfigs(const PortConfigList& pconfigs)
{
    _portconfigs = pconfigs;
    setPortConfigIndex(-1);
}

void SerialSensor::setPortConfig(const PortConfig& pc)
{
    // When findWorkingPortConfig() calls this the first time, it is likely
    // starting out with the same PortConfig as was used to first open the
    // device, in which case it is not necessary to re-apply those settings.
    // However, until that can be thought out more carefully, and the
    // successful application of the settings can be verified, there is no
    // real harm in applying the same settings again.
    _portconfig = pc;

    // These originally were enforced here and also duplicated elsewhere, but
    // instead they are now set in one place in the PortConfig class.  They
    // cannot be changed through the XML and no other settings have been
    // needed, so there is no need to keep enforcing the same settings.  And
    // it makes the API confusing to pass in a PortConfig to activate and then
    // modify it, when normally the PortConfig being activated is one that
    // already exists in the _portconfigs list.  The PortConfig defaults are
    // different from the Termios defaults, presumably because the Termios
    // defaults were intended for terminal devices and not for reading serial
    // sensors.  The crux here is that the current portconfig is passed to the
    // SerialPortIODevice, with any modifications, not the portconfig passed
    // into this method!

    // _portconfig.termios.setRaw(true);
    // _portconfig.termios.setRawLength(1);
    // _portconfig.termios.setRawTimeout(0);

    if (_serialDevice) {
        _serialDevice->setPortConfig(_portconfig);
    }
    else
    {
        DLOG(("") << getName()
                  << ": setPortConfig() but without serial io device.");
    }
}


PortConfig SerialSensor::getPortConfig() 
{
    return _portconfig;
}

void SerialSensor::applyTermios() throw(nidas::util::IOException)
{
    if (_serialDevice) {
        _serialDevice->applyTermios();
    }
    else {
        NLOG(("SerialSensor::applyTermios(): device, ") << getName() 
             << (", is trying to apply termios too early, "
                 "or is newer type serial device such as USB or socket-oriented, "
                 " which has no need of a PortConfig."));
    }
}

void SerialSensor::applyPortConfig() 
{
    static LogContext lp(LOG_DEBUG);

    if (_serialDevice) {
        _serialDevice->applyPortConfig();
    }
    else {
        NLOG(("SerialSensor::applyPortConfig(): device, ") << getName() 
             << (", is trying to apply PortConfig too early, "
                 "or is newer type serial device such as USB or socket-oriented, "
                 " which has no need of a PortConfig."));
    }

    if (lp.active()) {
        HardwareDevice port(HardwareDevice::lookupDevice(getDeviceName()));
        lp.log() << "SerialSensor::applyPortConfig(): Current power state: "
                 << port.getOutputState();
    }
}

void SerialSensor::initPrompting() throw(n_u::IOException)
{
    for (auto& pi : getPrompts())
    {
        if (! pi.valid())
            continue;
        Prompt prompt = pi;
        Prompter* prompter = new Prompter(this);
        prompt.setString(n_u::replaceBackslashSequences(prompt.getString()));
        prompter->setPrompt(prompt);
        int periodms = (int) rint(MSECS_PER_SEC / prompt.getRate());
        int offsetms = (int) rint(MSECS_PER_SEC * prompt.getOffset());
        prompter->setPromptPeriodMsec(periodms);
        prompter->setPromptOffsetMsec(offsetms);
        _prompters.push_back(prompter);
        DLOG(("") << "sensor " << getName() << " prompter: "
                    << prompt << " @ period=" << periodms << "ms, "
                    << "offset=" << offsetms << "ms");
    }
    startPrompting();
}

void SerialSensor::shutdownPrompting() throw(n_u::IOException)
{
    stopPrompting();
    for (auto prompter : _prompters)
    {
        delete prompter;
    }
    _prompters.clear();
}

void SerialSensor::startPrompting() throw(n_u::IOException)
{
    for (auto prompter : _prompters) {
        getLooper()->addClient(prompter,
                prompter->getPromptPeriodMsec(),
                prompter->getPromptOffsetMsec());
        _prompting = true;
    }
}

void SerialSensor::stopPrompting() throw(n_u::IOException)
{
    for (auto prompter : _prompters) {
        getLooper()->removeClient(prompter);
    }
    _prompting = false;
}

void SerialSensor::printStatus(std::ostream& ostr) throw()
{
    DSMSensor::printStatus(ostr);

    if (_serialDevice) {

        try {
			ostr << "<td align=left>" << to_string(_autoConfigState);
			if (_autoConfigState == AUTOCONFIG_STARTED) {
				ostr << "\n" << to_string(_serialState);
				if (_serialState != CONFIGURING_COMM_PARAMETERS) {
					ostr << "\n" << to_string(_scienceState);
				}
			}
            const Termios& tio = getPortConfig().termios;
			ostr << "\n" << tio.getBaudRate() <<
				tio.getParity().toChar() <<
				tio.getDataBits() << tio.getStopBits();
			if (getReadFd() < 0) {
				ostr << ",<font color=red><b>not active</b></font>";
				if (getTimeoutMsecs() > 0) {
					ostr << ",timeouts=" << getTimeoutCount();
				}
				ostr << "</td>" << endl;
				return;
			}
			if (getTimeoutMsecs() > 0) {
				ostr << ",timeouts=" << getTimeoutCount();
			}
			ostr << "</td>" << endl;
        }
        catch(const n_u::IOException& ioe) {
            ostr << "<td>" << ioe.what() << "</td>" << endl;
            n_u::Logger::getInstance()->log(
            	LOG_ERR, "%s: printStatus: %s",
				getName().c_str(), ioe.what());
        }
    }
}

/*
 *  Handles common port config attributes in autoconfig tag.
 */
void SerialSensor::fromDOMElementAutoConfig(const xercesc::DOMElement* node)
{
    xercesc::DOMNode* child;
    for (child = node->getFirstChild(); child != 0;
            child = child->getNextSibling()) {
        if (child->getNodeType() != xercesc::DOMNode::ELEMENT_NODE)
            continue;

        XDOMElement xchild((xercesc::DOMElement*) (child));
        const string& elname = xchild.getNodeName();
        if (elname == "autoconfig") {
            DLOG(("SerialSensor::fromDOMElementAutoconfig(): autoconfig tag found."));
            if (supportsAutoConfig()) {
                setAutoConfigEnabled();

                PortConfig pc;
                bool found_portconfig = false;
                // get all the attributes of the node
                xercesc::DOMNamedNodeMap* pAttributes = child->getAttributes();
                int nSize = pAttributes->getLength();
                for (int i = 0; i < nSize; ++i) {
                    XDOMAttr attr((xercesc::DOMAttr*) (pAttributes->item(i)));
                    if (pc.setAttribute(getName(), attr.getName(), attr.getValue()))
                        found_portconfig = true;
                }
                if (found_portconfig)
                {
                    addPortConfig(pc);
                    DLOG(("") << "added port config from autoconfig element: " << pc);
                }
            }
        }
        else if (elname == "message");
        else if (elname == "prompt");
        else if (elname == "sample");
        else if (elname == "parameter");
        else if (elname == "calfile");
        else
            throw n_u::InvalidParameterException(
                    getName(), "unknown element", elname);
    }
}


void SerialSensor::fromDOMElement(
	const xercesc::DOMElement* node)
    throw(n_u::InvalidParameterException)
{
    DLOG(("SerialSensor::fromDOMElement(): entry..."));

    CharacterSensor::fromDOMElement(node);

    DLOG(("SuperClass::fromDOMElement() methods done..."));

    // this is called before any sensor calls fromDOMElementAutoConfig(), so
    // reset the port config index so that all port configs from the xml get
    // inserted ahead of any existing configs, in the order read from the xml.
    DLOG(("") << "SerialSensor::fromDOMElement resetting port config index");
    setPortConfigIndex(0);

    if(node->hasAttributes()) {
        // get all the attributes of the node
        xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
        int nSize = pAttributes->getLength();
        PortConfig portconfig;
        if (!_portconfigs.empty())
        {
            // as a special case for serial port settings in the sensor
            // element, begin with a copy of the first hardcoded port config
            // in this sensor class.  this way configs which do not yet set
            // the transceiver mode will "inherit" that from the sensor class.
            // it could be useful to let all port configs inherit from the
            // last one set, as a way to specify multiple port configs which
            // only vary in a few parameters.  for now, though, stick to
            // what's needed to get CSI_IRGA_Sonic to set RS485_FULL for
            // sensor elements which don't otherwise set the transceiver mode.
            portconfig = _portconfigs.front();
        }
        bool found_portconfig = false;
        for(int i=0;i<nSize;++i) {
            XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
            // get attribute name
            const std::string& aname = attr.getName();

            if (aname == "ID")/*ignore*/;
            else if (aname == "IDREF");
            else if (aname == "class");
            else if (aname == "devicename");
            else if (aname == "id");
            else if (portconfig.setAttribute(getName(), aname, attr.getValue()))
                found_portconfig = true;
            else if (aname == "nullterm");
            else if (aname == "init_string");
            else if (aname == "suffix");
            else if (aname == "height");
            else if (aname == "depth");
            else if (aname == "duplicateIdOK");
            else if (aname == "timeout");
            else if (aname == "readonly");
            else if (aname == "station");
            else if (aname == "xml:base" || aname == "xmlns") {}
            else throw n_u::InvalidParameterException(
                    getName(), "unknown attribute", aname);
        }
        if (found_portconfig)
        {
            // insert this port config in the front of any built-in configs.
            addPortConfig(portconfig);
            DLOG(("") << getName()
                      << ": added port config from sensor element: "
                      << portconfig);
        }
    }

    DLOG(("SerialSensor::fromDOMElement(): exit..."));
}

/**
 *  Autoconfig functions
 */

void SerialSensor::initAutoConfig()
{
    if (supportsAutoConfig()) {
        _autoConfigState = WAITING_IDLE;
        _serialState = WAITING_IDLE;
        _scienceState = WAITING_IDLE;
        _deviceState = WAITING_IDLE;
    }
}

void SerialSensor::doAutoConfig()
{
	// find out if we're a legacy subclass or a new autoconfig subclass
	if (getAutoConfigEnabled()) {
	    setSensorState(SENSOR_CONFIGURING);
		_autoConfigState = AUTOCONFIG_STARTED;
		// Must be a new autoconfig subclass...
		_serialState = CONFIGURING_COMM_PARAMETERS;
		if (findWorkingSerialPortConfig()) {
			NLOG(("Found working sensor serial port configuration"));
			NLOG((""));
			NLOG(("Checking whether found working port config is the desired port config"));
			PortConfig foundWorkingConfig = getPortConfig();
			if (foundWorkingConfig != _portconfig) {
				NLOG(("found working config not equal to desired config"));
				NLOG(("found working config: ") << foundWorkingConfig);
				NLOG(("desired port config: ") << _portconfig);
				NLOG(("Attempting to install the desired sensor serial parameter configuration"));
				if (installDesiredSensorConfig(_portconfig)) {
					NLOG(("Desired sensor serial port configuration successfully installed"));
					_serialState = COMM_PARAMETER_CFG_SUCCESSFUL;
				}
				else {
					NLOG(("Failed to install desired config. Reverting back to what works. "));
					installDesiredSensorConfig(foundWorkingConfig);
					_serialState = COMM_PARAMETER_CFG_UNSUCCESSFUL;
					_autoConfigState = AUTOCONFIG_UNSUCCESSFUL;
		            setSensorState(SENSOR_CONFIGURE_FAILED);
				}
			}
			else {
				NLOG(("Found working port config is same as desired port config."));
				_serialState = COMM_PARAMETER_CFG_SUCCESSFUL;
			}

            NLOG(("Collecting sensor metadata..."));
            updateMetaData();
            NLOG(("Attempting to configure the sensor science parameters."));
            _scienceState = CONFIGURING_SCIENCE_PARAMETERS;
            if (configureScienceParameters()) {
                NLOG(("Desired sensor science configuration successfully installed"));
                _scienceState = SCIENCE_SETTINGS_SUCCESSFUL;
                _autoConfigState =
                        _serialState == COMM_PARAMETER_CFG_SUCCESSFUL ?
                                        AUTOCONFIG_SUCCESSFUL : AUTOCONFIG_UNSUCCESSFUL;
                setSensorState(_autoConfigState == AUTOCONFIG_SUCCESSFUL ?
                               SENSOR_CONFIGURE_SUCCEEDED : SENSOR_CONFIGURE_FAILED);
            }
            else {
                NLOG(("Failed to install sensor science configuration"));
                _scienceState = SCIENCE_SETTINGS_UNSUCCESSFUL;
                _autoConfigState = AUTOCONFIG_UNSUCCESSFUL;
                setSensorState(SENSOR_CONFIGURE_FAILED);
            }
		}
		else
		{
			NLOG(("Couldn't find a serial port configuration that worked with this sensor. "
				  "May need to troubleshoot the sensor or cable. "
				  "!!!NOTE: Sensor is NOT ready for data collection!!!"));
			_serialState = COMM_PARAMETER_CFG_UNSUCCESSFUL;
			_autoConfigState = AUTOCONFIG_UNSUCCESSFUL;
			setSensorState(SENSOR_CONFIGURE_FAILED);
		}

		printDeviceMetaData();
	}
	else {
	    NLOG(("") << getName() << ": autoconfig not enabled or not supported.");
        applyPortConfig();
	}
}

bool SerialSensor::findWorkingSerialPortConfig()
{
    bool foundIt = false;

    // Iterate through the available port configs looking for one that works.
    for (auto& pc : _portconfigs)
    {
        DLOG(("") << getName()
                  << ": checking for working port config: " << pc);
        setPortConfig(pc);
        applyPortConfig();

        CFG_MODE_STATUS cfgMode = enterConfigMode();
        if (cfgMode == ENTERED && doubleCheckResponse())
        {
            foundIt = true;
            break;
        }
        // At one point there was a lot of extra code here to try alternate
        // termination and rts485 settings if the port type was differential
        // or half-duplex.  However, I'm not sure the rts485 setting makes any
        // difference now on DSM3 ports, and those are the only differential
        // or half-duplex ports in use.  And sensors which might need specific
        // termination settings should specify those in the alternate port
        // configs.  This allows this algorithm to be much more
        // straightforward with a lot less duplicate code.
    }
    return foundIt;
}


int SerialSensor::readResponse(void *buf, int len, int msecTimeout, bool checkPrintable,
                               bool backOffTimeout, int retryTimeoutFactor)
{
    SPoll poller(msecTimeout);
    poller.addPollee(getReadFd(), 0);

    VLOG(("SerialSensor::readResponse(): timeout msec: %i", msecTimeout));

    int res = 0;

    if (msecTimeout >= MSECS_PER_SEC) {
        VLOG(("SerialSensor::readResponse(): Waiting on select just once as timeout >= 1s"));
        try {
            res = poller.poll();
        } catch (IOTimeoutException& e) {
            DLOG(("Select timeout error on: ") << getDeviceName() << e.what());
            return res;
        } catch (IOException& e) {
            DLOG(("General select error on: ") << getDeviceName() << e.what());
            return (std::size_t)0x40000000;
        }
    }
    else {
        // allow for some slop in data coming in...
        VLOG(("SerialSensor::readResponse(): Waiting on select a few times to allow for slowdowns"));
        int tmpto = msecTimeout;
        for (int i=0; res <= 0 && i < 4; ++i) {
            VLOG(("SerialSensor::readResponse(): select loop: i: %i; msec timeout: %i", i, tmpto));
            try {
                res = poller.poll();
            } catch (IOTimeoutException& e) {
                // DLOG(("Select timeout error on: ") << getDeviceName() << e.what());
                // return res;
            } catch (IOException& e) {
                // DLOG(("General select error on: ") << getDeviceName() << e.what());
                // return res;
            }
            VLOG(("SerialSensor::readResponse(): select loop: res: %i; ", res));

            if (res <= 0 && backOffTimeout) {
                VLOG(("SerialSensor::readResponse(): select loop backing off: i: %i; ", i));
                tmpto *= retryTimeoutFactor;
                poller.changeTimeout(tmpto);
            }
        }

        if (res < 0) {
            return (std::size_t)0x40000000;
        }

        if (res == 0) {
            DLOG(("Select timeout on: ") << getDeviceName() << ": " << msecTimeout << " msec");
            return 0;
        }
    }

    VLOG(("SerialSensor::readResponse(): Select successful, reading..."));
    // no select timeout or error, so call read() syscall directly.
    // we also know that there is only one file descriptor, so we don't need 
    // to invoke getNextPolleeEvents()
    std::size_t numChars = ::read(getReadFd(), (char*)buf, (unsigned long)len);
    if (numChars) {
        if (checkPrintable && containsNonPrintable((const char*)buf, numChars)) {
            // not so big assumption here that numChars never gets to 0x80000000
            numChars |= 0x80000000;
        }
    }
    else {
        Exception e(std::string(""), errno);
        DLOG(("SerialSensor::readResponse(): read error: %s", e.what()));
    }

    return numChars;
}

int SerialSensor::readEntireResponse(void *buf, int len, int msecTimeout,
                                     bool checkPrintable, int retryTimeoutFactor)
{
	char* cbuf = (char*)buf;
	int bufRemaining = len;
    int charsReadStatus = readResponse(cbuf, len, msecTimeout, checkPrintable, true, retryTimeoutFactor);

    bool readJunk = charsReadStatus & 0x80000000;
    DLOG(("SerialSensor::readEntireResponse(): readJunk: %s", (readJunk ? "true" : "false")));
    int numCharsRead = charsReadStatus & ~0xC0000000;
    DLOG(("SerialSensor::readEntireResponse(): numCharsRead: %i", numCharsRead));
    int totalCharsRead = numCharsRead;
    bool selectError = charsReadStatus & 0x40000000;
    if (selectError) {
        return 0;
    }

    int bufIdx = totalCharsRead;
    bufRemaining -= numCharsRead;

    static LogContext logctxt(LOG_VERBOSE);
    if (numCharsRead > 0 && logctxt.active()) {
        logctxt.log(nidas::util::LogMessage().format("Initial num chars read is: ")
                                                     << numCharsRead << " comprised of: ");
        printResponseHex(numCharsRead, cbuf);
    }

    int i=0;
    for (; (!readJunk && numCharsRead && bufRemaining > 0); ++i) {
        charsReadStatus = readResponse(&cbuf[bufIdx], bufRemaining, std::max(1, 5*getUsecsPerByte()/USECS_PER_MSEC),
                                       checkPrintable, true, retryTimeoutFactor);
        readJunk = charsReadStatus & 0x80000000;
        numCharsRead = charsReadStatus & ~0x80000000;
        if (readJunk) {
            DLOG(("SerialSensor::readEntireResponse(): Got junk. Bailing out of read loop."));
            totalCharsRead = -1;
            break;
        }
        totalCharsRead += numCharsRead;
        bufIdx = totalCharsRead;
        if (totalCharsRead >= len) {
            bufIdx = len;
            bufRemaining = 0;
            break;
        }
        bufRemaining -= numCharsRead;
        VLOG(("SerialSensor::readEntireResponse(): looping: %i, total chars: %i", i, totalCharsRead));
        VLOG(("SerialSensor::readEntireResponse(): num chars: %i, buf remaining: %i", numCharsRead, bufRemaining));
    }

    if (bufIdx > 0 && logctxt.active()) {
        logctxt.log(nidas::util::LogMessage().format("Num chars read: ")
                                                     << bufIdx << " comprised of: ");
        printResponseHex(totalCharsRead, cbuf);
    }

    // getting garbage, bail out early, but first drain the swamp
    if (readJunk) {
        DLOG(("SerialSensor::readEntireResponse(): Got junk. Draining the swamp."));
        bufRemaining -= numCharsRead;
        bufIdx += numCharsRead;
        int drainedChars = 0;
        do {
            numCharsRead = readResponse(&cbuf[bufIdx], bufRemaining,
                                        std::max(1, 2*getUsecsPerByte()/USECS_PER_MSEC), false);
            drainedChars += numCharsRead;
            bufIdx += numCharsRead;
            bufRemaining -= numCharsRead;
            if (bufRemaining < 0) {
                bufRemaining = 0;
            }
        } while (numCharsRead && bufRemaining > 0);
        VLOG(("SerialSensor::readEntireResponse(): got garbage, so drained ")
             << drainedChars << " characters.");
    }
    VLOG(("Took ") << i+1 << " reads to get entire response");

    return totalCharsRead;
}

bool SerialSensor::doubleCheckResponse()
{
    bool foundIt = false;

    DLOG(("Checking response once..."));
    if (checkResponse()) {
		// tell everyone
		DLOG(("Found working port config: ") << getPortConfig());

        foundIt = true;
    }
    else {
        DLOG(("Checking response twice..."));
        if (checkResponse()) {
            // tell everyone
            DLOG(("Response checks out on second try..."));
            foundIt = true;
        }
        else {
            DLOG(("Checked response twice, and failed twice."));
        }
    }

    return foundIt;
}

bool SerialSensor::configureScienceParameters()
{
    DLOG(("Sending sensor science parameters."));
    sendScienceParameters();
    DLOG(("First check of desired science parameters"));
    bool success = checkScienceParameters();
    if (!success) {
        DLOG(("First attempt to send science parameters failed - resending"));
        sendScienceParameters();
        success = checkScienceParameters();
        if (!success) {
            DLOG(("Second attempt to send science parameters failed. Giving up."));
        }
    }

    return success;
}

std::string to_string(AUTOCONFIG_STATE autoState)
{
    std::string stateStr;

    switch (autoState) {
        case AUTOCONFIG_UNSUPPORTED:
            stateStr = "Autoconfig unsupported";
            break;
        case WAITING_IDLE:
            stateStr = "Waiting/idle";
            break;
        case AUTOCONFIG_STARTED:
            stateStr = "Autoconfig started";
            break;
        case CONFIGURING_COMM_PARAMETERS:
            stateStr = "Configuring comm params";
            break;
        case COMM_PARAMETER_CFG_SUCCESSFUL:
            stateStr = "Comm cfg successful";
            break;
        case COMM_PARAMETER_CFG_UNSUCCESSFUL:
            stateStr = "Comm cfg unsuccessful";
            break;
        case CONFIGURING_SCIENCE_PARAMETERS:
            stateStr = "Configuring science params";
            break;
        case SCIENCE_SETTINGS_SUCCESSFUL:
            stateStr = "Science cfg successfull";
            break;
        case SCIENCE_SETTINGS_UNSUCCESSFUL:
            stateStr = "Science cfg unsuccessful";
            break;
        case AUTOCONFIG_SUCCESSFUL:
            stateStr = "Autoconfig successful";
            break;
        case AUTOCONFIG_UNSUCCESSFUL:
            stateStr = "Autoconfig unsuccessful";
            break;
        default:
            break;
    }

    return stateStr;
}

void SerialSensor::printResponseHex(int numCharsRead, const char* respBuf)
{
    if (numCharsRead > 0) {
        for (int i = 0; i < 5; ++i) {
            if ((i*10) > numCharsRead)
                break;

            char hexBuf[70];
            memset(hexBuf, 0, 70);
            for (int j = 0; j < 10; ++j) {
                if ((i*10 + j) > numCharsRead)
                    break;
                snprintf(&(hexBuf[j * 5]), 6, "0x%02x ", respBuf[(i * 10) + j]);
            }

            char* pBytes = &hexBuf[0];
            std::cout << std::string(pBytes) << std::endl;
        }

        DLOG(("SerialSensor::printResponseHex(): all done..."));
    }
}


SerialSensor::Prompter::~Prompter()
{
}

void SerialSensor::Prompter::setPrompt(const Prompt& prompt)
{
    _prompt = prompt;
}

void SerialSensor::Prompter::setPromptPeriodMsec(const int val)
{
    _promptPeriodMsec = val;
}

void SerialSensor::Prompter::setPromptOffsetMsec(const int val)
{
    _promptOffsetMsec = val;
}

void SerialSensor::Prompter::looperNotify() throw()
{
    // Set the response prefix before writing the prompt, to avoid a race
    // condition with the response.  Presumably the prompt has been timed so
    // that setting the prefix now will not interfere with any messages coming
    // from the sensor.
    if (_prompt.hasPrefix())
    {
        _sensor->setPrefix(_prompt.getPrefix());
    }
    const std::string& msg = _prompt.getString();
    if (msg.size())
    {
        try {
            _sensor->write(msg.data(), msg.size());
        }
        catch(const n_u::IOException& e) {
            n_u::Logger::getInstance()->log(LOG_ERR,
                "%s: write prompt: %s",_sensor->getName().c_str(),
                e.what());
        }
    }
}

} // namespace core
} // namespace nidas
