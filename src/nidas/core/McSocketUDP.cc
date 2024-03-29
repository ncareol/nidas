/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2009, Copyright University Corporation for Atmospheric Research
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

#include "McSocketUDP.h"
#include "DatagramSocket.h"
#include <nidas/util/Process.h>
#include <nidas/util/Logger.h>

using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

McSocketUDP::McSocketUDP(): IOChannel(),
    _iochanRequester(0),_mcsocket(this), _name(),
    _amRequester(true), _firstRead(true),_newInput(true),_nonBlocking(false)
{
    setName("McSocketUDP");
}

/*
 * Copy constructor. Should only be called before socket connection.
 */
McSocketUDP::McSocketUDP(const McSocketUDP& x):IOChannel(x),
    _iochanRequester(0),_mcsocket(this),
    _name(x._name), _amRequester(x._amRequester),
    _firstRead(true),_newInput(true),
    _nonBlocking(x._nonBlocking)
{
}

McSocketUDP* McSocketUDP::clone() const
{
    return new McSocketUDP(*this);
}

IOChannel* McSocketUDP::connect()
{
    n_u::DatagramSocket* sock;
    n_u::Inet4PacketInfoX pktinfo;

    if (isRequester()) sock = _mcsocket.connect(pktinfo);
    else sock = _mcsocket.accept(pktinfo);

    sock->setNonBlocking(isNonBlocking());
    nidas::core::DatagramSocket* ncsock = new nidas::core::DatagramSocket(sock);

    ConnectionInfo info(pktinfo.getRemoteSocketAddress(),
        pktinfo.getDestinationAddress(),pktinfo.getInterface());
    ncsock->setConnectionInfo(info);
    return ncsock;
}

void McSocketUDP::requestConnection(IOChannelRequester* requester)
{
    _iochanRequester = requester;
    if (isRequester()) _mcsocket.request();	// starts requester thread
    else _mcsocket.listen();			// starts listener thread
}

void McSocketUDP::connected(n_u::DatagramSocket* sock,const n_u::Inet4PacketInfoX& pktinfo)
{
    // cerr << "McSocketUDP::connected, sock=" << sock->getRemoteSocketAddress().toString() << endl;
    sock->setNonBlocking(isNonBlocking());
    nidas::core::DatagramSocket* dsock = new nidas::core::DatagramSocket(sock);

    ConnectionInfo info(pktinfo.getRemoteSocketAddress(),
        pktinfo.getDestinationAddress(),pktinfo.getInterface());
    dsock->setConnectionInfo(info);

    assert(_iochanRequester);
    _iochanRequester->connected(dsock);
}

void McSocketUDP::close()
{
    // cerr << "McSocket::close" << endl;
    _mcsocket.close();
}

int McSocketUDP::getFd() const
{
    return -1;
}

void McSocketUDP::fromDOMElement(const xercesc::DOMElement* node)
{
    string saddr;
    string sport;
    bool multicast = true;

    XDOMElement xnode(node);
    if(node->hasAttributes()) {
    // get all the attributes of the node
        xercesc::DOMNamedNodeMap *pAttributes = node->getAttributes();
        int nSize = pAttributes->getLength();
        for(int i=0;i<nSize;++i) {
            XDOMAttr attr((xercesc::DOMAttr*) pAttributes->item(i));
            // get attribute name
            const std::string& aname = attr.getName();
            const std::string& aval = attr.getValue();
	    if (aname == "address") saddr = n_u::Process::expandEnvVars(aval);
	    else if (aname == "port") sport = n_u::Process::expandEnvVars(aval);
	    else if (aname == "requestType") {
		int i;
	        istringstream ist(aval);
		ist >> i;
		if (ist.fail())
		    throw n_u::InvalidParameterException(
			    getName(),aname,aval);
		setRequestType((enum McSocketRequest)i);
	    }
	    else if (aname == "type") {
		if (aval == "mcacceptUDP") {
		    multicast = true;
		    setRequester(false);
		}
		else if (aval == "mcrequestUDP") {
		    multicast = true;
		    setRequester(true);
		}
		else if (aval == "dgacceptUDP") {
		    multicast = false;
		    setRequester(false);
		}
		else if (aval == "dgrequestUDP") {
		    multicast = false;
		    setRequester(true);
		}
		else throw n_u::InvalidParameterException(
			getName(),"type",aval);
	    }
	    else if (aname == "block") {
		std::istringstream ist(aval);
		ist >> boolalpha;
		bool val;
		ist >> val;
		if (ist.fail())
			throw n_u::InvalidParameterException(
			    "socket","block",aval);
		setNonBlocking(!val);
	    }
	    else throw n_u::InvalidParameterException(
	    	string("unrecognized socket attribute: ") + aname);
	}
    }
    // Default local listen address for accepters is NIDAS_MULTICAST_ADDR.
    // The socket is also bound to INADDR_ANY, so it receives unicast too.
    // Default address for multicast requesters is NIDAS_MULTICAST_ADDR.
    // Unicast requesters must know who to direct requests to.
    if (saddr.length() == 0) {
	if (multicast || !isRequester()) saddr = NIDAS_MULTICAST_ADDR;
	else throw n_u::InvalidParameterException(
	    	getName(),"address","unknown address for dgrequest socket");
    }

    n_u::Inet4Address iaddr;
    try {
	iaddr = n_u::Inet4Address::getByName(saddr);
    }
    catch(const n_u::UnknownHostException& e) {
        n_u::Logger::getInstance()->log(LOG_WARNING,"socket: unknown IP address: %s",
            saddr.c_str());
    }

    int port = 0;
    if (sport.length() > 0) port = atoi(sport.c_str());
    else port = NIDAS_SVC_REQUEST_PORT_UDP;

    setInet4McastSocketAddress(n_u::Inet4SocketAddress(iaddr,port));
}

