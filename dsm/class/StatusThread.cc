/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate: 2004-10-15 17:53:32 -0600 (Fri, 15 Oct 2004) $

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL: http://orion/svn/hiaper/ads3/dsm/class/RTL_DSMSensor.h $
 ********************************************************************

*/

#include <StatusThread.h>
#include <DSMEngine.h>
// #include <DSMConfig.h>
// #include <DSMSensor.h>
#include <Datagrams.h>

#include <atdUtil/Socket.h>

using namespace dsm;
using namespace std;

StatusThread::StatusThread(const std::string& name,int runPeriod):
	Thread(name),period(runPeriod)
{
    blockSignal(SIGINT);
    blockSignal(SIGHUP);
    blockSignal(SIGTERM);
}

int StatusThread::run() throw(atdUtil::Exception)
{
    DSMEngine* engine = DSMEngine::getInstance();
    const DSMConfig* dsm = engine->getDSMConfig();
    // const PortSelector* selector = engine->getPortSelector();

    struct timespec nsleep;
    nsleep.tv_sec = period;
    nsleep.tv_nsec = 0;

    atdUtil::MulticastSocket msock;
    atdUtil::Inet4Address maddr =
    	atdUtil::Inet4Address::getByName(DSM_MULTICAST_ADDR);
    atdUtil::Inet4SocketAddress msaddr =
	atdUtil::Inet4SocketAddress(maddr,DSM_MULTICAST_STATUS_PORT);

    std::ostringstream statStream;

    for (;;) {
	if (nanosleep(&nsleep,0) < 0 && errno == EINTR) break;
        if (isInterrupted()) break;

	const std::list<DSMSensor*>& sensors = dsm->getSensors();
	std::list<DSMSensor*>::const_iterator si;
	for (si = sensors.begin(); si != sensors.end(); ++si) {
	    DSMSensor* sensor = *si;
	    sensor->printStatus(statStream);
	}
	string statstr = statStream.str();
	statStream.str("");
        msock.sendto(statstr.c_str(),statstr.length()+1,0,msaddr);
    }
    return 0;
}
