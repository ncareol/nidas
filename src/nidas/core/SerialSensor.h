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
#ifndef NIDAS_DYNLD_SERIALSENSOR_H
#define NIDAS_DYNLD_SERIALSENSOR_H

#include "CharacterSensor.h"
#include "LooperClient.h"
#include "SerialPortIODevice.h"
#include "Prompt.h"

using namespace nidas::util; 

namespace nidas { namespace core {

/**
 *  Autoconfig helpers
 */
struct WordSpec
{
	WordSpec(int dbits, Termios::parity prty, int stopbits)
	: dataBits(dbits), parity(prty), stopBits(stopbits) {}
    int dataBits;
    Termios::parity parity;
    int stopBits;
};

struct SensorCmdArg
{
	std::string strArg;
	int intArg;
    char charArg;
	bool argIsString;
    bool argIsChar;
	bool argIsNull;

	SensorCmdArg() : strArg(""), intArg(0), charArg(0), argIsString(false), argIsChar(false), argIsNull(true) {}
	explicit SensorCmdArg(const int iarg) : strArg(""), intArg(iarg), charArg(0), argIsString(false), argIsChar(false), argIsNull(false) {}
	explicit SensorCmdArg(const char carg) : strArg(""), intArg(0), charArg(carg), argIsString(false), argIsChar(true), argIsNull(false) {}
	explicit SensorCmdArg(const std::string sarg) : strArg(sarg), intArg(0), charArg(0), argIsString(true), argIsChar(false), argIsNull(false) {}
	explicit SensorCmdArg(const char* cstrarg)  : strArg(cstrarg), intArg(0), charArg(0), argIsString(true), argIsChar(false), argIsNull(false) {}

	SensorCmdArg(const SensorCmdArg& rRight) : strArg(), intArg(), charArg(), argIsString(), argIsChar(), argIsNull() 
    {
        *this = rRight;
    }

	SensorCmdArg& operator=(const SensorCmdArg& rRight)
	{
		if (this != &rRight) {
			strArg = rRight.strArg;
			intArg = rRight.intArg;
            charArg = rRight.charArg;
			argIsString = rRight.argIsString;
            argIsChar = rRight.argIsChar;
			argIsNull = rRight.argIsNull;
		}

		return *this;
	}
	bool operator==(const SensorCmdArg& rRight)
	{
		return (strArg == rRight.strArg && intArg == rRight.intArg && charArg == rRight.charArg 
                && argIsString == rRight.argIsString && argIsChar == rRight.argIsChar);
	}
	bool operator!=(const SensorCmdArg& rRight) {return !(*this == rRight);}
};

const int NULL_COMMAND = -1;

struct SensorCmdData {
	SensorCmdData() : cmd(NULL_COMMAND), arg(0) {}
	SensorCmdData(int initCmd, SensorCmdArg initArg) : cmd(initCmd), arg(initArg) {}
    int cmd;
    SensorCmdArg arg;
};

enum CFG_MODE_STATUS {
    NOT_ENTERED,
    ENTERED_RESP_CHECKED,
    ENTERED
};

enum AUTOCONFIG_STATE {
	AUTOCONFIG_UNSUPPORTED,
	WAITING_IDLE,
	AUTOCONFIG_STARTED,
	CONFIGURING_COMM_PARAMETERS,
	COMM_PARAMETER_CFG_SUCCESSFUL,
	COMM_PARAMETER_CFG_UNSUCCESSFUL,
	CONFIGURING_SCIENCE_PARAMETERS,
	SCIENCE_SETTINGS_SUCCESSFUL,
	SCIENCE_SETTINGS_UNSUCCESSFUL,
	AUTOCONFIG_SUCCESSFUL,
	AUTOCONFIG_UNSUCCESSFUL
};

/**
 * Support for a sensor that is sending packets on a TCP socket, a UDP socket, a
 * Bluetooth RF Comm socket, or a good old RS232/422/485 serial port.
 * A SerialSensor builds the appropriate IODevice depending on the prefix
 * of the device name, see buildIODevice() below.
 * A SerialSensor also creates a SampleScanner, depending on the device name.
 * 
 * Configuration and opening of a SerialSensor is done in the following sequence:
 * 1.  After the configuration XML is being parsed, an instance of SerialSensor() is
 *     created, and the virtual method fromDOMElement() is called.
 *     SerialPort::fromDOMElement() configures the nidas::util::Termios of this SerialSensor
 *     from the attributes of the sensor DOM element.  fromDOMElement() also calls
 *     CharacterSensor::fromDOMElement(), which calls setMessageParameters().
 *     Note that the device is not open yet.
 * 2.  virtual method SerialSensor::open is called, which calls CharacterSensor::open().
 *     CharacterSensor::open calls DSMSensor::open().
 *     DSMSensor::open() does the following, calling virtual methods:
 *         iodev = buildIODevice();
 *             Creates the appropriate IODevice, see SerialSensor::buildIODevice().
 *         iodev->open()
 *             If the IODevice is a SerialPortIODevice, then SerialPortIODevice::open()
 *             calls UnixIODevice::open() which opens the device file.
 *             SerialPortIODevice::open() then applies the Termios to the opened file descriptor.
 *         scanr = buildSampleScanner();
 *              Calls CharacterSensor::buildSampleScanner().
 *         scanr->init()
 */
class SerialSensor : public CharacterSensor
{

public:

    /**
     * No arg constructor.  Typically the device name and other
     * attributes must be set before the sensor device is opened.
     */
    SerialSensor();
    SerialSensor(const PortConfig& rInitPortConfig);

    ~SerialSensor();

    /**
     * Expose the Termios. One must call applyTermios() to
     * apply any changes to the serial port.
     */
    nidas::util::Termios& termios() { return _desiredPortConfig.termios; }

    /**
     * Get a read-only copy of the Termios.
     */
    const nidas::util::Termios& getTermios() const { return _desiredPortConfig.termios; }

    /**
     * Calls CharacterSensor::buildSampleScanner(), and then sets the 
     * per-byte transmission delay for that scanner:
     * SampleScanner::setUsecsPerByte().
     */
    SampleScanner* buildSampleScanner()
        throw(nidas::util::InvalidParameterException);

    /**
     * Calls CharacterSensor::buildIODevice() to creates an IODevice depending 
     * on the device name prefix:
     * name prefix      type of IODevice
     * inet:            TCPSocketIODevice
     * sock:            TCPSocketIODevice
     * usock:           UDPSocketIODevice
     * btspp:           BluetoothRFCommSocketIODevice
     * 
     * If UnixIODevice type is returned, then check 
     * /dev/ttyDSM:     SerialPortIODevice
     *
     * If a SerialPortIODevice is created, the Termios of this SerialSensor is
     * copied to the device, which will then be applied when the device is opened.
     */
    IODevice* buildIODevice() throw(nidas::util::IOException);
    void setSerialPortIODevice(SerialPortIODevice* pSerialPortIODevice) {_serialDevice = pSerialPortIODevice;}
    SerialPortIODevice* getSerialPortIODevice() {return _serialDevice;}

    /**
     * Open the device connected to the sensor. This calls
     * CharacterSensor::open(), and then sets up the port
     * prompting if it is required.
     */
    void open(int flags) throw(nidas::util::IOException,
        nidas::util::InvalidParameterException);

    /*
     * Close the device connected to the sensor.
     */
    void close() throw(nidas::util::IOException);

    /* 
     * Flush the serial port, if this is a serial device
     */
    void serPortFlush(const int flags=0);

    /**
     * If the underlying IODevice is a SerialPortIODevice, update
     * the current Termios to the device.
     */
    void applyTermios() throw(nidas::util::IOException);

    /**
     * Set message separator and message length parameters, which are used to
     * parse and time-tag samples from the IODevice.
     */
    // void setMessageParameters(unsigned int len,const std::string& sep, bool eom)
    //     throw(nidas::util::InvalidParameterException,nidas::util::IOException);

    void printStatus(std::ostream& ostr) throw();

    /**
     * Is prompting active, i.e. isPrompted() is true, and startPrompting
     * has been called?
     */
    bool isPrompting() const { return _prompting; }

    /**
     * Start the prompters. They can be started and stopped multiple
     * times once a SerialSensor is opened.
     */
    void startPrompting() throw(nidas::util::IOException);

    void stopPrompting() throw(nidas::util::IOException);

    virtual void sendInitString() throw(nidas::util::IOException);

    // get auto config parameters, after calling base class implementation
    void fromDOMElement(const xercesc::DOMElement* node)
    	throw(nidas::util::InvalidParameterException);

    /**
     * If the underlying IODevice is a SerialPortIODevice,
     * return the value of SerialPortIODevice::getUsecsPerByte(),
     * otherwise return 0, which means no timetag correction
     * for transmission delay will be applied.
     */
    int getUsecsPerByte() const;

    /**
     * Get/set the working PortConfig - this means the ones in SerialPortIODevice and SerialXcvrCtrl, if they exist.
     * 
     * Assumption: If SerialSensor is a traditional, RS232/422/485, device, then the associated PortConfig in 
     *             SerialPortIODevice is all that matters. This should cover "non-sensor" devices such as GPS, cell 
     *             modems, etc, since they may be traditional serial devices, but not have a SerialXcvrCtrl object 
     *             associated with them. 
     */
    PortConfig getPortConfig();
    void setPortConfig(const PortConfig newPortConfig);

    PortConfig getDefaultPortConfig() {return _defaultPortConfig;}
    void setDefaultPortConfig(const PortConfig& newDefaultPortConfig) {_defaultPortConfig = newDefaultPortConfig;}
    PortConfig getDesiredPortConfig() {return _desiredPortConfig;}
    void setDesiredPortConfig(const PortConfig& newDesiredPortConfig) {_desiredPortConfig = newDesiredPortConfig;}

    void applyPortConfig();

    AUTOCONFIG_STATE getAutoConfigState() {return _autoConfigState; }
    AUTOCONFIG_STATE getSerialConfigState() {return _serialState; }
    AUTOCONFIG_STATE getScienceConfigState() {return _scienceState; }
    void setAutoConfigSupported() { _autoConfigSupported = true; }
    bool supportsAutoConfig() { return _autoConfigSupported; }
    void setAutoConfigEnabled() { _autoConfigEnabled = true; }
    bool getAutoConfigEnabled() { return _autoConfigEnabled; }

    int readEntireResponse(void *buf, int len, int msecTimeout,
                           bool checkPrintable=false, int retryTimeoutFactor=2);
    int readResponse(void *buf, int len, int msecTimeout, bool checkPrintable=false,
                     bool backOffTimeout=true, int retryTimeoutFactor=2);
    /*********************************************************
     * Utility function to drain the rest of a response from 
     * a sensor. This is useful for when the entire response 
     * isn't needed, but would get in the way of subsequent 
     * sensor operations.
     ********************************************************/
    void drainResponse()
    {
        const int drainTimeout = 5;
        const bool checkJunk = true;
        const int BUF_SIZE = 100;
        char buf[BUF_SIZE];
        for (int i=0; 
             i < 10 && readEntireResponse(buf, BUF_SIZE, drainTimeout, checkJunk);
             ++i);
    }

protected:

    /**
     * Perform whatever is necessary to initialize prompting right
     * after the device is opened.
     */
    void initPrompting() throw(nidas::util::IOException);

    /**
     * Shutdown prompting, typically done when a device is closed.
     */
    void shutdownPrompting() throw(nidas::util::IOException);

    void unixDevInit(int flags) throw(nidas::util::IOException);

    /**
     *  autoconfig specific methods which are entirely implemented in
     *  SerialSensor.
     */
    void doAutoConfig();
    void setTargetPortConfig(PortConfig& target, int baud, int dataBits, Termios::parity parity, int stopBits,
												 int rts485, PortType ptype, PortTermination termination);
    bool isDefaultConfig(const PortConfig& rTestConfig) const;
    bool findWorkingSerialPortConfig();
    bool testDefaultPortConfig();
    bool sweepCommParameters();
    int bytesAvailable()
    {
        int bytesInReadBuffer = 0;
        if (_serialDevice) {
            bytesInReadBuffer = _serialDevice->getBytesAvailable();
        }
        return bytesInReadBuffer;
    }

    bool doubleCheckResponse();
    bool configureScienceParameters();
    void printResponseHex(int numCharsRead, const char* respBuf);

    static std::string autoCfgToStr(AUTOCONFIG_STATE autoState);

    /**
     * These autoconfig methods do nothing, unless overridden in a subclass
     * Keep in mind that most subclasses will override all of these methods.
     * In particular, supportsAutoConfig() must be overridden to return true,
     * otherwise, the other virtual methods will not get called at all.
     */
    virtual CFG_MODE_STATUS enterConfigMode() 
    {
        DLOG(("SerialSensor::enterConfigMode(): No device subclass has overridden enterConfigMode()")); 
        return ENTERED; 
    }

    virtual void exitConfigMode() {}
    CFG_MODE_STATUS getConfigMode() {return _configMode;}
    void setConfigMode(CFG_MODE_STATUS newCfgMode) {_configMode = newCfgMode;}

    virtual bool checkResponse() { return true; }
    virtual bool installDesiredSensorConfig(const PortConfig& /*rDesiredConfig*/) { return true; };
    virtual void sendScienceParameters() {}
    virtual bool checkScienceParameters() { return true; }
    virtual void updateMetaData()
    {
        NLOG(("Sensor does not support metadata collection."));
    }

    void initAutoConfig();
    void fromDOMElementAutoConfig(const xercesc::DOMElement* node);
    void checkXcvrConfigAttribute(const XDOMAttr& rAttr);
    void checkTermiosConfigAttribute(const XDOMAttr& rAttr);

    // Autoconfig subclasses calls supportsAutoConfig() to set this to true
    bool _autoConfigSupported;
    // This class sets this attribute to true if it encounters an 
    // autoconfig XML tag.
    bool _autoConfigEnabled;

    /*******************************************************
     * Aggregate serial port configuration
     * 
     * Holds a default port config (both Termios config and XcvrConfig) 
     * which may be passed down by an auto-config subclass, 
     * which may be further updated by fromDOMElement().
     * 
     * No other operations occur on this attribute, since if 
     * this SerialSensor truly is a traditional serial port, then 
     * all the necessary operations such as set/get/applyPortConfig
     * occur in SerialPortIODevice. 
     *******************************************************/
    PortConfig _desiredPortConfig;

    /*
     * Containers for holding the possible serial port parameters which may be used by a sensor
     */
    typedef std::list<PortType> PortTypeList;
    typedef std::list<int> BaudRateList;
    typedef std::list<WordSpec> WordSpecList;

	PortTypeList _portTypeList;			// list of PortTypes this sensor may make use of
    BaudRateList _baudRateList;			// list of baud rates this sensor may make use of
    WordSpecList _serialWordSpecList;	// list of serial word specifications (databits, parity, stopbits) this sensor may make use of

    AUTOCONFIG_STATE _autoConfigState;
    AUTOCONFIG_STATE _serialState;
    AUTOCONFIG_STATE _scienceState;
    AUTOCONFIG_STATE _deviceState;
    CFG_MODE_STATUS _configMode;

private:
    /*
     *  The initial PortConfig sent by the subclass when constructing SerialSensor
     */
    PortConfig _defaultPortConfig;

    /**
     *  Non-null if the underlying IODevice is a SerialPortIODevice.
     */
    SerialPortIODevice* _serialDevice;

    /**
     * Prompter attaches a Prompt to a SerialSensor as a LooperClient.
     */
    class Prompter: public nidas::core::LooperClient
    {
    public:
        Prompter(SerialSensor* sensor): _sensor(sensor),
            _prompt(), _promptPeriodMsec(0), _promptOffsetMsec(0)
        {}

        ~Prompter();

        void setPrompt(const Prompt& prompt);
        const Prompt& getPrompt() const { return _prompt; }

        void setPromptPeriodMsec(const int);
        int getPromptPeriodMsec() const { return _promptPeriodMsec; }

        void setPromptOffsetMsec(const int);
        int getPromptOffsetMsec() const { return _promptOffsetMsec; }

        /**
         * Method called by Looper in order to send a prompt.
         */
        void looperNotify() throw();

    private:
        SerialSensor* _sensor;
        Prompt _prompt;
        int _promptPeriodMsec;
        int _promptOffsetMsec;

        /** copy not necessary */
        Prompter(const Prompter&);

        /** assignment not necessary */
        Prompter& operator=(const Prompter&);
    };

    std::list<Prompter*> _prompters;

    bool _prompting;

    /** No copying. */
    SerialSensor(const SerialSensor&);

    /** No assignment. */
    SerialSensor& operator=(const SerialSensor&);

};

}}	// namespace nidas namespace core

#endif
