/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate$

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL$
 ********************************************************************
*/
#include <xercesc/util/OutOfMemoryException.hpp>
#include <xercesc/framework/MemBufInputSource.hpp>

#include <nidas/util/Socket.h>

#include <nidas/core/Datagrams.h>
#include <nidas/core/XMLStringConverter.h>
#include <nidas/core/StatusListener.h>
#include <nidas/core/StatusHandler.h>

#include <iostream>

using namespace std;
using namespace nidas::core;

namespace n_u = nidas::util;

StatusListener::StatusListener():Thread("StatusListener")
{
  // initialize the XML4C2 system for the SAX2 parser
  try
  {
    xercesc::XMLPlatformUtils::Initialize();
  }
  catch (const xercesc::XMLException& toCatch)
  {
    cerr << "Error during initialization! :"
         << XMLStringConverter(toCatch.getMessage()) << endl;
    return;
  }
  // create a SAX2 parser object
  _parser = xercesc::XMLReaderFactory::createXMLReader();
  _handler = new StatusHandler(this);
  _parser->setContentHandler(_handler);
  _parser->setLexicalHandler(_handler);
  _parser->setErrorHandler(_handler);
}

StatusListener::~StatusListener()
{
  xercesc::XMLPlatformUtils::Terminate();
  delete _handler;
  delete _parser;
}

int StatusListener::run() throw(n_u::Exception)
{
  // create a socket to listen for the XML status messages
  n_u::MulticastSocket msock(DSM_MULTICAST_STATUS_PORT);
  msock.joinGroup(n_u::Inet4Address::getByName(DSM_MULTICAST_ADDR));
  n_u::Inet4SocketAddress from;
  char buf[8192];

  for (;;) {
    // blocking read on multicast socket
    size_t l = msock.recvfrom(buf,sizeof(buf),0,from);
    if (l==8192) throw n_u::Exception(" char *buf exceeded!");

//    cerr << buf << endl;
    // convert char* buf into a parse-able memory stream
    xercesc::MemBufInputSource* memBufIS = new xercesc::MemBufInputSource
    (
       (const XMLByte*)buf
       , strlen(buf)
       , "fakeSysId"
       , false
    );
    try {
      _parser->parse(*memBufIS);
    }
    catch (const xercesc::OutOfMemoryException&) {
        cerr << "OutOfMemoryException" << endl;
        delete memBufIS;
        return 0;
    }
    catch (const xercesc::XMLException& e) {
        cerr << "\nError during parsing memory stream:\n"
             << "Exception message is:  \n"
             << XMLStringConverter(e.getMessage()) << "\n" << endl;
        delete memBufIS;
        return 0;
    }
    delete memBufIS;
  }
}

// ----------

void GetClocks::execute(XmlRpc::XmlRpcValue& params,
	XmlRpc::XmlRpcValue& result)
{
//cerr << "GetClocks" << endl;
  map<string, string>::iterator mi;
  for (mi  = _listener->_clocks.begin();
       mi != _listener->_clocks.end();    ++mi)
  {
    // only mark stalled numeric time changes as '-- stopped --'
    if (mi->second[0] == '2') // as in 2006 ...
    {
      if (mi->second.compare(_listener->_oldclk[mi->first]) == 0) {
        if (_listener->_nstale[mi->first]++ > 3)
          mi->second = "----- stopped -----";
      }
      else
        _listener->_nstale[mi->first] = 0;
    }

    _listener->_oldclk[mi->first] = mi->second;
    result[mi->first] = mi->second;
  }
}

void GetStatus::execute(XmlRpc::XmlRpcValue& params,
	XmlRpc::XmlRpcValue& result)
{
  std::string& arg = params[0];
//cerr << "GetStatus for " << arg << endl;
  result = _listener->_status[arg];
}
