// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
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

#ifndef NIDAS_CORE_XMLPARSER_H
#define NIDAS_CORE_XMLPARSER_H

#include "XMLException.h"

#include <nidas/util/ThreadSupport.h>
#include <nidas/util/IOException.h>

#include <xercesc/dom/DOMImplementation.hpp>
#include <xercesc/dom/DOMErrorHandler.hpp>
#include <xercesc/dom/DOMDocument.hpp>
#include <xercesc/sax/InputSource.hpp>

#include <xercesc/dom/DOMLSParser.hpp>

#include <string>
#include <map>
#include <list>

namespace nidas { namespace core {

class XMLImplementation {
public:
    /**
     * @throws nidas::core::XMLException
     **/
    static xercesc::DOMImplementation *getImplementation();
    static void terminate();

private:
    static xercesc::DOMImplementation *_impl;
    static nidas::util::Mutex _lock;
};
    
class XMLErrorHandler : public xercesc::DOMErrorHandler
{
public:
    // -----------------------
    //  Constructors and Destructor
    // ----------------------
    XMLErrorHandler();
    ~XMLErrorHandler();

    // --------------------------
    //  Implementation of the DOM ErrorHandler interface
    // -------------------------
    bool handleError(const xercesc::DOMError& domError);

    void resetErrors();

    int getWarningCount() const { return _warningMessages.size(); }

    const std::list<std::string>& getWarningMessages() const
    	{ return _warningMessages; }

    const XMLException* getXMLException() const { return _xmlException; }

private :

    // -----------------------------
    //  Unimplemented constructors and operators
    // ----------------------------
    XMLErrorHandler(const XMLErrorHandler&);

    void operator=(const XMLErrorHandler&);

    /**
     * Accumulated warning messages.
     */
    std::list<std::string> _warningMessages;

    /**
     * Accumulated error messages.
     */
    XMLException* _xmlException;

};

/**
 * Utility function which creates a temporary XMLParser, sets the options we
 * typically want and parses the XML into a DOMDocument.
 *
 * @throws nidas::core::XMLException
 */
xercesc::DOMDocument* parseXMLConfigFile(const std::string& xmlFileName);

/**
 * Wrapper class around xerces-c DOMBuilder to parse XML.
 */
class XMLParser {
public:

    /**
     * Constructor. The default setting for
     * setXercesUserAdoptsDOMDocument(true) is true.
     *
     * @throws nidas::core::XMLException
     */
    XMLParser();

    /**
     * Nuke the parser. This does a release() (delete) of the
     * associated DOMBuilder.
     */
    virtual ~XMLParser();

    /**
     * DOMBuilder::setFilter is not yet implemented in xerces c++ 2.6.0 
    void setFilter(xercesc::DOMBuilderFilter* filter)
     */

    /**
     * Enable/disable validation.
     * See /link
     *	http://xml.apache.org/xerces-c/program-dom.html#DOMBuilderFeatures
     * @param val Boolean value specifying whether to report all
     *   validation errors.
     *   Default: false.
     */
    void setDOMValidation(bool val);

    /**
     * Enable/disable schema validation.
     * See /link
     *	http://xml.apache.org/xerces-c/program-dom.html#DOMBuilderFeatures
     * @param val If true the parser will validate the
     *	 document only if a grammar is specified.
     *   If false validation is determined by the state of the
     *   validation feature, see setDOMValidation().  
     *   Default: false.
     */
    void setDOMValidateIfSchema(bool val);

    /**
     * Enable/disable namespace processing.
     * See /link
     *	http://xml.apache.org/xerces-c/program-dom.html#DOMBuilderFeatures
     * @param val If true perform namespace processing.
     *   Default: false.
     */
    void setDOMNamespaces(bool val);

    /**
     * Enable/disable schema support.
     * See /link
     *	http://xml.apache.org/xerces-c/program-dom.html#DOMBuilderFeatures
     * @param val If true enable the parser's schema support. 
     *   Default: false.
     */
    void setXercesSchema(bool val);

    /**
     * Enable/disable full schema constraint checking,
     * including checking which may be time-consuming or
     * memory intensive. Currently, particle unique
     * attribution constraint checking and particle derivation
     * restriction checking are controlled by this option.  
     * See /link
     *	http://xml.apache.org/xerces-c/program-dom.html#DOMBuilderFeatures
     * @param val If true enable schema constraint checking.
     *   Default: false.
     */
    void setXercesSchemaFullChecking(bool val);

    /**
     * Enable/disable datatype normalization.
     * See /link
     *	http://xml.apache.org/xerces-c/program-dom.html#DOMBuilderFeatures
     * @param val If true let the validation process do its datatype
     *    normalization that is defined in the used schema language.
     *    If false disable datatype normalization. The XML 1.0 attribute
     *    value normalization always occurs though.  
     *   Default: false.
     */
    void setDOMDatatypeNormalization(bool val);

    /**
     * Control who owns DOMDocument pointer.
     * See /link
     *	http://xml.apache.org/xerces-c/program-dom.html#DOMBuilderFeatures
     * @param val If true the caller will adopt the DOMDocument that
     *     is returned from the parse method and thus is responsible
     *     to call DOMDocument::release() to release the associated memory.
     *     The parser will not release it. The ownership is transferred
     *     from the parser to the caller.
     *     If false the returned DOMDocument from the parse method is
     *     owned by the parser and thus will be deleted when the parser
     *     is released.
     */
    void setXercesUserAdoptsDOMDocument(bool val);

    void setXercesHandleMultipleImports(bool val);

    void setXercesDoXInclude(bool val);

    /**
     * @throws nidas::core::XMLException
     **/
    xercesc::DOMDocument* parse(const std::string& xmlFile, bool verbose=true);

    /**
     * @throws nidas::core::XMLException
     **/
    xercesc::DOMDocument* parse(xercesc::InputSource& source);

    /**
     * @brief Parse XML string into a document.
     *
     * The returned DOMDocument pointer needs to be deleted, and before the
     * XMLImplementation is terminated.
     *
     * @param xml 
     * @return xercesc::DOMDocument* 
     **/
    xercesc::DOMDocument* parseString(const std::string& xml);

    /**
     * @brief Call parseString() on a default XMLParser instance.
     * 
     * @param xml 
     * @return xercesc::DOMDocument* 
     **/
    static
    xercesc::DOMDocument*
    ParseString(const std::string& xml);

protected:
    
    xercesc::DOMImplementation *_impl;

    xercesc::DOMLSParser *_parser;
    XMLErrorHandler _errorHandler;

private:

    /** No copying. */
    XMLParser(const XMLParser&);

    /** No assignment. */
    XMLParser& operator=(const XMLParser&);
};

/**
 * Derived class of XMLParser that keeps its DOMDocuments
 * when parsing an XML disk file, and returns the cached
 * DOMDocument if the file hasn't changed.
 */
class XMLCachingParser : public XMLParser {
public:

    /**
     * @throws nidas::core::XMLException
     **/
    static XMLCachingParser* getInstance();

    static void destroyInstance();

    /**
     * Parse from a file. This will return the DOMDocument
     * pointer of the a previous parse result if the file has
     * not been modified since the last time it was
     * parsed. The pointer to the DOMDocuemnt is owned by
     * XMLCachingParser and the user should not call
     * doc->release();
     *
     * @throws nidas::core::XMLException
     * @throws nidas::util::IOException
     */
    xercesc::DOMDocument* parse(const std::string& xmlFile);

    /**
     * Parse from an InputSource. This is not cached.
     */
    /*
    xercesc::DOMDocument* parse(xercesc::InputSource& source)
    	throw(nidas::core::XMLException)
    {
        return XMLParser::parse(source);
    }
    */

    /**
     * @throws nidas::util::IOException
     **/
    static time_t getFileModTime(const std::string& name);

protected:
    /**
     * @throws nidas::core::XMLException
     **/
    XMLCachingParser();
    ~XMLCachingParser();

protected:
    static XMLCachingParser* _instance;
    static nidas::util::Mutex _instanceLock;

    std::map<std::string,time_t> _modTimeCache;
    std::map<std::string,xercesc::DOMDocument*> _docCache;

    nidas::util::Mutex _cacheLock;

private:
    /** No copying. */
    XMLCachingParser(const XMLCachingParser&);

    /** No assignment. */
    XMLCachingParser& operator=(const XMLCachingParser&);
};

}}	// namespace nidas namespace core

#endif

