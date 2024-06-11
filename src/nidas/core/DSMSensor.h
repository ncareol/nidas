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
#ifndef NIDAS_CORE_DSMSENSOR_H
#define NIDAS_CORE_DSMSENSOR_H

#include "SampleClient.h"
#include "SampleSourceSupport.h"
#include "SampleScanner.h"
#include "SampleTag.h"
#include "IODevice.h"
#include "DOMable.h"
#include "Dictionary.h"
#include "VariableIndex.h"

#include <nidas/util/IOException.h>
#include <nidas/util/InvalidParameterException.h>

#include <xmlrpcpp/XmlRpc.h>

#include <string>
#include <list>

#include <fcntl.h>

namespace nidas { namespace core {

class DSMConfig;
class Parameter;
class CalFile;
class Looper;

/**
 * DSMSensor provides the basic support for reading, processing
 * and distributing samples from a sensor attached to a DSM.
 *
 * Much of the implementation of a DSMSensor is delegated
 * to an IODevice and a SampleScanner, which are
 * built with virtual methods.
 * DSMSensor can fill in its attributes
 * from an XML DOM element with fromDOMElement().
 * One attribute of a DSMSensor is the system device
 * name associated with this sensor, e.g. "/dev/xxx0".
 * Once a device name has been set, then a user of this sensor
 * can call open(),  and then ioctl(), read() and write(),
 * which are carried out by the IODevice.
 *
 * SampleClient's can call
 * addRawSampleClient()/removeRawSampleClient() if they want to
 * receive raw Samples from this sensor during real-time operations.
 *
 * SampleClient's can also call
 * addSampleClient()/removeSampleClient() if they want to
 * receive processed Samples from this sensor.
 *
 * A common usage of a DSMSensor is to add it to a SensorHandler
 * object with SensorHandler::addSensorPort().
 * When the SensorHandler::run method has determined that there is data
 * available on a DSMSensor's file descriptor, it will then call
 * the readSamples() method which reads the samples from the
 * IODevice, and forwards the raw and processed
 * samples to all associated SampleClient's of this DSMSensor.
 *
 */
class DSMSensor : public SampleSource, public SampleClient, public DOMable
{

public:

    /**
     * Constructor.
     */

    DSMSensor();

    virtual ~DSMSensor();

    /**
     * Set the DSMConfig for this sensor.
     */
    void setDSMConfig(const DSMConfig* val);

    /**
     * What DSMConfig am I associated with?
     */
    const DSMConfig* getDSMConfig() const
    {
        return _dsm;
    }

    /**
     * What Site am I associated with?
     */
    const Site* getSite() const;

    /**
     * Set the Site for this sensor, then propagate the setting to any
     * sample tags and variables.
     */
    void setSite(Site* site);

    /**
     * First resolve the given @p site_name to a Site, then set it with
     * setSite().  Throw InvalidArgumentException if the name cannot be found
     * among sites in the containing Project.
     */
    void setSite(const std::string& site_name);

    /**
     * Set the name of the system device that the sensor
     * is connected to.
     * @param val Name of device, e.g. "/dev/xxx0".
     */
    virtual void setDeviceName(const std::string& val)
    {
       _devname = val;
    }

    /**
     * Fetch the name of the system device that the sensor
     * is connected to.
     */
    virtual const std::string& getDeviceName() const
    {
	return _devname;
    }

    /**
     * Set the class name. In the usual usage this
     * method is not used, and getClassName() is
     * over-ridden in a derived class to return
     * a constant string.
     */
    virtual void setClassName(const std::string& val)
    {
        _className = val;
    }

    /**
     * Fetch the class name.
     */
    virtual const std::string& getClassName() const
    {
        return _className;
    }

    /**
     * Set the name of the catalog entry for this sensor.
     */
    virtual void setCatalogName(const std::string& val)
    {
        _catalogName = val;
    }

    /**
     * Fetch the name of the catalog entry for this sensor.
     */
    virtual const std::string& getCatalogName() const
    {
        return _catalogName;
    }

    /**
     * Fetch the DSM name.
     */
    const std::string& getDSMName() const;

    /**
     * Return a name that should fully identify this sensor. This
     * name could be used in informative messages. The returned name
     * has this format:
     *  dsmName:deviceName.
     */
    virtual std::string getName() const
    {
        return getDSMName() + ':' + getDeviceName();
    }

    /**
     * Location string. A DSMConfig also has a Location.
     * If the location has not been set for this DSMSensor,
     * then the location of the DSMConfig will be returned.
     */
    const std::string& getLocation() const;

    void setLocation(const std::string& val) { _location = val; }

    /**
     * Sensor suffix, which is added to variable names.
     * It is only necessary to have a sensor suffix
     * to make variable names unique.  For example, if
     * there are multiple sensors at the same height,
     * with common variable names, then one can
     * use a sensor suffix to make unique names.
     * The sensor suffix is the second dot-separated
     * word in a variable name, where only the first
     * word is required:
     *  variable[.sensor][.height][.site]
     */
    const std::string& getSuffix() const { return _suffix; }

    void setSuffix(const std::string& val);

    int getStation() const 
    {
        return _station;
    }

    void setStation(int val);

    /**
     * Set sensor height above ground via a string which is added
     * to variable names. 
     * @param val String containing sensor height and units
     * in meters (m) or centimeters(cm), e.g. "15m".
     * This height string is added to all the variable names,
     * with a "." separator, so that a variable "u" for this
     * sensor becomes "u.15m".
     */
    void setHeight(const std::string& val);

    /**
     * Set sensor height above ground.
     * @param val height above ground, in meters.
     * The height is added to all the variable names,
     * with a "." separator, so that a variable "u" for this
     * sensor becomes "u.15m".
     */
    void setHeight(float val);

    /**
     * Get sensor height above ground in a string.
     * @return Height of sensor above ground,  e.g. "15m".
     */
    const std::string& getHeightString() const { return _heightString; }

    /**
     * Get sensor height above ground.
     * @return Height of sensor above ground, in meters. Nan if unknown.
     */
    float getHeight() const { return _height; }

    /**
     * Set sensor depth below ground via a string which is added
     * to variable names. 
     * @param val String containing sensor below and units
     * in meters (m) or centimeters(cm), e.g. "5cm".
     * This depth string is added to all the variable names,
     * with a "." separator, so that a variable "Tsoil" for this
     * sensor becomes "Tsoil.5cm".
     */
    void setDepth(const std::string& val);

    /**
     * Set sensor depth below ground.
     * @param val depth below ground, in meters.
     * The depth is converted to centimeters and is added
     * to all the variable names, with a "." separator, so
     * that a variable "Tsoil" for this
     * sensor becomes "Tsoil.5cm".
     */
    void setDepth(float val);

    /**
     * Get sensor depth below ground in a string.
     * @return Depth of sensor below ground,  e.g. "5cm".
     */
    const std::string& getDepthString() const { return _depthString; }

    /**
     * Get sensor depth below ground.
     * @return Depth of sensor below ground, in meters.
     */
    float getDepth() const { return -_height; }

    /**
     * Full sensor suffix, the concatenation of the
     * sensor suffix, if any, and the height or depth
     * string, if any.  The full sensor suffix are
     * words 2 and 3 in the dot-separated name:
     *  variable[.sensor][.height][.site]
     */
    const std::string& getFullSuffix() const { return _fullSuffix; }

    /**
     * Utility function to expand ${TOKEN} or $TOKEN fields
     * in a string with their value from getTokenValue().
     * If curly brackets are not used, then the TOKEN should
     * be delimited by a '/', a '.' or the end of string,
     * e.g.:  xxx/yyy/$ZZZ.dat
     */
    std::string expandString(const std::string& input) const
    {
        return _dictionary.expandString(input);
    }

    /**
     * Implement a lookup for tokens that I know about, like $HEIGHT.
     * For other tokens, call getDSMConfig()->getTokenValue(token,value);
     */
    bool getTokenValue(const std::string& token,std::string& value) const
    {
        return _dictionary.getTokenValue(token,value);
    }

    const Dictionary& getDictionary() const
    {
        return _dictionary;
    }

    /**
     * Implementation of SampleSource::getRawSampleSource().
     * Return the SampleSource for raw samples from this DSMSensor.
     * A DSMSensor is only a SampleSource of raw samples when
     * running in real-time, not during post-processing.
     */
    SampleSource* getRawSampleSource()
    {
        return &_rawSource;
    }

    /**
     * Implementation of SampleSource::getProcessedSampleSource().
     * Return the SampleSource for processed samples from this DSMSensor.
     */
    SampleSource* getProcessedSampleSource()
    {
        return this;
    }

    /**
     * Convenience function to get my one-and-only raw SampleTag().
     * Equivalent to:
     *  getRawSampleSource()->getSampleTags()->front()
     */
    const SampleTag* getRawSampleTag() const
    {
        return &_rawSampleTag;
    }

    /**
     * Add a SampleTag to this sensor.  DSMSensor will own the SampleTag.
     * Throw an exception the DSMSensor cannot support
     * the sample (bad rate, wrong number of variables, etc).
     * Note that a SampleTag may be changed after it has
     * been added. addSampleTag() is called when a sensor is initialized
     * from the sensor catalog.  The SampleTag may be modified later
     * if it is overridden in the actual sensor entry.
     * For this reason, it is wise to wait to scan the SampleTags
     * of a DSMSensor in the validate(), init() or open() methods,
     * which are invoked after fromDOMElement.
     *
     * @throws nidas::util::InvalidParameterException
     */
    virtual void addSampleTag(SampleTag* val);

    /**
     * Remove val from the list of SampleTags, and delete it.
     */
    virtual void removeSampleTag(SampleTag* val);

    /**
     * Implementation of SampleSource::getSampleTags().
     */
    std::list<const SampleTag*> getSampleTags() const
    {
        return _source.getSampleTags();
    }

    /**
     * Non-const method to get a list of non-const pointers to SampleTags.
     */
    std::list<SampleTag*>& getSampleTags()
    {
        return _sampleTags;
    }

    SampleTagIterator getSampleTagIterator() const
    {
        return SampleTagIterator(this);
    }

    /**
     * Add a SampleClient to this SampleSource.  The pointer
     * to the SampleClient must remain valid, until after
     * it is removed.
     */
    void addSampleClient(SampleClient* c) throw()
    {
        return _source.addSampleClient(c);
    }

    /**
     * Remove a SampleClient from this SampleSource
     * This will also remove a SampleClient if it has been
     * added with addSampleClientForTag().
     */
    void removeSampleClient(SampleClient* c) throw()
    {
        return _source.removeSampleClient(c);
    }

    /**
     * Add a SampleClient to this SampleSource.  The pointer
     * to the SampleClient must remain valid, until after
     * it is removed.
     */
    void addSampleClientForTag(SampleClient* client,const SampleTag* tag) throw()
    {
        return _source.addSampleClientForTag(client,tag);
    }

    /**
     * Remove a SampleClient for a given SampleTag from this SampleSource.
     * The pointer to the SampleClient must remain valid, until after
     * it is removed.
     */
    void removeSampleClientForTag(SampleClient* client,const SampleTag* tag) throw()
    {
        return _source.removeSampleClientForTag(client,tag);
    }

    /**
     * How many SampleClients are currently in my list.
     */
    int getClientCount() const throw()
    {
        return _source.getClientCount();
    }

    /**
     * Implementation of SampleClient::flush(). This is where a DSMSensor's process()
     * method could send out any buffered results that might be ready.
     */
    void flush() {}

    /**
     * Distribute a raw sample which has been read from my
     * file descriptor in real time.
     */
    void distributeRaw(const Sample* s)
    {
        _rawSource.distribute(s);
    }

    const SampleStats& getSampleStats() const
    {
        return _source.getSampleStats();
    }

    virtual int getReadFd() const
    {
	if (_iodev) return _iodev->getReadFd();
	return -1;
    }

    virtual int getWriteFd() const
    {
	if (_iodev) return _iodev->getWriteFd();
	return -1;
    }

    /**
     * Whether to reopen this sensor on an IOException.
     * The base method returns true.  Over-ride if
     * a reopen should not be attempted.
     */
    virtual bool reopenOnIOException() const { return true; }

    /**
     * Set the various levels of the sensor identification.
     * A sensor ID is a 32-bit value comprised of four parts:
     * 6-bit sample type id (not used by DSMSensor), 10-bit DSM id,
     * and 16-bit sensor+sample ids.
     */
    void setId(dsm_sample_id_t val) { _id = SET_FULL_ID(_id,val); }
    void setSensorId(unsigned int val) { _id = SET_SPS_ID(_id,val); }
    void setDSMId(unsigned int val) { _id = SET_DSM_ID(_id,val); }

    /**
     * Get the various levels of the samples identification.
     * A sample tag ID is a 32-bit value comprised of four parts:
     * 6-bit sample type id(not used by DSMSensor), a 10-bit DSM id,
     * 16-bit sensor+sample ids.
     */
    dsm_sample_id_t  getId()      const { return GET_FULL_ID(_id); }
    unsigned int getDSMId()   const { return GET_DSM_ID(_id); }
    unsigned int getSensorId() const { return GET_SPS_ID(_id); }

    /**
     * Set desired latency, providing some control
     * over the response time vs buffer efficiency tradeoff.
     * Setting a latency of 1/10 sec means buffer
     * data in the driver for a 1/10 sec, then send the data
     * to user space. Generally it should be
     * set before doing a sensor open().
     *
     * @param val Latency, in seconds.
     * @throws nidas::util::InvalidParameterException
     */
    virtual void setLatency(float val)
    {
        _latency = val;
    }

    virtual float getLatency() const { return _latency; }

    /**
     * Set the sensor timeout value in milliseconds.
     * A value of 0 means no timeout (e.g. infinite).
     * If no data is received for this period, then the sensor
     * is closed, and re-opened.  For efficiency reasons, the system
     * may not actually detect a sensor timeout of less than 1 second,
     * so setting it to less than 1000 milliseconds will likely not
     * reduce the time before a sensor timeout is detected.
     */
    virtual void setTimeoutMsecs(int val)
    {
        _timeoutMsecs = val;
    }

    virtual int getTimeoutMsecs() const
    {
        return _timeoutMsecs;
    }

    int getTimeoutCount() const
    {
        return _nTimeouts;
    }

    void incrementTimeoutCount()
    {
        _nTimeouts++;
    }

    /**
     * Add a parameter to this DSMSensor. DSMSensor
     * will then own the pointer and will delete it
     * in its destructor. If a Parameter exists with the
     * same name, it will be replaced with the new Parameter.
     */
    void addParameter(Parameter* val);

    /**
     * Get list of parameters.
     */
    const std::list<const Parameter*>& getParameters() const
    {
	return _constParameters;
    }

    /**
     * Fetch a parameter by name. Returns a NULL pointer if
     * no such parameter exists.
     */
    const Parameter* getParameter(const std::string& name) const;

    /**
     * Factory method for an IODevice for this DSMSensor.
     * Must be implemented by derived classes.
     *
     * @throws nidas::util::IOException
     */
    virtual IODevice* buildIODevice() = 0;

    /**
     * Set the IODevice for this sensor. DSMSensor then
     * owns the pointer and will delete it in its destructor.
     */
    void setIODevice(IODevice* val)
    {
        _iodev = val;
    }

    /**
     * Factory method for a SampleScanner for this DSMSensor.
     * Must be implemented by derived classes.
     *
     * @throws nidas::util::InvalidParameterException
     */
    virtual SampleScanner* buildSampleScanner() = 0;

    /**
     * Set the SampleScanner for this sensor. DSMSensor then
     * owns the pointer and will delete it in its destructor.
     */
    void setSampleScanner(SampleScanner* val)
    {
        _scanner = val;
    }

    /**
     * validate() is called once on a DSMSensor after it has been
     * configured, but before open() or init() are called.
     *
     * @throws nidas::util::InvalidParameterException
     */
    virtual void validate();

    /**
     * Open the device. flags are a combination of O_RDONLY, O_WRONLY.
     *
     * @throws nidas::util::IOException,nidas::util::InvalidParameterException
     */
    virtual void open(int flags);

    /**
     * Initialize the DSMSensor. This method is called on a
     * DSMSensor after it is fully configured, and before the
     * process method is called.  This is where a DSMSensor should
     * do any required initialization of anything that is
     * used by the process() method.  If processed samples
     * are not requested from this DSMSensor, then NIDAS apps
     * typically do not call init().
     *
     * @throws nidas::util::InvalidParameterException
     */
    virtual void init();

    /**
     * How do I want to be opened.  The user can ignore it if they want to.
     * @return one of O_RDONLY, O_WRONLY or O_RDWR.
     */
    virtual int getDefaultMode() const
    {
        return _defaultMode;
    }

    virtual void setDefaultMode(int val)
    {
        _defaultMode = val;
    }

    /**
     * How many bytes are available to read on this sensor.
     * @see IODevice::getBytesAvailable().
     *
     * @throws nidas::util::IOException
     */
    virtual size_t getBytesAvailable() const
    {
        return _iodev->getBytesAvailable();
    }

    /**
     * Read from the device (duh). Behaves like the read(2) system call,
     * without a file descriptor argument, and with an IOException.
     *
     * @throws nidas::util::IOException
     */
    virtual size_t read(void *buf, size_t len)
    {
        return _iodev->read(buf,len);
    }

    /**
     * Read from the device with a timeout.
     *
     * @throws nidas::util::IOException
     */
    virtual size_t read(void *buf, size_t len,int msecTimeout)
    {
        return _iodev->read(buf,len,msecTimeout);
    }

    /**
     * Write to the device (duh). Behaves like write(2) system call,
     * without a file descriptor argument, and with an IOException.
     *
     * @throws nidas::util::IOException
     */
    virtual size_t write(const void *buf, size_t len)
    {
        return _iodev->write(buf,len);
    }

    /**
     * Perform an ioctl on the device. request is an integer
     * value which must be supported by the device. Normally
     * this is a value from a header file for the device.
     *
     * @throws nidas::util::IOException
     */
    virtual void ioctl(int request, void* buf, size_t len)
    {
        _iodev->ioctl(request,buf,len);
    }

    /**
     * close my associated device.
     *
     * @throws nidas::util::IOException;
     */
    virtual void close();

    /**
     * Read samples from my associated file descriptor,
     * and distribute() them to my RawSampleClient's.
     * This method is called by SensorHander, when select/poll
     * indicates that data is available on the file descriptor
     * returned by getReadFd().
     * This is a convienence method which does a
     * readBuffer() to read available data from the DSMSensor
     * into a buffer, and then repeatedly calls nextSample()
     * to extract all samples out of that buffer.
     *
     * @throws nidas::util::IOException;
     */
    virtual bool readSamples();

    /**
     * Extract the next sample from the buffer. Returns a
     * null pointer if there are no samples left in the buffer.
     */
    virtual Sample* nextSample()
    {
        return _scanner->nextSample(this);
    }

    /**
     * Return the next sample. Buffer(s) will be read if necessary.
     * 
     * @throws nidas::util::IOException
     */
    virtual Sample* readSample();

    /**
     * A DSMSensor can be used as a SampleClient,
     * meaning it receives its own raw samples.
     * In real-time operations, a DSMSensor can be added
     * as a raw SampleClient of itself, using addRawSampleClient().
     * In post-processing, a DSMSensor typically receives
     * samples with its own sample id from a SampleSorter.
     * receive() then applies further processing via the process()
     * method.
     */
    bool receive(const Sample *s);

    /**
     * Apply further necessary processing to a raw sample
     * from this DSMSensor. Return the resultant sample(s)
     * in result.  The default implementation
     * of process() simply puts the input Sample into result.
     */
    virtual bool process(const Sample*,std::list<const Sample*>& result) = 0;

    void printStatusHeader(std::ostream& ostr);
    virtual void printStatus(std::ostream&);
    void printStatusTrailer(std::ostream& ostr);

    /**
     * Update the sensor sampling statistics.  Should be called
     * every periodUsec by a user of this sensor.
     * @param periodUsec Statistics period.
     */
    void calcStatistics(unsigned int periodUsec)
    {
        if (_scanner) _scanner->calcStatistics(periodUsec);
    }

    unsigned int getMaxSampleLength() const
    {
	if (_scanner) return _scanner->getMaxSampleLength();
	return 0;
    }

    unsigned int getMinSampleLength() const
    {
        if (_scanner) return _scanner->getMinSampleLength();
	return 0;
    }

    float getObservedSamplingRate() const
    {
        if (_scanner) return _scanner->getObservedSamplingRate();
	return 0.0;
    }

    float getObservedDataRate() const
    {
        if (_scanner) return _scanner->getObservedDataRate();
	return 0.0;
    }

    size_t getBadTimeTagCount() const
    {
	if (_scanner) return _scanner->getBadTimeTagCount();
	return 0;
    }

    VariableIterator getVariableIterator() const;

    /**
     * Crawl through the DOM tree for a DSMSensor to find
     * the class name - scanning the catalog entry if
     * necessary.
     *
     * @throws nidas::util::InvalidParameterException
     */
    static const std::string getClassName(const xercesc::DOMElement* node,
                                          const Project* project);

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void fromDOMElement(const xercesc::DOMElement*);

    /**
     * @throws xercesc::DOMException
     **/
    xercesc::DOMElement*
    toDOMParent(xercesc::DOMElement* parent, bool complete) const;

    /**
     * @throws xercesc::DOMException;
     **/
    xercesc::DOMElement*
    toDOMElement(xercesc::DOMElement* node, bool complete) const;

    /**
     * Set the type name of this sensor, e.g.:
     * "ACME Model 99 Mach7 Particle Disambiguator".
     * This is meant for descriptive purposes only,
     * and is not meant to change the behavior of a sensor object.
     */
    virtual void setTypeName(const std::string& val)
    {
        _typeName = val;
    }

    /**
     * Get the type name of this sensor.
     */
    virtual const std::string& getTypeName(void) const
    {
        return _typeName;
    }

    /**
     * getDuplicateIdOK will be true if it is OK for samples from this sensor
     * to have identical IDs to samples from another sensor.  That other sensor
     * must also agree that dupicateIdOK are OK.  This can be useful
     * if one may not be certain of the device name, e.g. /dev/ttyUSB*, that
     * the system will assign to a sensor may use, but there are
     * identifiers in the data returned that allow one to sort things out.
     * In general, getDuplicateID is false.
     */
    bool getDuplicateIdOK() const
    {
        return _duplicateIdOK;
    }

    /**
     * Set the duplicate ID attribute of this DSMSensor.
     */
    void setDuplicateIdOK(bool val)
    {
        _duplicateIdOK = val;
    }

    virtual bool getApplyVariableConversions() const
    {
        return _applyVariableConversions;
    }

    virtual void setApplyVariableConversions(bool val)
    {
        _applyVariableConversions = val;
    }

    virtual int getDriverTimeTagUsecs() const
    {
        return _driverTimeTagUsecs;
    }

    virtual void setDriverTimeTagUsecs(int val)
    {
        _driverTimeTagUsecs = val;
    }

    /**
     * Add a calibration file for this DSMSensor. After this
     * method is finished, DSMSensor will own the pointer, and
     * will delete it in the DSMSensor destructor.
     */
    void addCalFile(CalFile* val);

    /**
     * Return the collection of CalFiles, mapped by name.
     */
    const std::map<std::string,CalFile*>& getCalFiles()
    {
        return _calFiles;
    }

    /**
     * Return a CalFile by its getName(). Will return NULL if not found.
     */
    CalFile* getCalFile(const std::string& name);

    /**
     * Remove all CalFiles.
     */
    void removeCalFiles();

    /**
     * Method invoked when the DSMEngineIntf XmlRpcServer receives a "SensorAction"
     * request, with a "device" string matching the string that this DSMSensor
     * registers via DSMEngine::registerSensorWithXmlRpc(string,DSMServer*).
     * The default base class method does nothing.
     */
    virtual void executeXmlRpc(XmlRpc::XmlRpcValue&, XmlRpc::XmlRpcValue&)
    {}

    static void deleteLooper();

    /**
     * We'll allow derived classes and calibration applications to change the
     * SampleTags, so this method returns a list of non-constant
     * SampleTags.
     */
    virtual const std::list<SampleTag*>& getNonConstSampleTags()
    {
        return _sampleTags;
    }


    bool allowOpen()
    {
        return _openable;
    }

    /**
     * Read into my SampleScanner's buffer.
     *
     * @throws nidas::util::IOException
     */
    bool readBuffer()
    {
        bool exhausted;
        _scanner->readBuffer(this,exhausted);
        return exhausted;
    }

    /**
     * Read into my SampleScanner's buffer.
     *
     * @throws nidas::util::IOException 
     */
    bool readBuffer(int msecTimeout)
    {
        bool exhausted;
        _scanner->readBuffer(this,exhausted, msecTimeout);
        return exhausted;
    }


protected:

    /**
     * Clear the internal buffer.
     */
    void clearBuffer()
    {
        _scanner->clearBuffer();
    }


    IODevice* getIODevice() const { return _iodev; }

    SampleScanner* getSampleScanner() const { return _scanner; }

    void setFullSuffix(const std::string& val);

    /**
     * Fetch a pointer to a static instance of a Looper thread.
     * Use this Looper for periodic callbacks, as for prompting,
     * in support of a DSMSensor.
     */
    static Looper* getLooper();

    /**
     * Return the sampling lag for this sensor in microseconds.
     * A positive lag means one should adjust the sample time tags
     * for this sensor earlier in time to achieve a better estimate
     * of the actual time to be associated for each sample.
     * Derived classes can use this method in their process method
     * to correct for inherent, constant, sampling lags of a sensor.
     * A fixed sample lag, in fractional seconds, can be set for a
     * sensor in the XML:
     * @code
     * <sensor>
     *     <parameter name="lag" type = "float" value="0.186"/>
     * </sensor>
     * @endcode
     * The DSMSensor::fromDOM() method parses this parameter
     * and sets the value of the lag.
     */
    virtual int getLagUsecs() const
    {
        return _lag;
    }
    /**
     * Return the sampling lag for this sensor in seconds.
     * See getLagUsecs().
     */
    virtual double getLagSecs() const
    {
        return (double)_lag / USECS_PER_SEC;
    }

    /**
     * Set the sampling lag for this sensor in seconds. This lag
     * should then used to correct the timetags of the processed
     * samples in the process() method of derived classes.
     * Note that this lag is not used to alter the timetags of the
     * raw samples. Raw samples are saved with the un-altered timetag
     * that was determined at the moment they were sampled.
     *
     * The lag is stored as a signed integer of microseconds, so lags should
     * be between += 2147 seconds. No warning or exception is given
     * if the value exceeds that limit. If your lag is greater than
     * that I suggest you junk your sensor!
     * A positive lag means one should adjust the sample time tags
     * for this sensor earlier in time to achieve a better estimate
     * of the actual time to be associated for each sample.
     * A fixed sample lag, in fractional seconds, can be set for a
     * sensor in the XML:
     * @code
     * <parameter name="lag" type = "float" value="0.186"/>
     * @endcode
     * The DSMSensor::fromDOM() method parses this parameter
     * and calls this method to set the lag.  process() methods
     * in derived classes must apply this lag value. The DSMSensor
     * base class does not adjust time tags of processed samples.
     */
    virtual void setLagSecs(double val)
    {
        _lag = (int) rint(val * USECS_PER_SEC);
    }

    /**
     * Perform variable conversions for the variables in @p stag whose
     * values and sample time have been set in @p outs.  This method can be
     * used by subclasses to apply any variable conversions associated with
     * the variables in the given SampleTag, using a general algorithm
     * which loops over each value in each variable.  The min/max value
     * limits of a variable are applied also, so if a variable value is
     * converted but lies outside the min/max range, the value is set to
     * floatNAN.  Typically this can be the last step applied to the output
     * sample of a sensor's process() method, and note that the sample time
     * must already be set in the output sample @p outs, since that time
     * will be used to look up conversions in calibration files.
     *
     * If @p results is non-null, then the converted values are written
     * into @p results instead of overwriting the values in @p outs.  This
     * is used when one variable's results may be used to filter other
     * variables without replacing the one variable's results.  An example
     * is the Ifan variable in the TRH WisardMote. @p results must point to
     * enough memory to hold all of the values in @p outs, including for
     * any Variables with multiple values (getLength() > 1).
     *
     * See Variable::convert().
     **/
    void
    applyConversions(SampleTag* stag, SampleT<float>* outs, float* results=0);

    /**
     * Fill with floatNAN all the values past @p nparsed values in output
     * sample @p outs, and trim the length of the sample to match the
     * length of the variables in SampleTag @p stag.  process() methods
     * which support partial scans of messages can use this to finalize an
     * output sample according to the SampleTag that was matched with it
     * and the number of values parsed.
     **/
    void
    trimUnparsed(SampleTag* stag, SampleT<float>* outs, int nparsed);

    /**
     * Search all the sample tags for a variable whose name starts with the
     * given prefix, and return it's index in the list of variables in the
     * sample tag.  If no such variable is found, return -1.
     **/
    VariableIndex
    findVariableIndex(const std::string& vprefix);

    /**
     * Whether this sensor is allowed to be opened.  Most cases the answer is
     * yes/true.  But certain sensors should not be opened due to implementation.
     * e.g. DSMArincSensor when used in conjunction with the UDPArincSensor for
     * then Alta ENET appliance.
     *
     * The fallout from setting this to false is that the sensor will not be
     * added to SensorHandler.
     */
    bool _openable;


private:

    /**
     * DSMSensor does provide public support for
     * SampleSource::addSampleTag(const SampleTag* val).
     *
     * @throws nidas::util::InvalidParameterException
     */
    void addSampleTag(const SampleTag* val);

    /**
     * DSMSensor does not provide public support for
     * SampleSource::removeSampleTag(const SampleTag* val)
     *
     * @throw()
     */
    void removeSampleTag(const SampleTag* val);

    std::string _devname;

    class MyDictionary : public Dictionary {
    public:
        MyDictionary(DSMSensor* sensor): _sensor(sensor) {}
        MyDictionary(const MyDictionary& x): Dictionary(),_sensor(x._sensor) {}
        MyDictionary& operator=(const MyDictionary& rhs)
        {
            if (&rhs != this) {
                *(Dictionary*)this = rhs;
                _sensor = rhs._sensor;
            }
            return *this;
        }
        bool getTokenValue(const std::string& token, std::string& value) const;
    private:
        DSMSensor* _sensor;
    } _dictionary;


    IODevice* _iodev;

    int _defaultMode;

    /**
     * Class name attribute of this sensor. Only used here for
     * informative messages.
     */
    std::string _className;

    std::string _catalogName;

    /**
     * Sensor suffix, which is added to variable names.
     */
    std::string _suffix;

    std::string _heightString;

    std::string _depthString;

    float _height;

    /**
     * Concatenation of sensor suffix, and the height or depth string
     */
    std::string _fullSuffix;

    std::string _location;

    SampleScanner* _scanner;

    const DSMConfig* _dsm;

    /**
     * A DSMSensor can be associated with a different Site than the DSM which
     * records it.  If this is not set, then the site will be the DSM site.
     */
    const Site* _site{nullptr};

    /**
     * Id of this sensor.  Raw samples from this sensor will
     * have this id.
     */
    dsm_sample_id_t _id;

    SampleTag _rawSampleTag;

    std::list<SampleTag*> _sampleTags;

    SampleSourceSupport _rawSource;

    SampleSourceSupport _source;

    // toggle flag for zebra striping printStatus
    static bool zebra;

    float _latency;

    /**
     * Map of parameters by name.
     */
    std::map<std::string,Parameter*> _parameters;

    /**
     * List of const pointers to Parameters for providing via
     * getParameters().
     */
    std::list<const Parameter*> _constParameters;

    std::map<std::string,CalFile*> _calFiles;

    std::string _typeName;

    int _timeoutMsecs;

    bool _duplicateIdOK;

    bool _applyVariableConversions;

    int _driverTimeTagUsecs;

    int _nTimeouts;

    static Looper* _looper;

    static nidas::util::Mutex _looperMutex;

    int _lag;

    int _station;

private:

    // no copying
    DSMSensor(const DSMSensor& x);

    // no assignment
    DSMSensor& operator=(const DSMSensor& x);

};

}}	// namespace nidas namespace core

#endif
