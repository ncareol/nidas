// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2021, Copyright University Corporation for Atmospheric Research
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

#include "ModbusRTU.h"
#include <nidas/core/Variable.h>
#include <nidas/util/UTime.h>
#include <nidas/util/Logger.h>
#include <nidas/util/EndianConverter.h>

#include <sstream>

using namespace nidas::dynld;
using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;
using nidas::util::Logger;

NIDAS_CREATOR_FUNCTION(ModbusRTU)

/*
 * modbus_read_registers apparently converts from modbus endianness,
 * which is big endian, to the endianness of the host. We want the
 * archived data to have a defined endianness, not depending on
 * the endianness of the host it was acquired on.  It is quite unlikely
 * that we'll use big-endian hosts, but just to be paranoid we'll
 * convert all data to little-endian, which is a no-op for little-endian hosts.
 */
static const n_u::EndianConverter *toLittle = n_u::EndianConverter::getConverter(
        n_u::EndianConverter::getHostEndianness(),
        n_u::EndianConverter::EC_LITTLE_ENDIAN);

static const n_u::EndianConverter *toHost = n_u::EndianConverter::getConverter(
        n_u::EndianConverter::EC_LITTLE_ENDIAN,
        n_u::EndianConverter::getHostEndianness());

ModbusRTU::ModbusRTU():SerialSensor(),
#ifdef HAVE_LIBMODBUS
    _modbusrtu(0), _slaveID(1), _regaddr(0), _pipefds{-1,-1},
    _thread(0), _iodevice(0),
#endif
    _nvars(0), _stag(0)
{
}

void ModbusRTU::validate()
{
    SerialSensor::validate();

    if (getSampleTags().size() != 1)
        throw n_u::InvalidParameterException(getName(), "sample",
            "must have exactly one sample");
    _stag = *getSampleTags().begin();
    _nvars = (uint16_t) _stag->getVariables().size();

    const vector<Variable*>& vars = _stag->getVariables();
    vector<Variable*>::const_iterator iv = vars.begin();

    for ( ; iv != vars.end(); ++iv) {
        if ((*iv)->getLength() > 1)
            throw n_u::InvalidParameterException(getName(), "variable",
                "cannot have length > 1");
    }
}

void ModbusRTU::init()
{
    SerialSensor::init();
}

#ifdef HAVE_LIBMODBUS

IODevice* ModbusRTU::buildIODevice()
{
    if (!_iodevice)
        _iodevice = new ModbusIODevice();   // deleted in DSMSensor dtor

    return _iodevice;
}

SampleScanner* ModbusRTU::buildSampleScanner()
{
    ModbusMessageStreamScanner* scanr = new ModbusMessageStreamScanner();
    scanr->setNullTerminate(false);
    scanr->setUsecsPerByte(getUsecsPerByte());

    // Format of data written by writer thread is a simple buffer,
    // consisting of all little-endian, words of uint16_t.
    // The first word is the number of values, nvars, followed by nvars of data.
    //
    // Use the little endian uint16_t nvars at the beginning of the
    // data buffer as the message separator for nidas to use when parsing messages.
    // The message length (which doesn't include the separator) is then 2 * nvars.
    uint16_t nvars_le = toLittle->uint16Value(_nvars);
    string sepstr((const char*) &nvars_le, 2);
    scanr->setMessageParameters(_nvars * sizeof(uint16_t), sepstr, false);

    return scanr;
}

Sample* ModbusRTU::ModbusMessageStreamScanner::nextSampleSepBOM(DSMSensor* sensor)
{
    Sample* samp = MessageStreamScanner::nextSampleSepBOM(sensor);
    if (samp)
        // Adjust time tag earlier by number of bytes that were discarded
        // between the libmodbus read and the data sent over the pipe.
        samp->setTimeTag(samp->getTimeTag() - _nbytesDiscarded * getUsecsPerByte());
    return samp;
}
#endif

#ifdef HAVE_LIBMODBUS
void ModbusRTU::open(int flags)
#else
void ModbusRTU::open(int)
#endif
{

#ifndef HAVE_LIBMODBUS
    throw n_u::IOException(getDeviceName(), "open", "built without libmodbus-dev");
#else

    if (::pipe(_pipefds) < 0)
        throw n_u::IOException(getDeviceName(), "pipe", errno);

    buildIODevice();
    _iodevice->setFd(_pipefds[0]);   // nidas reads from the IODevice

    SerialSensor::open(flags);

    const n_u::Termios& termios = getTermios();

    int baud = termios.getBaudRate();
    int data = termios.getDataBits();
    int stop = termios.getStopBits();
    int bits = data + stop + 1;

    char parity = 'N';

    switch (termios.getParity()) {
        case termios.NONE:
            parity = 'N'; break;
        case termios.ODD:
            parity = 'O'; bits++; break;
        case termios.EVEN:
            parity = 'E'; bits++; break;
        default: break;
    }

    int usecs = (bits * USECS_PER_SEC + termios.getBaudRate() / 2) /
        termios.getBaudRate();
    getSampleScanner()->setUsecsPerByte(usecs);

    _modbusrtu = modbus_new_rtu(getDeviceName().c_str(), baud, parity,
            data, stop);
    if (!_modbusrtu) throw n_u::IOException(getDeviceName(), "modbus_new_rtu", errno);

    const Parameter* param = getParameter("slaveID");
    if (param) {
        if (param->getLength() != 1)
            throw nidas::util::InvalidParameterException(
                getName(), "parameter",
                "bad length for slaveID");
        _slaveID = (int) param->getNumericValue(0);
    }
    param = getParameter("register");
    if (param) {
        if (param->getLength() != 1)
            throw nidas::util::InvalidParameterException(
                getName(), "parameter",
                "bad length for register");
        _regaddr = (int) param->getNumericValue(0);
    }

    float rate = _stag->getRate();
    if (rate == 0.0) rate = 1.0;

    int dtusec = USECS_PER_SEC / rate;

/* Evaluates to True if the version is greater than @major, @minor and @micro
   (Actually does a >= on micro, if major and minor are the same) */
#if LIBMODBUS_VERSION_CHECK(3,1,0)
    uint32_t to_sec, to_usec;
    modbus_get_response_timeout(_modbusrtu, &to_sec, &to_usec);
    DLOG(("old timeout, sec=") << to_sec << ", usec=" << to_usec);
#ifdef SET_MODBUS_TIMEOUT
    to_sec = dtusec / USECS_PER_SEC;
    to_usec = dtusec % USECS_PER_SEC;
    modbus_set_response_timeout(_modbusrtu, &to_sec, &to_sec);
    DLOG(("new timeout, sec=") << to_sec << ", usec=" << to_usec);
#endif

#else
    struct timeval tvold;
    modbus_get_response_timeout(_modbusrtu, &tvold);
    DLOG(("old timeout, sec=") << tvold.tv_sec << ", usec=" << tvold.tv_usec);
#ifdef SET_MODBUS_TIMEOUT
    struct timeval tvnew;
    tvnew.tv_sec = dtusec / USECS_PER_SEC;
    tvnew.tv_usec = dtusec % USECS_PER_SEC;
    modbus_set_response_timeout(_modbusrtu, &tvnew);
    DLOG(("new timeout, sec=") << tvnew.tv_sec << ", usec=" << tvnew.tv_usec);
#endif
#endif

    DLOG(("dtusec=") << dtusec);

    setTimeoutMsecs((dtusec * 5) / USECS_PER_MSEC);

    if (modbus_set_slave(_modbusrtu, _slaveID) < 0)
        throw n_u::IOException(getDeviceName(), "modbus_set_slave", errno);

    if (modbus_connect(_modbusrtu) < 0)
        throw n_u::IOException(getDeviceName(), "modbus_connect", errno);

    // must be connected before setting mode
#ifdef DO_485
    // But, on Vortex, /dev/ttyS2 this fails with "Inappropriate ioctl for device"
    // Apparently the 485 ioctl doesn't exist.
    if (modbus_rtu_set_serial_mode(_modbusrtu, MODBUS_RTU_RS485) < 0)
        throw n_u::IOException(getDeviceName(), "modbus_rtu_set_serial_mode", errno);
#endif

    _thread = new ModbusThread(getDeviceName(), _modbusrtu, _regaddr, _nvars, _pipefds[1], rate);

    _thread->setRealTimeFIFOPriority(40);

#ifdef IGNORE_THREAD_ERROR
    try {
        _thread->unblockSignal(SIGUSR1);
        _thread->start();
    }
    catch(n_u::Exception& e) {
    }
#else
    _thread->unblockSignal(SIGUSR1);
    _thread->start();
#endif

#endif
}

#ifdef HAVE_LIBMODBUS
void ModbusRTU::close()
{
    _thread->interrupt();

    if (_pipefds[0] >= 0) ::close(_pipefds[0]);
    if (_pipefds[1] >= 0) ::close(_pipefds[1]);
    _pipefds[0] = _pipefds[1] = -1;

    // _iodevice->close() will do nothing if its fd is < 0
    _iodevice->setFd(-1);       

    // _thread->cancel();
    _thread->kill(SIGUSR1);
    _thread->join();

    modbus_close(_modbusrtu);
    modbus_free(_modbusrtu);

    SerialSensor::close();
}

int ModbusRTU::ModbusThread::run() throw()
{
    uint16_t data[_nvars + 1];

    // These toLittle conversions are here just in case this code
    // is run on a big-endian system (not likely now without armbe
    // machines). They do don't swap anything on a little-endian system.
    data[0] = toLittle->uint16Value(_nvars);

    int nCRCError = 0;
    int sampleMsec = MSECS_PER_SEC / _rate;

    for (; !isInterrupted(); ) {
        errno = 0;
        int nreg = modbus_read_registers(_mb, _regaddr, _nvars, data + 1);
        if (nreg < 0) {
            if (errno == 0) {
                if (nCRCError++ > 5) {
                    // When this thread exits the Nidas sensor handler
                    // will get a timeout reading from the pipe, which will
                    // then re-open the sensor.
                    return RUN_EXCEPTION;
                }
                n_u::IOException e(_devname, "modbus_read_registers", "checksum error");
                WLOG(("") << e.what());
                continue;
            }
            else {
                n_u::IOException e(_devname, "modbus_read_registers", errno);
                PLOG(("") << e.what());
                // Another thread exit, resulting in sensor timeout
                return RUN_EXCEPTION;
            }
        }
        nCRCError = 0;

        for (int i = 0; i < _nvars; i++)
            data[i+1] = toLittle->uint16Value(data[i+1]);

        if (::write(_pipefd, data, (_nvars + 1) * sizeof(uint16_t)) < 0) {
            n_u::IOException e(_devname + " pipe", "write", errno);
            PLOG(("Error: ") << e.what());
        }

        n_u::sleepUntil(sampleMsec);
    }
    return RUN_OK;
}
#endif

bool ModbusRTU::process(const Sample* samp,list<const Sample*>& results)
  throw()
{
    assert(samp->getType() == CHAR_ST);

    uint16_t nwords = samp->getDataLength() / sizeof(uint16_t);

    if (nwords == 0) return false;

    const uint16_t* inwords = (const uint16_t*) samp->getConstVoidDataPtr();

    SampleT<float>* outsamp = getSample<float>(_nvars);

    outsamp->setTimeTag(samp->getTimeTag());
    outsamp->setId(_stag->getId());

    adjustTimeTag(_stag, outsamp);

    uint16_t nvarsin = toHost->uint16Value(inwords[0]);
    if (nvarsin != _nvars)
        WLOG(("%s: ",getDeviceName().c_str()) << "nvarsin=" << nvarsin << ", _nvars=" << _nvars);

    if (nvarsin != nwords-1)
        WLOG(("%s: ",getDeviceName().c_str()) << "nvarsin=" << nvarsin << ", nwords=" << nwords);

    const vector<Variable*>& vars = _stag->getVariables();

    for (int i = 0; i < std::min(std::min((int)nvarsin, (int)_nvars), (int)nwords - 1); i++) {
        // treat value as signed int16
        float val = (float) toHost->int16Value(inwords[i+1]);

        // var->convert screens for missing values, converts, then
        // screens for min and max values
        Variable* var = vars[i];
        var->convert(outsamp->getTimeTag(), &val);
        outsamp->getDataPtr()[i] = val;
    }
    results.push_back(outsamp);
    return true;
}
