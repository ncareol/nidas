/*
 ********************************************************************
    Copyright by the National Center for Atmospheric Research

    $LastChangedDate: 2004-10-15 17:53:32 -0600 (Fri, 15 Oct 2004) $

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL: http://orion/svn/hiaper/ads3/dsm/class/RTL_DSMSensor.h $
 ********************************************************************

*/

#include <RawSampleOutput.h>
#include <RawSampleServiceRequestor.h>

#include <iostream>

using namespace dsm;
using namespace std;
using namespace xercesc;

CREATOR_ENTRY_POINT(RawSampleOutput)

RawSampleOutput::RawSampleOutput()
{
}

RawSampleOutput::~RawSampleOutput()
{
}

void RawSampleOutput::connect() throw(atdUtil::IOException)
{
    atdUtil::ServerSocket servsock;
                                                                                
    RawSampleServiceRequestor requestor(servsock.getLocalPort());
    requestor.setSocketAddress(getSocketAddress());
    requestor.start();
                                                                                
    cerr << "accepting on " <<
        servsock.getInet4SocketAddress().toString() << endl;
    atdUtil::Socket sock = servsock.accept();      // throws IOException
    cerr << "accepted connection" << endl;
                                                                                
    servsock.close();
                                                                                
    cerr << "canceling requestor" << endl;
    requestor.cancel();

    setSocket(sock);
}

