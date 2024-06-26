/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
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

#include "Parameter.h"
#include "Sample.h"	// floatNAN
#include "Dictionary.h"

#include <sstream>
#include <iostream>

using namespace std;

namespace n_u = nidas::util;

namespace nidas {
namespace core {

double Parameter::getNumericValue(int i) const
{
    switch (_type) {
    case FLOAT_PARAM:
        return static_cast<const ParameterT<float>*>(this)->getValue(i);
        break;
    case INT_PARAM:
        return static_cast<const ParameterT<int>*>(this)->getValue(i);
        break;
    case STRING_PARAM:
        break;
    case BOOL_PARAM:
        return static_cast<const ParameterT<bool>*>(this)->getValue(i);
        break;
    }
    return floatNAN;
}

std::string Parameter::getStringValue(int i) const
{
    ostringstream ost;
    switch (_type) {
    case FLOAT_PARAM:
        ost << static_cast<const ParameterT<float>*>(this)->getValue(i);
        break;
    case INT_PARAM:
        ost << static_cast<const ParameterT<int>*>(this)->getValue(i);
        break;
    case STRING_PARAM:
        ost << static_cast<const ParameterT<string>*>(this)->getValue(i);
        break;
    case BOOL_PARAM:
        ost << static_cast<const ParameterT<bool>*>(this)->getValue(i);
        break;
    }
    return ost.str();
}

bool Parameter::getBoolValue(const std::string& name) const
{
    if ((getType() != Parameter::BOOL_PARAM &&
        getType() != Parameter::INT_PARAM &&
        getType() != Parameter::FLOAT_PARAM) ||
        getLength() != 1)
    {
        if (!name.empty())
            throw n_u::InvalidParameterException(name, getName(),
                "should be a boolean or integer (FALSE=0,TRUE=1) of length 1");
        return false;
    }
    return (bool) getNumericValue(0);
}


Parameter* Parameter::createParameter(const xercesc::DOMElement* node,
                                      const Dictionary* dict)
{
    XDOMElement xnode(node);
    Parameter* parameter = 0;
    if(node->hasAttributes()) {
    // get all the attributes of the node
        xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
        int nSize = pAttributes->getLength();
        for(int i=0;i<nSize;++i) {
            XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
            const string& aname = attr.getName();
            const string& aval = attr.getValue();
            if (aname == "type") {
                if (aval == "float")
                        parameter = new ParameterT<float>();
                else if (aval == "bool")
                        parameter = new ParameterT<bool>();
                else if (aval == "string")
                        parameter = new ParameterT<string>();
                else if (aval == "strings")
                        parameter = new ParameterT<string>();
                else if (aval == "int")
                        parameter = new ParameterT<int>();
                else if (aval == "hex")
                        parameter = new ParameterT<int>();
                else throw n_u::InvalidParameterException("parameter",
                        aname,aval);

                parameter->fromDOMElement(node,dict);

                return parameter;
            }
        }
    }
    throw n_u::InvalidParameterException("parameter",
            "element","no type attribute found");
}

template<class T>
ParameterT<T>* ParameterT<T>::clone() const
{
    return new ParameterT<T>(*this);
}

template<class T>
void ParameterT<T>::assign(const Parameter& x)
{
    if (_type == x.getType()) {
        _name = x.getName();
        const ParameterT<T> * xT =
            dynamic_cast<const ParameterT<T>*>(&x);
        if (xT) _values = xT->_values;
    }
}

template<class T>
void ParameterT<T>::fromDOMElement(const xercesc::DOMElement* node)
{
    fromDOMElement(node,0);
}

template<class T>
void ParameterT<T>::fromDOMElement(const xercesc::DOMElement* node,
                                   const Dictionary* dict)
{

    XDOMElement xnode(node);
    if(node->hasAttributes()) {
        // get all the attributes of the node

        const string& ptype = xnode.getAttributeValue("type");
        bool oneString = ptype == "string";

        xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
        int nSize = pAttributes->getLength();
        for(int i=0;i<nSize;++i) {
            XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
            const std::string& aname = attr.getName();
            std::string aval = attr.getValue();
            if (dict) aval = dict->expandString(aval);

            if (aname == "name") setName(aval);
            else if (aname == "value") {
                // get attribute value(s)

                // If type is simply "string", don't break it up.
                if (oneString) {
                    // ugly!
                    ParameterT<string>* strParam =
                            dynamic_cast<ParameterT<string>*>(this);
                    strParam->setValue(aval);
                }
                else {
                    std::istringstream ist(aval);
                    if (ptype == "hex") ist >> hex;
                    if (ptype == "bool") ist >> boolalpha;
                    T val;
                    for (int i = 0; ; i++) {
                        ist >> val;
#ifdef DEBUG
                        std::cerr << 
                            "Parameter::fromDOMElement, read, val=" << val <<
                            " eof=" << ist.eof() <<
                            " fail=" << ist.fail() << std::endl;
#endif
                        // In case a bool was entered as "0" or "1", turn off boolalpha and try again.
                        if (ist.fail()) {
                            if (ist.eof()) break;
                            ist.clear();
                            ist >> noboolalpha >> val;
                            if (ist.fail())
                                throw n_u::InvalidParameterException(
                                    "parameter",getName(),aval);
                            ist >> boolalpha;
                        }
                        setValue(i,val);
                    }
                }
#ifdef DEBUG
                std::cerr << "Parameter::fromDOMElement, getLength()=" <<
                        getLength() << std::endl;
#endif
            }
            else if (aname != "type" && aname != "xmlns")
                // XMLConfigWriter seems to add xmlns attributes
                throw n_u::InvalidParameterException(
                    "parameter",aname,aval);
        }
    }
}


template <class T>
ParameterT<T>::ParameterT():
    Parameter(getParamType(T())),
    _values()
{
}

template <class T>
int ParameterT<T>::
getLength() const {
    return _values.size();
}

template <class T>
const std::vector<T> ParameterT<T>::getValues() const
{
    return _values;
}

template <class T>
void ParameterT<T>::setValues(const std::vector<T>& vals) {
    _values = vals;
}

template <class T>
void ParameterT<T>::setValue(unsigned int i, const T& val)
{
    if (i+1 > _values.size())
        _values.resize(i+1);
    _values[i] = val;
}

template <class T>
void ParameterT<T>::setValue(const T& val) {
    _values.resize(1);
    _values[0] = val;
}

template <class T>
T ParameterT<T>::getValue(int i) const
{
    return _values[i];
}


template class ParameterT<float>;
template class ParameterT<int>;
template class ParameterT<string>;
template class ParameterT<bool>;

} // namespace core
} // namespace nidas
