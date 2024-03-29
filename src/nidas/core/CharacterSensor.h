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

#ifndef NIDAS_CORE_CHARACTERSENSOR_H
#define NIDAS_CORE_CHARACTERSENSOR_H

#include "DSMSensor.h"
#include "Prompt.h"
#include <nidas/util/util.h>

namespace nidas { namespace core {

class AsciiSscanf;
class Sample;
class TimetagAdjuster;

/**
 * Implementation of support for a sensor which generates
 * character output. Typically this character output is
 * somewhat free-form and is then parsed in the process()
 * method.
 */
class CharacterSensor: public DSMSensor {

public:

    /**
     * No arg constructor.
     */
    CharacterSensor();

    virtual ~CharacterSensor();

    /**
     * Creates an IODevice depending on the device name prefix:
     * name prefix      type of IODevice
     * inet:            TCPSocketIODevice
     * sock:            TCPSocketIODevice
     * usock:           UDPSocketIODevice
     * btspp:           BluetoothRFCommSocketIODevice
     * all others       UnixIODevice
     *
     * @throws nidas::util::IOException
     */
    IODevice* buildIODevice();

    /**
     * Creates a SampleScanner for this DSMSensor depending on the
     * device name prefix:
     * name prefix      type of SampleScanner
     * usock:           DatagramSampleScanner
     * all others       MessageStreamScanner
     *
     * @throws nidas::util::InvalidParameterException
     */
    SampleScanner* buildSampleScanner();

    /**
     * Open the sensor device port for real-time sampling.
     * This should only be done after the CharacterSensor
     * has been initialized with fromDOMElement.
     *
     * @throws nidas::util::IOException
     * @throws nidas::util::InvalidParameterException
     */
    void open(int flags);

    /**
     * Implementation of DSMSensor::validate for a Character Sensor.
     * Currently initializes all the prompts for this sensor.
     *
     * @throws nidas::util::InvalidParameterException
     */
    void validate();

    /**
     * Initialize the CharacterSensor instance for post-processing.
     * This should only be done after the CharacterSensor
     * has been initialized with fromDOMElement.
     * Calls validateSscanfs.
     *
     * @throws nidas::util::InvalidParameterException
     */
    void init();

    /**
     * The messageSeparator is the string of bytes that sensor
     * generates to separate messages.
     *
     * @throws nidas::util::IOException
     * @throws nidas::util::InvalidParameterException
     **/
    virtual void setMessageParameters(unsigned int length, const std::string& val,
                                      bool eom);

    /**
     * Get message separator with backslash sequences replaced by their
     * intended character.
     */
    const std::string& getMessageSeparator() const
    {
	return _messageSeparator;
    }

    /**
     * Get message separator with backslash sequences added back.
     */
    const std::string getBackslashedMessageSeparator() const
    {
        return nidas::util::addBackslashSequences(_messageSeparator);
    }

    bool getMessageSeparatorAtEOM() const
    {
        return _separatorAtEOM;
    }

    int getMessageLength() const
    {
	return _messageLength;
    }

    /**
     * Prompting Sensors can have multiple prompts and rates.
     * Add another prompt and rate to this sensor.
     * @param promptString May contain backslash escape sequences.
     * @param promptRate prompts/sec.
     */
    virtual void addPrompt(const std::string& promptString, double promptRate,
            double promptOffset)
    {
        Prompt prompt;
        prompt.setString(promptString);
        prompt.setRate(promptRate);
        prompt.setOffset(promptOffset);

        _prompts.push_back(prompt);
        _prompted = true;
//cerr<< "pushed back prompt.  String = "<<promptString<<" rate= "<<promptRate;
    }

    const std::list<Prompt>& getPrompts() const { return _prompts;}

    /**
     * Is this a prompted sensor.  Will be true if setPromptString()
     * has been called with a non-empty string, and setPromptRate()
     * has been called with a rate other than IRIG_ZERO_HZ.
     */
    virtual bool isPrompted() const { return _prompted; }

    /**
     * Is prompting active, i.e. isPrompted() is true, and startPrompting
     * has been called?
     */
    virtual bool isPrompting() const { return false; }

    /**
     * @throws nidas::util::IOException
     **/
    virtual void startPrompting()
    {
        throw nidas::util::IOException(getName(),
		"startPrompting","not supported");
    }

    /**
     * @throws nidas::util::IOException
     **/
    virtual void stopPrompting()
    {
        throw nidas::util::IOException(getName(),
		"stopPrompting","not supported");
    }

    /**
     * @throws nidas::util::IOException
     **/
    virtual void togglePrompting()
    {
        if (isPrompting()) stopPrompting();
        else startPrompting();
    }

    /**
     * Set the initialization string(s) for this sensor.
     * The init string may contain backslash escape sequences
     * like the prompt string.
     */
    void setInitString(const std::string& val) { _initString = val; }

    const std::string& getInitString() const { return _initString; }

    /**
     * @throws nidas::util::IOException
     **/
    virtual void sendInitString();

    bool getNullTerminated() const
    {
        if (!getSampleScanner()) return false;
        return getSampleScanner()->getNullTerminate();
    }

    /**
     * How many samples have resulted in complete scanf failures -
     * nothing parsed, because the sensor messages do not
     * correspond to the sscanf format.
     */
    int getNumScanfFailures() const {
        return _scanfFailures;
    }

    /**
     * How many samples have been partially scanned, because
     * a character in the middle of a message conflicts
     * with the sscanf format.
     */
    int getNumScanfPartials() const {
        return _scanfPartials;
    }

    /**
     * Return the list of AsciiSscanfs requested for this CharacterSensor.
     * This list is only valid after the init() method has been called.
     */
    const std::list<AsciiSscanf*>& getScanfers() const
    {
        return _sscanfers;
    }

    /**
     * The maximum number of fields in any of the AsciiSscanfs for
     * this CharacterSensor. Prior to scanning a message, a sample
     * of this size must be allocated.
     */
    int getMaxScanfFields() const { return _maxScanfFields; }

    /**
     * Virtual method to check that the Sscanfs for this CharacterSensor
     * are OK.  The default implementation checks that the number of parse
     * fields in each sscanf parser matches the number of variables in
     * the associated output sample.
     *
     * @throws nidas::util::InvalidParameterException
     */
    virtual void validateSscanfs();

    /**
     * Process a raw sample, which in this case means do
     * a sscanf on the character string contents, creating
     * a processed sample of binary floating point data.
     *
     * @throw()
     */
    virtual bool process(const Sample*,std::list<const Sample*>& result);

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void fromDOMElement(const xercesc::DOMElement*);

    /**
     * @return: true if this CharacterSensor as been configured to
     *          use one or more  AsciiSscanf objects to parse its data.
     */
    bool doesAsciiSscanfs();

protected:

    /**
     * Set the rate at which `<sensor>` prompts are sent to this sensor.
     * This will be set on a CharacterSensor if a `<prompt>` element
     * is found for `<sensor>`, not as a sub-element of `<sample>`.
     */
    void setPromptRate(const double val) {_promptRate = val;}

    double getPromptRate() const { return (_promptRate);}

    void setPromptOffset(const double val) {_promptOffset = val;}

    double getPromptOffset()const { return (_promptOffset);}

    /**
     * Set the `<sensor>` prompt string for this sensor.
     * The prompt string may contain backslash escape sequences and
     * null characters, so be carefull when copying to a char*.
     */
    void setPromptString(const std::string& val) { _promptString = val; }

    const std::string& getPromptString() const { return (_promptString);}

    virtual int scanSample(AsciiSscanf* sscanf, const char* inputstr,
			   float* data_ptr);

    /**
     * Search through the AsciiSscanf instances attached to this sensor,
     * looking for the next scanner which parses at least one variable from
     * the given raw sample.  The search picks up after the last scanner
     * which matched a sample.  The parsed values are added to a new
     * Sample, and a pointer to the new Sample is returned.  If stag_out is
     * non-null, it is set to the SampleTag pointer for the AsciiSscanf
     * which matched.  If no scanners match this sample, then this returns
     * null.  The returned Sample has a reference which must be freed by
     * the caller or passed on.  Any unparsed variables in the returned
     * sample are filled with NaN using trimUnparsed().  The time tag of
     * the new Sample is set to the time of raw sample @p samp.  No other
     * time tag adjustments or variable conversions are applied, that is up
     * to the caller.
     **/
    SampleT<float>*
    searchSampleScanners(const Sample* samp, SampleTag** stag_out=0) throw();

    /**
     * Apply TimetagAdjuster and lag adjustments to the timetag of the
     * given sample.
     **/
    void
    adjustTimeTag(SampleTag* stag, SampleT<float>* outs);

    std::map<const SampleTag*, TimetagAdjuster*> _ttadjusters;

private:

    std::string _messageSeparator;

    bool _separatorAtEOM;

    int _messageLength;

    std::list<Prompt> _prompts;

    std::string _promptString;

    double _promptRate;

    double _promptOffset;

    std::list<AsciiSscanf*> _sscanfers;

    std::list<AsciiSscanf*>::const_iterator _nextSscanfer;

    int _maxScanfFields;

    int _scanfFailures;

    int _scanfPartials;

    bool _prompted;

    /**
     * String that is sent once after sensor is opened.
     */
    std::string _initString;

    std::string _emptyString;

    /** No copying */
    CharacterSensor(const CharacterSensor&);

    /** No assignment */
    CharacterSensor& operator=(const CharacterSensor&);

};

}}	// namespace nidas namespace core

#endif
