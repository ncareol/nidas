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

#ifndef NIDAS_CORE_DOMABLE_H
#define NIDAS_CORE_DOMABLE_H

#include <nidas/util/InvalidParameterException.h>

#include "DOMObjectFactory.h"
#include "XDOM.h"
#include "XMLStringConverter.h"

#include <xercesc/util/XMLUniDefs.hpp>
#include <xercesc/util/XMLString.hpp>
#include <xercesc/dom/DOMDocument.hpp>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMElement.hpp>
#include <xercesc/dom/DOMException.hpp>
#include <xercesc/dom/DOMNamedNodeMap.hpp>

#include <vector>
#include <string>
#include <typeinfo>

namespace nidas { namespace core {

class DOMable;

/**
 * DOMableContext is a scoped object associated with a DOMable instance.  Upon
 * construction, tt sets the context in the DOMable and increments the DOMable
 * context counter.  Then on destruction, it decrements the counter.  When the
 * counter reaches zero, then the DOMable instance is called to check for
 * unhandled attributes.
 * 
 * DOMable subclasses can use this object to trigger the unhanlded attribute
 * check after all calls of fromDOMElement() up the class chain are finished.
 * Without it, an implement of fromDOMElement() does not know if it is the
 * most derived class instance or not.
 * 
 * This is a template so the type of the this parameter can be captured, since
 * the type will be exactly the class whose fromDOMElement() implemnetation is
 * being called, rather than the most derived type returned by rtti.
 */
class DOMableContext
{
public:
    DOMableContext(DOMable* domable, const std::string& context,
                   const xercesc::DOMElement* node=0);

    /**
     * The destructor calls checkUnhandledExceptions() if this is the last
     * context on the stack, so it has to be allowed to propagate exceptions
     * instead of calling terminate().  There are no resources allocated by
     * this class, so it is safe to throw through the destructor.
     */
    ~DOMableContext() noexcept(false);

    DOMableContext(const DOMableContext&) = delete;
    DOMableContext& operator=(const DOMableContext&) = delete;

private:
    DOMable* _domable{nullptr};
    const xercesc::DOMElement* _node{nullptr};
};


/**
 * Interface of an object that can be instantiated from a DOM element,
 * via the fromDOMElement method, or that can be serialized into a DOM,
 * via the toDOMParent/toDOMElement method.
 */
class DOMable {
public:
    using handled_attributes_t = std::vector<std::string>;

    /**
     * Virtual destructor.
     */
    virtual ~DOMable() {}

    /**
     * Initialize myself from a xercesc::DOMElement.
     *
     * @throws nidas::util::InvalidParameterException
     */
    virtual void fromDOMElement(const xercesc::DOMElement*) = 0;

    /**
     * Create a DOMElement and append it to the parent.
     *
     * @throws xercesc::DOMException
     */
    virtual xercesc::DOMElement*
    toDOMParent(xercesc::DOMElement* parent, bool complete) const;

    /**
     * Add my content into a DOMElement.
     *
     * @throws xercesc::DOMException
     */
    virtual xercesc::DOMElement*
    toDOMElement(xercesc::DOMElement* node, bool complete) const;

    static const XMLCh* getNamespaceURI();

protected:

    /**
     * Log information about this node.
     */
    std::string toString(const xercesc::DOMElement* node);

    /**
     * Push a context onto the stack, logging the new context and information
     * about the current node.
     */
    void pushContext(const std::string& context,
                     const xercesc::DOMElement* node=0);

    /**
     * Pop a context off the stack.  If this is the last context, then call
     * checkUnhandledAttributes().  Subclasses should not need to call this or
     * pushContext(), instead using a scoped DOMableContext instance to take
     * care of it.
     */
    void popContext(const xercesc::DOMElement* node);

    /**
     * Append @p context to the current instantiation context for this
     * DOMable, such as a particular node identifier.  The context is added to
     * messages and exceptions.  This does nothing if there is not already a
     * context on the stack.
     */
    void addContext(const std::string& context);

    /**
     * Register a list of attribute names that are handled by this DOMable.
     */
    void handledAttributes(const handled_attributes_t& names);

    /**
     * Add to the list of elements handled for this node.
     */
    void handledElements(const handled_attributes_t& names);

    /**
     * Lookup an attribute with name @p name and return the value as a string
     * in @p value.  If not found, @p value is not changed.  Return true if
     * the attribute is found.  When an attribute is requested, it is
     * automatically added to the handled list.
     */
    bool getAttribute(const xercesc::DOMElement* node,
                      const std::string& name, std::string& value);

    /**
     * Return attribute string value @p value as a float.  Throw
     * InvalidParameterException if the attribute value cannot be parsed as a
     * float.  The exception string will include the attribute name, using
     * @p name if given, otherwise the last attribute name retrieved with
     * getAttribute().
     */
    float asFloat(const std::string& value, const std::string& name = "");

    /**
     * Return attribute string value @p value as a bool.  Throw
     * InvalidParameterException if the attribute value cannot be parsed as a
     * bool.  The exception string will include the attribute name, using
     * @p name if given, otherwise the last attribute name retrieved with
     * getAttribute().
     */
    bool asBool(const std::string& value, const std::string& name = "");

    /**
     * Return attribute string value @p value as an int.  Throw
     * InvalidParameterException if the attribute value cannot be parsed as a
     * int.  The format can be decimal, or else hex with 0x prefix or octal
     * with 0 prefix.  The exception string will include the attribute name, using
     * @p name if given, otherwise the last attribute name retrieved with
     * getAttribute().
     */
    int asInt(const std::string& value, const std::string& name = "");

    handled_attributes_t getHandledAttributes()
    {
        return _handled_attributes;
    }

    /**
     * Check all the handled attributes against the attributes in the given
     * @p node, and raise an InvalidParameterException if there is an
     * attribute which was not handled.
     */
    void checkUnhandledAttributes(const xercesc::DOMElement* node);

    std::string& context();

private:

    const std::string& get_name(const std::string& iname);

    friend class DOMableContext;

    // Subclasses of DOMable can register the attributes they handle, so that
    // base class implementations can detect unhandled attributes.
    handled_attributes_t _handled_attributes{};
    handled_attributes_t _handled_elements{};

    std::vector<std::string> _contexts{};
};


}}	// namespace nidas namespace core

/**
 * Define a "creator" function, an extern "C" function that
 * invokes a public no-arg constructor for a class.
 *
 * @param CLASSNAME Name of class, without quotes.
 *
 * The idea here is to define a function that we can
 * lookup by name with DynamicLoader::lookup(string).
 * Since we don't want to know how C++ mangles namespace names,
 * this C function must not be in a namespace.  The class
 * is in a namespace though, so we do our own crude name
 * mangling in order to keep creator functions for separate
 * namespaces distinct, by substituting underscores for "::".
 *
 * Example:
 * nidas::dynld::MyClass is derived from nidas::core::DOMable
 * so that a pointer to DOMable can be returned by
 * DOMObjectFactory::createObject("MyClass"):
 *
 * namespace nidas { namespace dynld {
 *     class MyClass : public nidas::core::DOMable {
 *     public:
 *         MyClass() { ... }		// no-arg ctor
 *     };
 * }}
 *
 * Define an extern "C" creator function:
 *
 * NIDAS_CREATOR_FUNCTION(MyClass)
 *
 * This defines a C function called create_nidas_dynld_MyClass()
 * which invokes the nidas::dynld::MyClass() no arg constructor.
 *
 * To create a pointer to a new instance of
 * nidas::dynld::MyClass from a name in a string, do:
 *
 * DOMable* newobj = DOMObjectFactory::createObject("MyClass");
 *
 * Then one can cast it to a pointer to MyClass:
 *
 * nidas::dynld::MyClass* classobj =
 *   	dynamic_cast<nidas::dynld::MyClass*>(newobj);
 *
 * DOMObjectFactory::createObject does the name mangling by
 * prepending "create_nidas_dynld_" to the string argument,
 * converting all "::" to '_' in the class name string
 * and then does a lookup:
 * 
 * nidas::core::DOMable* (*ctor)() = 
 *   DynamicLoader::lookup("create_nidas_dynld_MyClass");
 *
 * Then it executes the creator function to get a pointer
 * to the object: 
 *
 * nidas::core::DOMable* obj = ctor();
 *
 */

#define NIDAS_CREATOR_FUNCTION(CLASSNAME) \
extern "C" {\
    nidas::core::DOMable* create_nidas_dynld_##CLASSNAME()\
    {\
	return new nidas::dynld::CLASSNAME();\
    }\
}

/**
 * Same as NIDAS_CREATOR_FUNCTION(CLASSNAME), but with a namespace
 * argument, indicating a namespace under nidas::dynld.
 * @param NS namespace of class.
 * @param CLASSNAME Name of class.
 */
#define NIDAS_CREATOR_FUNCTION_NS(NS,CLASSNAME) \
extern "C" {\
    nidas::core::DOMable* create_nidas_dynld_##NS##_##CLASSNAME()\
    {\
	return new nidas::dynld::NS::CLASSNAME();\
    }\
}

#endif
