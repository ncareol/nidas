/*
 ********************************************************************
    Copyright by the National Center for Atmospheric Research

    $LastChangedDate: 2004-10-15 17:53:32 -0600 (Fri, 15 Oct 2004) $

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL: http://orion/svn/hiaper/ads3/dsm/class/RTL_DSMSensor.h $
 ********************************************************************
*/

#include <ConfigRequestor.h>

#include <iostream>

using namespace dsm;
using namespace std;

ConfigRequestor::ConfigRequestor(int listenPort) throw(atdUtil::IOException,atdUtil::UnknownHostException) :
    atdUtil::McastServiceRequestor(listenPort,XML_CONFIG)
{
    atdUtil::Inet4SocketAddress to(
	    atdUtil::Inet4Address::getByName(DSM_MULTICAST_ADDR),
	    DSM_MULTICAST_PORT);
    setSocketAddress(to);

}
 
