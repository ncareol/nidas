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
#include "DynamicLoader.h"

#include <xercesc/util/XMLUniDefs.hpp>
#include <xercesc/util/XMLString.hpp>
#include <xercesc/dom/DOMDocument.hpp>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMElement.hpp>
#include <xercesc/dom/DOMException.hpp>
#include <xercesc/dom/DOMNamedNodeMap.hpp>

namespace nidas { namespace core {

/**
 * Interface of an object that can be instantiated from a DOM element,
 * via the fromDOMElement method, or that can be serialized into a DOM,
 * via the toDOMParent/toDOMElement method.
 */
class DOMable {
public:

    /**
     * Virtual destructor.
     */
    virtual ~DOMable() {}

    /**
     * Initialize myself from a xercesc::DOMElement.
     */
    virtual void fromDOMElement(const xercesc::DOMElement*)
    	throw(nidas::util::InvalidParameterException) = 0;

    /**
     * Create a DOMElement and append it to the parent.
     */
    virtual xercesc::DOMElement*
    	toDOMParent(xercesc::DOMElement* parent,bool complete) const
		throw(xercesc::DOMException);

    /**
     * Add my content into a DOMElement.
     */
    virtual xercesc::DOMElement*
    	toDOMElement(xercesc::DOMElement* node,bool complete) const
		throw(xercesc::DOMException);

    static const XMLCh* getNamespaceURI() {
	if (!namespaceURI) namespaceURI =
		xercesc::XMLString::transcode(
		        "http://www.eol.ucar.edu/nidas");
        return namespaceURI;
    }

private:
    static XMLCh* namespaceURI;

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
    nidas::core::DOMable* create_nidas_dynld_ ##CLASSNAME()\
    {\
	return new nidas::dynld::CLASSNAME();\
    }\
    void __attribute__((constructor)) register_nidas_dynld_ ## CLASSNAME()\
    {\
        nidas::core::DynamicLoader::add("create_nidas_dynld_" #CLASSNAME, (void*)create_nidas_dynld_ ## CLASSNAME);\
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
    nidas::core::DOMable* create_nidas_dynld_ ## NS ## _ ## CLASSNAME()\
    {\
	return new nidas::dynld::NS::CLASSNAME();\
    }\
    void __attribute__((constructor)) register_nidas_dynld_ ## NS ## _ ## CLASSNAME()\
    {\
        nidas::core::DynamicLoader::add("create_nidas_dynld_" #CLASSNAME,(void*)create_nidas_dynld_ ## NS ## _ ## CLASSNAME);\
    }\
}

#endif
