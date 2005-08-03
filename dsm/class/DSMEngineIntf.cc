/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate$

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL$
 ********************************************************************
*/

#include <DSMEngineIntf.h>

#include <DSMEngine.h>
#include <Datagrams.h> // defines DSM_XMLRPC_PORT

// #include <atdUtil/Logger.h>

#include <iostream>

using namespace dsm;
using namespace std;
using namespace XmlRpc;


void Start::execute(XmlRpcValue& params, XmlRpcValue& result)
{
  DSMEngine::getInstance()->mainStart();
  result = "DSM started";
  cerr << &result << endl;
}


void Stop::execute(XmlRpcValue& params, XmlRpcValue& result)
{
  DSMEngine::getInstance()->mainStop();
  result = "DSM stopped";
  cerr << &result << endl;
}


void Restart::execute(XmlRpcValue& params, XmlRpcValue& result)
{
  DSMEngine::getInstance()->mainRestart();
  result = "DSM restarted";
  cerr << &result << endl;
}


void Quit::execute(XmlRpcValue& params, XmlRpcValue& result)
{
  DSMEngine::getInstance()->mainQuit();
  result = "DSM quit";
  cerr << &result << endl;
}


int DSMEngineIntf::run() throw(atdUtil::Exception)
{
  // Create an XMLRPC server
  _xmlrpc_server = new XmlRpcServer;

  // These constructors register methods with the XMLRPC server
  Start   start   (_xmlrpc_server);
  Stop    stop    (_xmlrpc_server);
  Restart restart (_xmlrpc_server);
  Quit    quit    (_xmlrpc_server);

  // DEBUG - set verbosity of the xmlrpc server HIGH...
  XmlRpc::setVerbosity(5);

  // Create the server socket on the specified port
  _xmlrpc_server->bindAndListen(DSM_XMLRPC_PORT);

  // Enable introspection
  _xmlrpc_server->enableIntrospection(true);

  // Wait for requests indefinitely
  _xmlrpc_server->work(-1.0);

  return RUN_OK;
}
