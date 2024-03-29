// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2012, Copyright University Corporation for Atmospheric Research
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

#ifndef REQUESTXMLCONFIG_H
#define REQUESTXMLCONFIG_H

#include <xercesc/dom/DOM.hpp>
#include <nidas/util/Inet4SocketAddress.h>
#include <nidas/util/Exception.h>

#include <signal.h>

namespace n_u = nidas::util;

namespace nidas { namespace core {

/**
 * Request the XML configuration via a McSocket request to
 * a given multicast socket address.
 * @param all: If true, request the entire project XML, otherwise just
 *    the XML which corresponds to the address of the calling DSM.
 *
 * @throws nidas::util::Exception
 **/
extern xercesc::DOMDocument*
requestXMLConfig(bool all,
                 const n_u::Inet4SocketAddress& mcastAddr,
                 sigset_t* signalMask=(sigset_t*)0 );

}}  // namespace nidas namespace core

#endif // REQUESTXMLCONFIG_H
