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


#include "DOMable.h"

#include <xercesc/util/XMLString.hpp>
#include <nidas/util/Logger.h>

#include <sstream>
#include <algorithm>

using nidas::util::InvalidParameterException;
using std::string;
using std::ios;

namespace {
    XMLCh* namespaceURI = 0;

    std::string DEFAULT_CONTEXT{ "DOMable" };
}


namespace nidas {
namespace core
{

const XMLCh* DOMable::getNamespaceURI()
{
    if (!namespaceURI) namespaceURI =
            xercesc::XMLString::transcode(
                    "http://www.eol.ucar.edu/nidas");
    return namespaceURI;
}


/**
 * Create a DOMElement and append it to the parent.
 */
xercesc::DOMElement*
DOMable::toDOMParent(xercesc::DOMElement* /* parent */,bool /* brief */) const
{
    throw xercesc::DOMException(xercesc::DOMException::NOT_SUPPORTED_ERR);
/*
    DOMElement* elem =
        parent->getOwnerDocument()->createElementNS(
                (const XMLCh*)XMLStringConverter("name"),
			DOMable::getNamespaceURI());
    parent->appendChild(elem);
    return toDOMElement(elem);
*/
}

/**
 * Add my content into a DOMElement.
 */
xercesc::DOMElement*
DOMable::toDOMElement(xercesc::DOMElement*, bool) const
{
    throw xercesc::DOMException(xercesc::DOMException::NOT_SUPPORTED_ERR);
}


void DOMable::logNode(const xercesc::DOMElement* node)
{
    static nidas::util::LogContext lp(LOG_VERBOSE);
    if (node && lp.active())
    {
        XDOMElement xnode(node);
        if (!node->hasAttributes())
            return;
        std::ostringstream out;
        out << "DOMable element node " << xnode.getNodeName() << ": ";
        xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
        int nSize = pAttributes->getLength();
        for (int i = 0; i < nSize; ++i) {
            XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
            const string& aname = attr.getName();
            out << aname << "=" << attr.getValue() << "; ";
        }
        lp.log() << out.str();
    }
}


void DOMable::pushContext(const std::string& context,
                          const xercesc::DOMElement* node)
{
    _contexts.push_back(context);
    VLOG(("entering context: ") << context);
    logNode(node);
}


void DOMable::popContext(const xercesc::DOMElement* node)
{
    VLOG(("leaving context: ") << _contexts.back());
    if (_contexts.size() == 1 && node)
        checkUnhandledAttributes(node);
    _contexts.pop_back();
}


void DOMable::addContext(const std::string& context)
{
    if (! _contexts.empty())
    {
        _contexts.back() += context;
        VLOG(("") << "DOMable context updated: " << _contexts.back());
    }
}

std::string& DOMable::context()
{
    if (!_contexts.empty())
        return _contexts.back();
    return DEFAULT_CONTEXT;
}

void
DOMable::handledAttributes(const std::vector<std::string>& names)
{
    for (auto& name: names)
        _handled_attributes.push_back(name);
}


bool DOMable::getAttribute(const xercesc::DOMElement* node,
                           const std::string& name, std::string& value)
{
    handledAttributes({name});
    std::string aval;
    bool found{false};
    if (!node)
        throw std::logic_error("getAttribute(): node not set.");
    if (!node->hasAttributes())
        return false;
    xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
    int nSize = pAttributes->getLength();
    // just in case an attribute appears more than once, the last value is
    // returned.
    for (int i = 0; i < nSize; ++i) {
        XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
        const string& aname = attr.getName();
        if (aname == name) {
            aval = attr.getValue();
            found = true;
        }
    }
    if (found)
        value = aval;
    return found;
}


const std::string&
DOMable::
get_name(const std::string& iname)
{
    static const std::string unknown{ "unknown" };
    const std::string* pname{&iname};
    if (iname.empty() && !_handled_attributes.empty())
        pname = &_handled_attributes.back();
    else
        pname = &unknown;
    const std::string& name{ *pname };
    return name;
}


float DOMable::asFloat(const std::string& value, const std::string& iname)
{
    std::istringstream ist(value);
    float val;
    ist >> val;
    if (ist.fail())
        throw InvalidParameterException(context(), get_name(iname), value);
    return val;
}


bool DOMable::asBool(const std::string& value, const std::string& iname)
{
    bool bval{false};
    std::istringstream ist(value);

    ist >> std::boolalpha >> bval;
    if (ist.fail()) {
        ist.clear();
        ist >> std::noboolalpha >> bval;
        if (ist.fail())
            throw InvalidParameterException(context(), get_name(iname), value);
    }
    return bval;
}


int DOMable::asInt(const std::string& value, const std::string& iname)
{
    std::istringstream ist(value);
    int val;
    // If you unset the dec flag, then a leading '0' means
    // octal, and 0x means hex.
    ist.unsetf(ios::dec);
    ist >> val;
    if (ist.fail())
        throw InvalidParameterException(context(), get_name(iname), value);
    return val;
}


void DOMable::checkUnhandledAttributes(const xercesc::DOMElement* node)
{
    if (!node || !node->hasAttributes())
        return;
    xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
    int nSize = pAttributes->getLength();
    auto begin = _handled_attributes.begin();
    auto end = _handled_attributes.end();
    for (int i = 0; i < nSize; ++i) {
        XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
        const string& aname = attr.getName();
        if (std::find(begin, end, aname) == end)
        {
            throw InvalidParameterException(context() +
                                            ": unknown attribute " + aname);
        }
    }
}


DOMableContext::DOMableContext(DOMable* domable, const std::string& context,
                               const xercesc::DOMElement* node):
    _domable(domable),
    _node(node)
{
    _domable->pushContext(context, node);
}

DOMableContext::~DOMableContext() noexcept(false)
{
    _domable->popContext(_node);
}


} // namespace core
} // namespace nidas
