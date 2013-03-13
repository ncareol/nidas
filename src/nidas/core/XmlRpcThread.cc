// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 Copyright 2005 UCAR, NCAR, All Rights Reserved

 $LastChangedDate$

 $LastChangedRevision$

 $LastChangedBy$

 $HeadURL$
 ********************************************************************
 */

#include <nidas/core/XmlRpcThread.h>
#include <nidas/core/DSMEngine.h>
#include <nidas/core/Datagrams.h>

#include <iostream>

using namespace nidas::core;
using namespace std;
using namespace XmlRpc;

XmlRpcThread::XmlRpcThread(const std::string& name):
    Thread(name), _xmlrpc_server(0)
{
    blockSignal(SIGINT);
    blockSignal(SIGHUP);
    blockSignal(SIGTERM);

    // unblock SIGUSR1 to register a signal handler, then block it
    // so that the pselect within XmlRpcDispatch will catch it.
    unblockSignal(SIGUSR1);
    blockSignal(SIGUSR1);
}

void XmlRpcThread::interrupt()
{
    // XmlRpcServer::exit() will not cause an exit of
    // XmlRpcServer::work(-1.0) if there are no rpc
    // requests coming in, but we'll do it anyway.
    if (_xmlrpc_server) _xmlrpc_server->exit();
    try {
        kill(SIGUSR1);
    }
    catch(const nidas::util::Exception& e) {
    }
}

XmlRpcThread::~XmlRpcThread()
{
    // user must have done a join of this thread before calling
    // this destructor.
    if (_xmlrpc_server) _xmlrpc_server->shutdown();
    delete _xmlrpc_server;
}
