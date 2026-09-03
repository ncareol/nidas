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

std::string to_string(AUTOCONFIG_STATE autoState);


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
 *
 * SerialSensor, for when it is actually connected to a serial port, can
 * specify multiple possible configurations for the serial port.  These serial
 * port configurations can be added when the SerialSensor subclass is
 * constructed, or they can be added from the XML configuration.  They are
 * meant to be typical or likely serial port configurations used by a sensor.
 * For sensors which support autoconfiguration, where the sensor class knows
 * how to test that sensor communication settings are working, the sensor can
 * search the port configurations to find the one which works.
 *
 * Port configurations from the XML are inserted ahead of the configurations
 * added in the code.  Once all port configurations are added, the first one
 * becomes the active configuration by default.
 *
 * For sensors which know how to query and possible change the settings in the
 * sensor, those settings can be specified in a default SensorConfig object.
 * The SensorConfig can be modified by the XML.  When the sensor is opened,
 * and after the serial communication parameters have been settled on both
 * sides of the connection, autoconfig sensors can then apply the SensorConfig
 * settings to the sensor.
 *
 * Here is the nominal sequence for opening a sensor with a serial connection:
 *
 * :open: Open sensor device.
 *
 * :set active config:
 *
 * Set the first serial config as the active config, identified by index 0.
 * If the sensor does not support searching ("sweeping") port configs, skip to
 * :query:
 *
 * :test: Test that the sensor is responding.
 *
 * If not responding, set active config to the next config, go back to :test:
 * If no working config found, go back to first config, and skip to :query:
 *
 * :change sensor serial config:
 *
 * If the working portconfig is not the required one, and the sensor supports
 * changing the sensor communications parameters, change the sensor to that
 * config.  Either way, continue to :query:
 *
 * :query:
 *
 * If sensor supports it, query for the metadata, record it, and publish it.
 * This can fail if no working serial communications could be established or
 * if there are sensor problems.  Either way, continue to the next step.
 *
 * :data:
 *
 * Put sensor into data mode, such as with an init string or command.  This
 * step may not do anything if the sensor is expected to come up sending data
 * and has no init string.
 *
 * :done:
 *
 * Sensor open, start reading samples.
 *
 * The sensor XML config can be used to extend the default serial port configs
 * in the <autoconfig> element.  The serial port configs are in a separate
 * element, <portconfig>, since they are specific to serial connections to the
 * sensor.  Sensor configuration settings are specified in a <sensorconfig>
 * element.  Any serial settings in the <serialSensor> element automatically
 * become the first serial config in the list of settings, followed by serial
 * settings in the <autoconfig> element (for backwards compatibility with the
 * original scheme), followed by <portconfig> elements inside <autoconfig>.
 *
 * For example, here is an example of the planned XML scheme to specify
 * autoconfiguration settings for the Gill 2D sonic:
 *
 * @code
 * <autoconfig>
 *
 * <sensorconfig outputrate='10'/>
 *
 * <portconfig baud="19200" parity="none" databits="8" stopbits="1"/>
 *
 * <portconfig baud="9600" parity="none" databits="8" stopbits="1"/>
 *
 * <portconfig baud="9600" parity="even" databits="7" stopbits="1"/>
 *
 * </autoconfig>
 * @endcode
 *
 * For reference, the full <portconfig> element can have these attributes:
 *
 *   baud="9600" databits="7" parity="even" stopbits="1" porttype="rs232"
 *   termination="noterm" rts485="0"
 *
 * Note that serial port configurations can have porttype and termination
 * settings even if the sensor is not connected to a DSM port where those
 * settings can be changed.  However, if a DSM port does need those settings,
 * then the ones in the active <portconfig> will be uesd, even if just the
 * default of RS232.
 *
 * In future iterations, we may want to add a 'disable' or 'ignore' flag to
 * elements like autoconfig, portconfig, and sensorconfig, so they can be left
 * in the XML config for reference but easily disabled for a specific
 * deployment.  Also, rather than a sensor class always trying to force a
 * sensor into the first communications settings, that might be better if
 * explicitly requested in the config, such as with an attribute like
 * required=true.
 *
 * At any time a SerialSensor has an _active_ serial port config and a set of
 * alternate configs.  The _active_ config is the one applied to the
 * SerialPortIODevice when one has been attached to this sensor.  The _active_
 * config can be changed at any time with setPortConfig().
 *
 * The set of alternate configs is modified and accessed with
 * getPortConfigs(), addPortConfig(), and replacePortConfigs().
 */
class SerialSensor : public CharacterSensor
{

public:

    /**
     * No arg constructor.  Typically the device name and other
     * attributes must be set before the sensor device is opened.
     */
    SerialSensor();

    ~SerialSensor();

    /**
     * Expose the Termios. One must call applyTermios() to
     * apply any changes to the serial port.
     */
    nidas::util::Termios& termios() { return _portconfig.termios; }

    /**
     * Get a read-only copy of the Termios.
     */
    const nidas::util::Termios& getTermios() const { return _portconfig.termios; }

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

    /**
     * @defgroup PortConfig Serial port configuration methods
     */
    /**@{*/

    /**
     * Container type for alternate serial port configs for this sensor.
     */
    using PortConfigList = std::vector<PortConfig>;

    /**
     * Return the serial port configs associated with this sensor as a vector.
     * The reference is only valid until the list is changed with
     * addPortConfig() or replacePortConfigs().
     */
    PortConfigList
    getPortConfigs();

    /**
     * Return the first PortConfig associated with this sensor.  This is the
     * config that would be tried first as the active config when the sensor
     * serial port is opened.  If the list of available port configs is empty,
     * this returns a default PortConfig().
     */
    PortConfig
    getFirstPortConfig();

    /**
     * Insert @p pc into the available configs at the port config index.
     * 
     * See setPortConfigIndex().  This does not affect the active port config.
     */
    void
    addPortConfig(const PortConfig& pc);

    /**
     * Set the port config index to @p idx.
     * 
     * The port config index starts at zero and is incremented on each call to
     * addPortConfig().  Setting it to zero allows new configs to be inserted
     * ahead of any existing configs.  This is used to add XML port configs in
     * front of any configs added by the sensor subclass, so the XML configs
     * take precedence.  Pass @p idx as 0 to start adding at the front, pass
     * -1 to set it to the end.
     */
    void
    setPortConfigIndex(int idx=0);

    /**
     * Replace the entire list of available serial port configs.  This does
     * not affect the active port config.  The index is set to add new port
     * configs to the end of the list, as in setPortConfigIndex(-1).
     */
    void
    replacePortConfigs(const PortConfigList& pconfigs);

    /**
     * Get the _active_ PortConfig, the one last set by setPortConfig().  As
     * mentioned for setPortConfig(), the _active_ PortConfig may or may not
     * be in the list of available port configs for this sensor.  The _active_
     * PortConfig is just an empty default until the sensor is opened (when
     * the first of the requested port configs becomes active) or until
     * setPortConfig() is called.
     */
    PortConfig getPortConfig();

    /**
     * Set the _active_ PortConfig on this SerialSensor, and apply it to a
     * SerialPortIODevice if attached.  If a SerialPortIODevice is not
     * attached to this SerialSensor, then the _active_ PortConfig is what
     * will be applied when one is created.  See applyPortConfig().
     *
     * The _active_ config is typically set from among the set of available
     * configs.  When the SerialSensor is opened, the first of the available
     * configs becomes the active config, but a sensor may also cycle through
     * available configs by setting each one as the active config with
     * setPortConfig().
     *
     * Setting the active config does not change the list of available
     * configs, so the active config may or may not appear in the available
     * configs.
     *
     * When the active port config is changed, it is explicitly set to raw
     * mode with a raw length of 1, and then it is applied to the
     * SerialPortIODevice, if any.
     */
    void setPortConfig(const PortConfig& portconfig);

    void applyPortConfig();

    /**
     * Turn on the hardware power for this serial sensor, if available. This
     * and the powerOff() method are defined only for serial sensors at the at
     * the moment, since those are the only sensors with hardware power
     * control.  Maybe they should be virtual methods on DSMSensor.
     */
    void powerOn();

    /**
     * Turn off the hardware power for this serial sensor, if available.
     */
    void powerOff();

    /**@}*/

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

    /**
     * Search the available serial port configurations for one which works.
     *
     * The sensor must implement autoconfig methods to indicate when a sensor
     * is successfully communicating over the active port config.
     * 
     * @return true A working PortConfig was found and is currently active.
     * @return false No working PortConfig, last tried config is active.
     */
    bool findWorkingSerialPortConfig();

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

    // Autoconfig subclasses calls supportsAutoConfig() to set this to true
    bool _autoConfigSupported;
    // This class sets this attribute to true if it encounters an 
    // autoconfig XML tag.
    bool _autoConfigEnabled;

    // _active_ port config
    PortConfig _portconfig;

    PortConfigList _portconfigs;
    unsigned int _pcindex;

    AUTOCONFIG_STATE _autoConfigState;
    AUTOCONFIG_STATE _serialState;
    AUTOCONFIG_STATE _scienceState;
    AUTOCONFIG_STATE _deviceState;
    CFG_MODE_STATUS _configMode;

private:
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

    SerialSensor(const SerialSensor&) = delete;
    SerialSensor& operator=(const SerialSensor&) = delete;

};

}}	// namespace nidas namespace core

#endif
