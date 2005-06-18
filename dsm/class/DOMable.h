
/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate$

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL$


*/

#ifndef DSM_DOMABLE_H
#define DSM_DOMABLE_H

#include <atdUtil/InvalidParameterException.h>

#include <DOMObjectFactory.h>
#include <XDOM.h>
#include <XMLStringConverter.h>

#include <xercesc/util/XMLUniDefs.hpp>
#include <xercesc/util/XMLString.hpp>
#include <xercesc/dom/DOMDocument.hpp>
#include <xercesc/dom/DOMNode.hpp>
#include <xercesc/dom/DOMElement.hpp>
#include <xercesc/dom/DOMException.hpp>
#include <xercesc/dom/DOMNamedNodeMap.hpp>

namespace dsm {

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
    virtual ~DOMable() = 0;

    /**
     * Initialize myself from a xercesc::DOMElement.
     */
    virtual void fromDOMElement(const xercesc::DOMElement*)
    	throw(atdUtil::InvalidParameterException) = 0;

    /**
     * Create a DOMElement and append it to the parent.
     */
    virtual xercesc::DOMElement*
    	toDOMParent(xercesc::DOMElement* parent)
		throw(xercesc::DOMException) = 0;

    /**
     * Add my content into a DOMElement.
     */
    virtual xercesc::DOMElement*
    	toDOMElement(xercesc::DOMElement* node)
		throw(xercesc::DOMException) = 0;

    static const XMLCh* getNamespaceURI() {
	if (!namespaceURI) namespaceURI =
		xercesc::XMLString::transcode(
		        "http://www.eol.ucar.edu/daq");
        return namespaceURI;
    }

private:
    static XMLCh* namespaceURI;

};

}

#define CREATOR_ENTRY_POINT(className) \
extern "C" {\
    dsm::DOMable* create##className()\
    {\
	return new className();\
    }\
}


#endif
