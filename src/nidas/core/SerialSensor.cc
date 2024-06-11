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
#include "TCPSocketIODevice.h"
#include "UDPSocketIODevice.h"
#include "BluetoothRFCommSocketIODevice.h"
#include "Looper.h"
#include "Prompt.h"

#include <nidas/util/Logger.h>

#include <cmath>

#include <iostream>
#include <sstream>
#include <iomanip>

using namespace std;
using namespace nidas::core;

namespace n_u = nidas::util;

SerialSensor::SerialSensor():
    _termios(),_serialDevice(0),_prompters(),_prompting(false),
    _rts485(0)
{
    setDefaultMode(O_RDWR);
    _termios.setRaw(true);
    _termios.setRawLength(1);
    _termios.setRawTimeout(0);
}

SerialSensor::~SerialSensor()
{
    list<Prompter*>::const_iterator pi = _prompters.begin();
    for (; pi != _prompters.end(); ++pi) delete *pi;
}

SampleScanner* SerialSensor::buildSampleScanner()
{
    SampleScanner* scanr = CharacterSensor::buildSampleScanner();
    DLOG(("%s: usec/byte=%d",getName().c_str(),getUsecsPerByte()));
    scanr->setUsecsPerByte(getUsecsPerByte());
    return scanr;
}

IODevice* SerialSensor::buildIODevice()
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
    else {
        _serialDevice = new SerialPortIODevice();
        _serialDevice->termios() = _termios;
        _serialDevice->setRTS485(_rts485);
        return _serialDevice;
    }
}

int SerialSensor::getUsecsPerByte() const
{
    if (_serialDevice) return _serialDevice->getUsecsPerByte();
    return 0;
}

void SerialSensor::open(int flags)
{
    flags |= O_NOCTTY;
    CharacterSensor::open(flags);

    // Flush the serial port
    if (::isatty(getReadFd())) {
        int accmode = flags & O_ACCMODE;
        int fres;
        if (accmode == O_RDONLY) fres = ::tcflush(getReadFd(),TCIFLUSH);
        else if (accmode == O_WRONLY) fres = ::tcflush(getWriteFd(),TCOFLUSH);
        else fres = ::tcflush(getReadFd(),TCIOFLUSH);
        if (fres < 0) throw n_u::IOException(getName(),"tcflush",errno);
    }

    sendInitString();

    // FSSPs set up their message parameters in the sendInitString() method, so we can't check
    // for this problem until now.
    if (getDeviceName().find("usock:") != 0 &&
        getMessageLength() + getMessageSeparator().length() == 0)
            throw n_u::InvalidParameterException(getName(),"message","must specify a message separator or a non-zero message length");

    initPrompting();
}

void SerialSensor::setMessageParameters(unsigned int len, const string& sep,
                                        bool eom)
{
    CharacterSensor::setMessageParameters(len,sep,eom);

    // Note we don't change _termios here.
    // Termios is set to to raw mode, len=1, in the constructor.
    // Very old NIDAS code did a _termio.setRawLength() to the
    // message length, but not any more. I don't think it made
    // things any more efficient, and may have reduced the accuracy of
    // time-tagging.
}

void SerialSensor::close()
{
    shutdownPrompting();
    DSMSensor::close();
}

void SerialSensor::applyTermios()
{
    if (_serialDevice) {
        _serialDevice->termios() = _termios;
        _serialDevice->applyTermios();
    }
}

void SerialSensor::initPrompting()
{
    if (isPrompted()) {
        const list<Prompt>& prompts = getPrompts();
        list<Prompt>::const_iterator pi = prompts.begin();
        for (; pi != prompts.end(); ++pi) {
           const Prompt& prompt = *pi;
           Prompter* prompter = new Prompter(this);
           prompter->setPrompt(n_u::replaceBackslashSequences(prompt.getString()));
           prompter->setPromptPeriodMsec((int) rint(MSECS_PER_SEC / prompt.getRate()));
           prompter->setPromptOffsetMsec((int) rint(MSECS_PER_SEC * prompt.getOffset()));

           _prompters.push_back(prompter);
           //addPrompter(n_u::replaceBackslashSequences(pi->getString()), (int) rint(MSECS_PER_SEC / pi->getRate()));
           // cerr << "promptPeriodMsec=" << _promptPeriodMsec << endl;
        }
        startPrompting();
    }
}

void SerialSensor::shutdownPrompting()
{
    stopPrompting();
    list<Prompter*>::const_iterator pi = _prompters.begin();
    for (; pi != _prompters.end(); ++pi) delete *pi;
    _prompters.clear();
}

void SerialSensor::startPrompting()
{
    if (isPrompted()) {
        list<Prompter*>::const_iterator pi;
        //for (pi = getPrompters().begin(); pi != getPrompters.end(); ++pi) {
        for (pi = _prompters.begin(); pi != _prompters.end(); ++pi) {
            Prompter* prompter = *pi;
            getLooper()->addClient(prompter,
                    prompter->getPromptPeriodMsec(),
                    prompter->getPromptOffsetMsec());
        }
    _prompting = true;
    }
}

void SerialSensor::stopPrompting()
{
    if (isPrompted()) {
        list<Prompter*>::const_iterator pi;
        //for (pi = getPrompters().begin(); pi = getPrompters().end(); ++pi) {
        for (pi = _prompters.begin(); pi != _prompters.end(); ++pi) {
            Prompter* prompter = *pi;
            getLooper()->removeClient(prompter);
        }
    _prompting = false;
    }
}

void SerialSensor::printStatus(std::ostream& ostr) throw()
{
    DSMSensor::printStatus(ostr);

    try {
    ostr << "<td align=left>" << _termios.getBaudRate() <<
        _termios.getParityString().substr(0,1) <<
        _termios.getDataBits() << _termios.getStopBits();
    if (getReadFd() < 0) {
        ostr << ",<font color=red><b>not active</b></font>";
        if (getTimeoutMsecs() > 0)
        ostr << ",timeouts=" << getTimeoutCount();
        ostr << "</td></tr>" << endl;
        return;
    }
    if (getTimeoutMsecs() > 0)
        ostr << ",timeouts=" << getTimeoutCount();
    ostr << "</td></tr>" << endl;
    }
    catch(const n_u::IOException& ioe) {
        ostr << "<td>" << ioe.what() << "</td></tr>" << endl;
    n_u::Logger::getInstance()->log(LOG_ERR,
        "%s: printStatus: %s",getName().c_str(),
        ioe.what());
    }
}

void SerialSensor::fromDOMElement(const xercesc::DOMElement* node)
{
    DOMableContext dmc(this, "SerialSensor: ", node);
    CharacterSensor::fromDOMElement(node);

    XDOMElement xnode(node);

    std::string aval;
    if (getAttribute(node, "baud", aval))
    {
        if (!_termios.setBaudRate(asInt(aval)))
            throw n_u::InvalidParameterException(
                string("SerialSensor:") + getName(),
                "baud", aval);
    }
    if (getAttribute(node, "parity", aval))
    {
        if (aval == "odd") _termios.setParity(n_u::Termios::ODD);
        else if (aval == "even") _termios.setParity(n_u::Termios::EVEN);
        else if (aval == "none") _termios.setParity(n_u::Termios::NONE);
        else throw n_u::InvalidParameterException(
            string("SerialSensor:") + getName(),
            "baud", aval);
    }
    if (getAttribute(node, "databits", aval)) {
        _termios.setDataBits(asInt(aval));
    }
    if (getAttribute(node, "stopbits", aval)) {
        _termios.setStopBits(asInt(aval));
    }
    if (getAttribute(node, "rts485", aval)) {
        if (aval == "true" || aval == "1") {
            _rts485 = 1;
        }
        else if (aval == "false" || aval == "0") {
            _rts485 = 0;
        }
        else if (aval == "-1") {
            _rts485 = -1;
        }
        else {
            throw n_u::InvalidParameterException(
                string("SerialSensor:") + getName(),
                "rts485", aval);
        }
    }
}

SerialSensor::Prompter::~Prompter()
{
    delete [] _prompt;
}

void SerialSensor::Prompter::setPrompt(const string& val)
{
    delete [] _prompt;
    _promptLen = val.length();
    _prompt = new char[_promptLen+1];
    // Use memcpy, not strcpy since the prompt may contain
    // null chars.
    memcpy(_prompt,val.c_str(),_promptLen);
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
    if (!_prompt) return;
    try {
    _sensor->write(_prompt,_promptLen);
    }
    catch(const n_u::IOException& e) {
    n_u::Logger::getInstance()->log(LOG_ERR,
        "%s: write prompt: %s",_sensor->getName().c_str(),
        e.what());
    }
}

