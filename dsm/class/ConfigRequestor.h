/*
 ********************************************************************
    Copyright by the National Center for Atmospheric Research

    $LastChangedDate: 2004-10-15 17:53:32 -0600 (Fri, 15 Oct 2004) $

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL: http://orion/svn/hiaper/ads3/dsm/class/RTL_DSMSensor.h $
 ********************************************************************
*/

#ifndef DSM_CONFIGREQUESTOR_H
#define DSM_CONFIGREQUESTOR_H

#include <atdUtil/McastServiceRequestor.h>

#include <Datagrams.h>

namespace dsm {

/**
 * Thread whose run method patiently multicasts ConfigDatagrams
 * until it is canceled.
 */
class ConfigRequestor: public atdUtil::McastServiceRequestor
{
public:
    ConfigRequestor(int listenPort) throw(atdUtil::IOException,atdUtil::UnknownHostException);

};

}

#endif
