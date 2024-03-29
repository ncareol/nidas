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

#ifndef NIDAS_CORE_XMLFDBININPUTSTREAM_H
#define NIDAS_CORE_XMLFDBININPUTSTREAM_H
                                                                                
#include <xercesc/util/XercesDefs.hpp>
#include <xercesc/util/BinInputStream.hpp>
#include <nidas/util/IOException.h>

#include <unistd.h>

#include <iostream>

namespace nidas { namespace core {

/**
 * Implemenation of xercesc::BinInputStream, which reads from a
 * unix file descriptor.
 */
class XMLFdBinInputStream: public xercesc::BinInputStream {
public:

    /**
     * Constructor.
     * @param n name of device - only used when reporting errors.
     * @param f unix file descriptor of device that is already open.
     */
    XMLFdBinInputStream(const std::string& n,int f) : name(n),fd(f),curpos(0),_eof(false) {}
    ~XMLFdBinInputStream()
    {
	// std::cerr << "~XMLFdBinInputStream" << std::endl;
    }

    XMLFilePos
    curPos() const { return curpos; }

    /**
     * return number of bytes read, or 0 on EOF.
     *
     * @throws nidas::util::IOException
     */
    XMLSize_t
    readBytes(XMLByte* const toFill,
    	const XMLSize_t maxToRead
    )
    {
        if (_eof) return 0;
	// std::cerr << "XMLFdBinInputStream reading " << maxToRead << std::endl;
	ssize_t l = ::read(fd,toFill,maxToRead);
	if (l < 0) throw nidas::util::IOException(name,"read",errno);
        for (int i = 0; i < l; i++)
            if (toFill[i] == '\x04') {
                l = i;
                _eof = true;
            }
	curpos += l;
	// std::cerr << "XMLFdBinInputStream read " << std::string((char*)toFill,0,l < 20 ? l : 20) << std::endl;
	// std::cerr << "XMLFdBinInputStream read " << std::string((char*)toFill,0,l) << std::endl;
	// std::cerr << "XMLFdBinInputStream read " << l << std::endl;
	// toFill[l] = 0;
	// toFill[l+1] = 0;
	return l;
    }

    const XMLCh* getContentType() const
    {
	return 0;
    }

protected:
    
    std::string name;
    
    int fd;
    XMLFilePos curpos;

    bool _eof;

};

}}	// namespace nidas namespace core

#endif

