// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2005, Copyright University Corporation for Atmospheric Research
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
#ifndef NIDAS_CORE_VARIABLE_H
#define NIDAS_CORE_VARIABLE_H

#include "DOMable.h"
#include "Site.h"
#include "VariableConverter.h"
#include <nidas/util/InvalidParameterException.h>

#include <string>
#include <list>

namespace nidas { namespace core {

class SampleTag;
class Parameter;
class Site;

/**
 * Class describing a sampled variable.
 */
class Variable : public DOMable
{
public:

    typedef enum var_type { CONTINUOUS, COUNTER, CLOCK, OTHER, WEIGHT } type_t;

    /**
     * Create a variable.
     */
    Variable();

    /**
     * Copy constructor.
     */
    Variable(const Variable&);

    /**
     * Assignment.
     */
    Variable& operator=(const Variable&);

    virtual ~Variable();

    /**
     * Equivalence operator for Variable, checks
     * equivalence of their length, the variables's names
     * without any site suffix, and their sites.
     */
    bool operator == (const Variable& x) const;

    bool operator != (const Variable& x) const;

    /**
     * A more loose check of the equivalence of two variables,
     * This will also return a value of true if either Site is NULL.
     */
    bool closeMatch(const Variable& x) const;

    /**
     * Set the Site where this variable was measured.
     */
    void setSite(const Site* val)
    {
        _site = val;
        if (_site && getStation() == 0) setSiteSuffix(_site->getSuffix());
        else setSiteSuffix("");
    }

    /**
     * Return the measurement site for this variable.
     */
    const Site* getSite() const
    {
        return _site;
    }

    /**
     * Station number of this variable:
     * @return -1: not specified
     *          0: the "non" site, or the project-wide site,
     *        1-N: a site number
     *
     * Variables can be grouped by a station number.
     *
     * A measured variable is associated with a Site.
     * A Site also has a non-negative station number.
     * A Site may have an associated suffix, like ".bigtower".
     *
     * By default, all DSMSensors at a site will be assigned the site's station number.
     * To support the logical grouping of variables into stations, the station number
     * of a DSMSensor can be set to a different value than the Site's number.
     *
     * All variables sampled by a DSMSensor will get assigned its station number.
     *
     * If the station number of a variable is 0, then the site suffix will be appended
     * to the variable name.
     *
     * Otherwise, if the station number of a variable is positive, the site suffix is
     * not appended to its name.  In this way we can have variables with a common name,
     * like "RH.2m" at different sites, which will have different station numbers.
     */
    int getStation() const { return _station; }

    void setStation(int val)
    {
        _station = val;
        if (_site && _station == 0) setSiteSuffix(_site->getSuffix());
        else setSiteSuffix("");
    }

    type_t getType() const { return _type; }

    void setType(type_t val) { _type = val; }

    /**
     * What sample am I a part of?
     */
    const SampleTag* getSampleTag() const { return _sampleTag; }

    /**
     * Set the sample tag pointer.  Variable does not own the pointer.
     * This also copies attributes from the sample, such as
     * site and station if they have not been set on this Variable.
     */
    void setSampleTag(const SampleTag* val);

    /**
     * Convenience routine to get the SampleTag rate.
     */
    float getSampleRate() const;

    /**
     * Set the name prefix.  The full variable name will
     * be the concatenation of: prefix + suffix + site
     */
    void setPrefix(const std::string& val)
    {
        _prefix = val;
        updateName();
    }

    const std::string& getPrefix() const { return _prefix; }

    /**
     * Variable suffix, which is added to the name.  The full variable name
     * is created from  prefix + suffix + siteSuffix.  suffix and/or
     * siteSuffix may be empty strings.  The suffix commonly
     * comes from the DSMSensor::getFullSuffix(), containing
     * an optional sensor suffix and a height/depth string.
     */
    const std::string& getSuffix() const { return _suffix; }

    void setSuffix(const std::string& val)
    {
        _suffix = val;
        updateName();
    }

    /**
     * Site suffix, which is added to the name.
     */
    const std::string& getSiteSuffix() const { return _siteSuffix; }

    /**
     * Set the full name. This clears the suffix and site
     * portions of the name.  Once this is called,
     * the suffix and siteSuffix fields are cleared.
     */
    void setName(const std::string& val)
    {
        _suffix.clear();
        _siteSuffix.clear();
        _prefix = val;
        updateName();
    }

    const std::string& getName() const { return _name; }

    /**
     * Get the name without the site suffix.
     */
    const std::string& getNameWithoutSite() const { return _nameWithoutSite; }

    /**
     * Descriptive, long name, e.g. "Ambient air temperature".
     */
    void setLongName(const std::string& val) { _longname = val; }

    const std::string& getLongName() const { return _longname; }

    /**
     * The A2D channel for this variable.
     */
    void setA2dChannel( int val ) { _A2dChannel = val; }

    int getA2dChannel() const { return _A2dChannel; }

    /**
     * The string discription of the units for this variable.
     */
    void setUnits(const std::string& val) { _units = val; }

    const std::string& getUnits() const { return _units; }

    /**
     * How many values in this variable?
     */
    unsigned int getLength() const { return _length; }

    void setLength(unsigned int val) { _length = val; }

    /**
     * Set the VariableConverter for this Variable.
     * Variable will own the pointer and will delete it.
     */
    void setConverter(VariableConverter* val)
    {
        delete _converter;
        _converter = val;
    }

    const VariableConverter* getConverter() const { return _converter; }

    VariableConverter* getConverter() { return _converter; }

    /**
     * Apply the conversions for this Variable to the floats pointed to by
     * @p values, putting the results in @p results, up to @p nvalues.  Any
     * calibrations are looked up using time @p ttag.  The min/max value
     * limits of a variable are applied also, so if a variable value is
     * converted but lies outside the min/max range, the value is set to
     * floatNAN.  
     *
     * If @p nvalues is null, then multiple values will be converted up to
     * the length of the Variable, otherwise only the number given in @p
     * nvalues.  If @p results is null, then the converted values will be
     * written back into the array pointed to by @p values.  Both @p values
     * and @p results must point to enough memory to hold @p nvalues
     * floats.  Typically the results of the conversions are written over
     * the values passed in, and @p values points into the memory in a
     * DSMSensor output sample.  However, if a sensor needs custom handling
     * of the results, then the inputs and outputs can be kept separate.
     * For example, The TRH Wisard sensor can keep the Ifan value as it was
     * but convert T and RH to floatNAN if the Ifan result is NAN, meaning
     * Ifan was bad or out of range.  It is safe to pass @p results equal
     * to @p values, the effect is the same as calling convert() with @p
     * results=null.
     *
     * Usually this conversion is the last step applied to the output
     * sample of a sensor's process() method, and this method is called
     * from the DSMSensor::applyConversions() method.
     *
     * If this Variable has a Site (getSite() returns non-null) and the
     * Site's getApplyVariableConversions() returns false, then no
     * conversions are applied, not even the min/max limit
     * filter. Otherwise the conversions are applied.
     *
     * Originally, the getApplyVariableConversions() was called on
     * DSMSensor, usually inside a sensor subclass process() method.
     * However, that value is usually copied from the Site's applyCals
     * attribute.  So the value of the flag should be the same for both
     * Site and DSMSensor, unless there are some uses out there which
     * change the DSMSensor's value, such as in the sensor subclass itself.
     * However, if a Sensor does not want to apply conversions, then it can
     * just avoid calling any conversion methods.
     *
     * The original code limit filtered values whether conversion was
     * applied or not, and there was some inconsistency in the domain of
     * the min/max values: Did they apply to values before or after
     * conversion?  The convention will be that min/max are in the
     * converted space, so now no filtering happens based on min/max if
     * conversions not applied first.  It also makes sense that disabling
     * conversions, presumably to see the more raw values, implies not
     * limit filtering any values either.  In practice this change should
     * have little effect, since the 'applyCals' Site attribute is rarely
     * enabled.
     *
     * The return value points just past the last element in @p values to
     * be converted.  Note that @p results is not passed by reference,
     * therefore if non-null it must be advanced after the call to
     * convert().
     **/
    float*
    convert(dsm_time_t ttag, float* values, int nvalues=0, float* results=0);

    /**
     * Add a parameter to this Variable. Variable
     * will then own the pointer and will delete it
     * in its destructor.
     */
    void addParameter(Parameter* val);

    /**
     * Get full list of parameters.
     */
    std::list<const Parameter*> getParameters() const;

    /**
     * Fetch a parameter by name. Returns a NULL pointer if
     * no such parameter exists.
     */
    const Parameter* getParameter(const std::string& name) const;

    void setMissingValue(float val)
    {
        _missingValue = val;
    }

    float getMissingValue() const
    {
        return _missingValue;
    }

    /**
     * Set the minimum allowed value for this variable.
     * Variable values less than val will be set to NAN.
     * Support for this check must be supported by
     * each DSMSensor class.
     */
    void setMinValue(float val)
    {
        _minValue = val;
        if (std::isnan(_plotRange[0])) _plotRange[0] = val;
    }

    float getMinValue() const
    {
        return _minValue;
    }

    void setMaxValue(float val)
    {
        _maxValue = val;
        if (std::isnan(_plotRange[1])) _plotRange[1] = val;
    }

    float getMaxValue() const
    {
        return _maxValue;
    }

    void setPlotRange(float minv,float maxv)
    {
        _plotRange[0] = minv;
        _plotRange[1] = maxv;
    }

    void getPlotRange(float& minv,float& maxv) const
    {
        minv = _plotRange[0];
        maxv = _plotRange[1];
    }

    /**
     * A dynamic variable is one that can come and go. The
     * dynamic attribute is typically used by a display
     * application which will scan all the variables
     * for a project and create a plot for each. If a variable is
     * dynamic, then that application should wait to create a plot
     * for the variable until data is received for it.
     */
    void setDynamic(bool val) { _dynamic = val; }

    bool isDynamic() const { return _dynamic; }

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
     * @throws xercesc::DOMException
     **/
    xercesc::DOMElement*
    toDOMElement(xercesc::DOMElement* node, bool complete) const;

private:

    void setSiteSuffix(const std::string& val);

    /**
     * Any time _prefix, _suffix, or _siteSuffix are updated, this method
     * must be called to update _name.
     */
    void updateName();

    /**
     * Expand the string in @p aval against either the containing SampleTag or
     * else the global Project instance.  Return a reference to the same
     * string.
     */
    std::string& expand(std::string& aval);

    std::string _name;

    const Site* _site;

    int _station;

    std::string _nameWithoutSite;

    std::string _prefix;

    std::string _suffix;

    std::string _siteSuffix;

    std::string _longname;

    const SampleTag* _sampleTag;

    int _A2dChannel;

    std::string _units;

    type_t _type;

    unsigned int _length;

    VariableConverter *_converter;

    /**
     * List of pointers to Parameters.
     */
    std::list<Parameter*> _parameters;

    float _missingValue;

    float _minValue;

    float _maxValue;

    float _plotRange[2];

    bool _dynamic;

};

}} // namespace nidas namespace core

#endif
