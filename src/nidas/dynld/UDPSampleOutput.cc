// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
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

#include "UDPSampleOutput.h"
#include <nidas/core/Project.h>
#include <nidas/core/XMLParser.h>
#include <nidas/core/XMLFdFormatTarget.h>
#include <nidas/core/XMLWriter.h>
#include <nidas/core/MultipleUDPSockets.h>
#include <nidas/core/NidsIterators.h>
#include <nidas/core/DSMConfig.h>
#include <nidas/core/Site.h>
#include <nidas/core/SamplePipeline.h>
#include <nidas/util/Logger.h>

#include <byteswap.h>

using namespace nidas::dynld;
using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

NIDAS_CREATOR_FUNCTION(UDPSampleOutput)

UDPSampleOutput::UDPSampleOutput():
    _mochan(0),_doc(0),
    _projectChanged(true), _docLock(),_docRWLock(),
    _listenerLock(),
    _xmlPortNumber(NIDAS_VARIABLE_LIST_PORT_TCP),
    _multicastOutPort(NIDAS_DATA_PORT_UDP),
    _listener(0),_monitor(0),
    _nbytesOut(0),_buffer(0),_head(0),_tail(0),_buflen(0),_eob(0),
    _lastWrite(0),_maxUsecs(USECS_PER_SEC/4)
{
}

UDPSampleOutput::UDPSampleOutput(UDPSampleOutput&,IOChannel*):
    _mochan(0),_doc(0),
    _projectChanged(true), _docLock(),_docRWLock(),
    _listenerLock(),
    _xmlPortNumber(NIDAS_VARIABLE_LIST_PORT_TCP),
    _multicastOutPort(NIDAS_DATA_PORT_UDP),
    _listener(0),_monitor(0),
    _nbytesOut(0),_buffer(0),_head(0),_tail(0),_buflen(0),_eob(0),
    _lastWrite(0),_maxUsecs(USECS_PER_SEC/4)
{
    n_u::Logger::getInstance()->log(LOG_ERR,
        "Programming error: cannot clone a UDPSampleOutput");
    assert(!"cannot clone");
}

UDPSampleOutput* UDPSampleOutput::clone(IOChannel*)
{
    n_u::Logger::getInstance()->log(LOG_ERR,
        "Programming error: cannot clone a UDPSampleOutput");
    assert(!"cannot clone");
    return 0;
}

UDPSampleOutput::~UDPSampleOutput()
{
    if (_listener && _listener->isRunning()) {
        _listener->interrupt();
        _listener->join();
    }
    if (_monitor && _monitor->isRunning()) {
        _monitor->interrupt();
        _monitor->join();
    }
    delete _listener;
    delete _monitor;
    // _mochan (_iochan) is deleted by ~SampleOutputBase.
}

void UDPSampleOutput::allocateBuffer(size_t len)
{
    if (_buffer) {
        char* newbuf = new char[len];
        // will silently lose data if len  is too small.
        size_t wlen = _head - _tail;
        if (wlen > len) wlen = len;
        memcpy(newbuf,_tail,wlen);

        delete [] _buffer;
        _buffer = newbuf;
        _buflen = len;
        _tail = _buffer;
        _head = _tail + wlen;
    }
    else {
        _buffer = new char[len];
        _buflen = len;
        _head = _tail = _buffer;
    }
    _eob = _buffer + _buflen;
}

SampleOutput* UDPSampleOutput::connected(IOChannel* ochan) throw()
{
    // A packet was received on a DatagramSocket, asking
    // for data.
    // ochan is a new nidas::core::DatagramSocket
    assert(_mochan);
    assert(_mochan != ochan);
    {
        n_u::Autolock xloc(_listenerLock);
        if (!_listener) {
            _monitor = new ConnectionMonitor(_mochan);
            _monitor->start();
            _listener = new XMLSocketListener(this,_xmlPortNumber,_monitor);
            _listener->start();
        }
        if (!_buffer) allocateBuffer(ochan->getBufferSize());
    }

    /* The connection info contains
     *      remoteSocketAddress: 
     *          address of sender's interface, port it was sent from
     *      destination address
     *          where it was sent to on this server, could be a
     *          multicast address
     * If the destination is a multicast address, then the
     * outgoing data packets will be sent to the _multicastOutPort
     * and the multicast address group.
     */
    _monitor->addDestination(ochan->getConnectionInfo(), _multicastOutPort);

    list<string> strings;
    // strings.push_back(NIDAS_MULTICAST_ADDR);

#ifdef HOST_NAME_MAX
    char hname[HOST_NAME_MAX];
#elif defined MAXHOSTNAMELEN
    char hname[MAXHOSTNAMELEN];
#endif
    if (gethostname(hname,sizeof(hname)) < 0) {
       WLOG(("%s",n_u::IOException(getName(),"gethostname",errno).what()));
       hname[sizeof(hname) -1] = '\0';
    }
    strings.push_back(hname);

    strings.push_back(Project::getInstance()->getName());

    SiteIterator si = Project::getInstance()->getSiteIterator();
    for ( ; si.hasNext(); ) {
        const Site* site = si.next();
        DSMConfigIterator di = site->getDSMConfigIterator();
        for ( ; di.hasNext(); ) {
            const DSMConfig* dsm = di.next();
            strings.push_back(dsm->getName());
        }
    }
    int slen = 0;
    for (list<string>::const_iterator si = strings.begin(); si != strings.end(); ++si)
        slen += si->length() + 1;

    int rlen = sizeof(InitialUDPDataRequestReply) + slen;
    // cerr << "sizeof(InitialUDPDataRequestReply) =" << sizeof(InitialUDPDataRequestReply) << endl;
    vector<char> replyBuf(rlen);
    InitialUDPDataRequestReply* reply = (InitialUDPDataRequestReply*) &replyBuf.front();

    char *cp = reply->strings;
    for (list<string>::const_iterator si = strings.begin(); si != strings.end(); ++si) {
        strcpy(cp,si->c_str());
        // cerr << "string (length=" << strlen(cp) << ") cp=" << cp << endl;
        cp += si->length() + 1;
    }

    try {
        reply->magic = htonl(reply->MAGIC);
        reply->xmlTcpPort = htons(_xmlPortNumber);
        reply->dataMulticastPort = htons(_multicastOutPort);
        ochan->write(reply,rlen);
    }
    catch(const n_u::IOException& e) {
        WLOG(("%s: %s",getName().c_str(),e.what()));
    }
    ochan->close();
    delete ochan;

    return SampleOutputBase::connected(_mochan);
}

void UDPSampleOutput::close()
{
    if (_listener && _listener->isRunning()) {
        _listener->interrupt();
        _listener->join();
    }
    if (_monitor && _monitor->isRunning()) {
        _monitor->interrupt();
        _monitor->join();
    }
    SampleOutputBase::close();
}

bool UDPSampleOutput::receive(const Sample* samp) throw()
{
    if (!getIOChannel()) return false;

    try {

        SampleHeader header;
        struct iovec iov[2];

        if (__BYTE_ORDER == __BIG_ENDIAN)
        {
            header.setTimeTag(bswap_64(samp->getTimeTag()));
            header.setDataByteLength(bswap_32(samp->getDataByteLength()));
            header.setRawId(bswap_32(samp->getRawId()));
            iov[0].iov_base = &header;
            iov[0].iov_len = SampleHeader::getSizeOf();
            // TODO: must also endian flip the data. assert that it is a
            // float sample.
        }
        else
        {
            iov[0].iov_base = const_cast<void*>(samp->getHeaderPtr());
            iov[0].iov_len = samp->getHeaderLength();
        }

        iov[1].iov_base = const_cast<void*>(samp->getConstVoidDataPtr());
        iov[1].iov_len = samp->getDataByteLength();

        size_t l = write(iov,2);
        if (l == 0) {
            if (!(incrementDiscardedSamples() % 1000))
                n_u::Logger::getInstance()->log(LOG_WARNING,
                    "%s: %zd samples discarded due to output jambs\n",
                    getName().c_str(),getNumDiscardedSamples());
        }
    }
    catch(const n_u::IOException& ioe) {
        n_u::Logger::getInstance()->log(LOG_ERR,
            "%s: %s",getName().c_str(),ioe.what());
        // this disconnect will schedule this object to be deleted
        // in another thread, so don't do anything after the
        // disconnect except return;
        disconnect();
        return false;
    }
    return true;
}

size_t UDPSampleOutput::write(const struct iovec* iov,int iovcnt)
{
    size_t l;
    int ibuf;

    /* compute total length of user buffers */
    size_t tlen = 0;
    for (ibuf = 0; ibuf < iovcnt; ibuf++) tlen += iov[ibuf].iov_len;

    dsm_time_t tnow = n_u::getSystemTime();
    dsm_time_t tdiff = tnow - _lastWrite;       // microseconds

    // Only make two attempts at most.  Most likely the first attempt will
    // be enough to copy in the user buffers and then potentially write it
    // out.  The second attempt is in case the current buffer must first be
    // written to make room for the user buffers.
    for (int attempts = 0; attempts < 2; ++attempts)
    {
        /* number of bytes currently in buffer */
        size_t wlen = _head - _tail;

        /* space available in buffer */
        size_t space = _eob - _head;

        // If there's space now for this write in the buffer, add it.
        if (tlen <= space) {
            for (ibuf = 0; ibuf < iovcnt; ibuf++) {
                l = iov[ibuf].iov_len;
                memcpy(_head,iov[ibuf].iov_base,l);
                _head += l;
            }
            // Indicate the user buffers have been added.
            iovcnt = 0;
            wlen = _head - _tail;
            space = _eob - _head;
        }

        // If maxUsecs has elapsed since the last write, or else we need to
        // write to make room for the user buffers.
        if (iovcnt > 0 || (wlen > 0 && tdiff >= _maxUsecs)) {
            if (wlen > 0) {
                l = getIOChannel()->write(_tail,wlen);
                addNumOutputBytes(l);
                // datagrams, we assume write was complete
                _tail = _head = _buffer;        // empty buffer
                space = _eob - _head;
            }
            // Large sample, write as one packet.
            if (tlen > _buflen) {
                l = getIOChannel()->write(iov,iovcnt);
                addNumOutputBytes(l);
                iovcnt = 0;
            }

           // Note this just updates lastWrite and does not change tdiff.
            // We want the second time around the loop to write the user
            // buffers if it's been too long.
            _lastWrite = tnow;
        }

        // We're done when the user buffers have been copied into this buffer.
        if (iovcnt == 0) break;
    }

    // Return zero when the user buffers could not be copied, which happens
    // when writes for the data currently in the buffer don't succeed.
    return tlen;
}

xercesc::DOMDocument* UDPSampleOutput::getProjectDOM()
{

    /*
    <project name="xxx">
        <site name="xxx">
            <dsm name="xxx">
                <sample id="N">
                    <variable name="u.3m" units="m/s" longname="xxx"/>
                </sample>
            </dsm>
        </site>
    </project>
    */

    n_u::Autolock aloc1(_docLock);
    if (!_doc || _projectChanged) {
        n_u::AutoWrLock aloc2(_docRWLock);
        if (_doc) _doc->release();
        _doc = XMLImplementation::getImplementation()->createDocument(
                DOMable::getNamespaceURI(), (const XMLCh *)XMLStringConverter("project"),0);
        xercesc::DOMElement* elem = _doc->getDocumentElement();
        Project::getInstance()->toDOMElement(elem,false);
    }
    _docRWLock.rdlock();
    return _doc;
}

void UDPSampleOutput::releaseProjectDOM()
{
    _docRWLock.unlock();
}

void UDPSampleOutput::fromDOMElement(const xercesc::DOMElement* node)
{
    SampleOutputBase::fromDOMElement(node);
    if (getIOChannel()->getRequestType() < 0)
            getIOChannel()->setRequestType(UDP_PROCESSED_SAMPLE_FEED);
    // the unfortunate dynamic_cast ...
    _mochan = dynamic_cast<MultipleUDPSockets*>(getIOChannel());

    const std::list<const Parameter*>& params = getParameters();
    list<const Parameter*>::const_iterator pi;
    for (pi = params.begin(); pi != params.end(); ++pi) {
        const Parameter* param = *pi;
        const string& pname = param->getName();
        if (pname == "xmlPort") {
                if (param->getType() != Parameter::INT_PARAM ||
                    param->getLength() != 1)
                    throw n_u::InvalidParameterException(getName(),"UDPSampleOutput",
                        "xmlPort parameter is not an integer");
                _xmlPortNumber = (int)param->getNumericValue(0);
        }
        else if (pname == "dataPort") {
                if (param->getType() != Parameter::INT_PARAM ||
                    param->getLength() != 1)
                    throw n_u::InvalidParameterException(getName(),"UDPSampleOutput",
                        "dataPort parameter is not an integer");
                _multicastOutPort = (int)param->getNumericValue(0);
        }
    }
    _mochan->setDataPort(_multicastOutPort);
}

UDPSampleOutput::ConnectionMonitor::ConnectionMonitor(MultipleUDPSockets* msock):
    Thread("ConnectionMonitor"), _msock(msock),
    _pendingSockets(),_pendingRemoveSockets(),_sockets(),_destinations(),
    _changed(false),_sockLock(),_fds(0),_nfds(0)
{
    blockSignal(SIGHUP);
    blockSignal(SIGINT);
    blockSignal(SIGTERM);
}

UDPSampleOutput::ConnectionMonitor::~ConnectionMonitor()
{
    n_u::Autolock al(_sockLock);
    for (vector<pair<n_u::Socket*,unsigned short> >::const_iterator si =
        _sockets.begin(); si != _sockets.end(); ++si) {
        n_u::Socket* sock = si->first;
        sock->close();
        delete sock;
    }
    for (list<pair<n_u::Socket*,unsigned short> >::const_iterator si = _pendingSockets.begin();
        si != _pendingSockets.end(); ++si) {
        n_u::Socket* sock = si->first;
        sock->close();
        delete sock;
    }
    for (list<pair<n_u::Socket*,unsigned short> >::const_iterator si = _pendingRemoveSockets.begin();
        si != _pendingRemoveSockets.end(); ++si) {
        n_u::Socket* sock = si->first;
        sock->close();
        delete sock;
    }
    delete [] _fds;
}

void UDPSampleOutput::ConnectionMonitor::addDestination(
    const ConnectionInfo& info, unsigned short mcport)
{
    /* The connection info is the information associated with
     * the request packet on the UDP port.
     *      remoteSocketAddress: 
     *          address of sender's interface, port it was sent from
     *      destination address
     *          where it was sent to. Could be a multicast address
     *      local interface address that the packet was received on.
     *          An output multicast socket is created for every
     *          interface that clients request on.
     * mcport is the multicast port where multicast clients will listen
     * for data packets.
     */
    n_u::Autolock al(_sockLock);
    n_u::Inet4SocketAddress s4addr = info.getRemoteSocketAddress();

    // remote client will switch its port to the multicast port
    if (info.getDestinationAddress().isMultiCastAddress())
        s4addr.setPort(mcport);

    ILOG(("ConnectionMonitor: addDestination: ") << s4addr.toAddressString());
    _destinations[s4addr] = info;
}

void UDPSampleOutput::ConnectionMonitor::addConnection(n_u::Socket* sock,
        unsigned short udpport)
{
    // remote client has established a TCP connection in order to
    // fetch the XML. It has sent a packet containing the udpport
    // that it expects data to be sent to, which can be either
    // unicast or multicast.
    const n_u::SocketAddress& saddr = sock->getRemoteSocketAddress();
    if (saddr.getFamily() == AF_INET) {
        n_u::Inet4SocketAddress s4addr =
            n_u::Inet4SocketAddress((const struct sockaddr_in*)
                saddr.getConstSockAddrPtr());
        s4addr.setPort(udpport);

        n_u::Autolock al(_sockLock);
        map<n_u::Inet4SocketAddress,ConnectionInfo>::const_iterator mi =
            _destinations.find(s4addr);
        if (mi != _destinations.end()) {
            // send the connection info to the
            // nidas::core::MultipleUDPSockets
            _msock->addClient(mi->second);
            _pendingSockets.push_back(pair<n_u::Socket*,unsigned short>(sock,udpport));
            _changed = true;
        }
        else WLOG(("%s: ",getName().c_str()) << " connection " <<
            s4addr.toAddressString() << ", not found, cannot add");
    }
}

void UDPSampleOutput::ConnectionMonitor::removeConnection(n_u::Socket* sock,
    unsigned short udpport)
{
    n_u::Autolock al(_sockLock);

    _pendingRemoveSockets.push_back(pair<n_u::Socket*,unsigned short>(sock,udpport));

    const n_u::SocketAddress& saddr = sock->getRemoteSocketAddress();
    if (saddr.getFamily() == AF_INET) {
        n_u::Inet4SocketAddress s4addr =
            n_u::Inet4SocketAddress((const struct sockaddr_in*)
                saddr.getConstSockAddrPtr());
        s4addr.setPort(udpport);

        map<n_u::Inet4SocketAddress,ConnectionInfo>::iterator mi =
            _destinations.find(s4addr);
        if (mi != _destinations.end()) _destinations.erase(mi);
        else WLOG(("%s: ",getName().c_str()) << " connection " <<
            s4addr.toAddressString() << " not found, cannot remove");
        _msock->removeClient(s4addr);
    }
    _changed = true;
}

void UDPSampleOutput::ConnectionMonitor::updatePollfds()
{
    n_u::Autolock al(_sockLock);
    for (list<pair<n_u::Socket*,unsigned short> >::const_iterator si = _pendingSockets.begin();
        si != _pendingSockets.end(); ++si)
        _sockets.push_back(*si);
    _pendingSockets.clear();

    for (list<pair<n_u::Socket*,unsigned short> >::const_iterator si = _pendingRemoveSockets.begin();
        si != _pendingRemoveSockets.end(); ++si) {
        n_u::Socket* sock = si->first;
        vector<pair<n_u::Socket*,unsigned short> >::iterator vi = _sockets.begin();
        for ( ; vi != _sockets.end(); ++vi) {
            if (vi->first == sock) {
                _sockets.erase(vi);
                sock->close();
                delete sock;
                break;
            }
        }
    }
    _pendingRemoveSockets.clear();

    delete [] _fds;
    _fds = 0;
    _nfds = (signed) _sockets.size();
    // cerr << "_nfds=" << _nfds << endl;
    if (_nfds > 0) {
        _fds = new struct pollfd[_nfds];
        for (int i = 0; i < _nfds; i++) {
            _fds[i].fd = _sockets[i].first->getFd();
            _fds[i].events = POLLHUP | POLLERR;
        }
    }
    _changed = false;
}

int UDPSampleOutput::ConnectionMonitor::run()
{

    for (;!amInterrupted();) {
        if (_changed) updatePollfds();
        int res;
        if ((res = ::poll(_fds,_nfds,MSECS_PER_SEC)) < 0) {
            int ierr = errno;   // Inet4SocketAddress::toString changes errno
            throw n_u::IOException(_msock->getName(),"poll",ierr);
        }
        if (res > 0) {
            for (int i = 0; res > 0 && i < _nfds; i++) {
                if (_fds[i].revents != 0) {
                    res--;
                    pair<n_u::Socket*,unsigned short> p = _sockets[i];
                    n_u::Socket* sock = p.first;
                    ILOG(("Monitor received POLLHUP/POLLERR on socket ") << i << ' ' <<
                        sock->getRemoteSocketAddress().toAddressString());
                    removeConnection(sock,p.second);
                }
            }
        }
    }
    return RUN_OK;
}
UDPSampleOutput::XMLSocketListener::XMLSocketListener(UDPSampleOutput* output,
    int xmlPortNumber,ConnectionMonitor* monitor):
    Thread("XMLSocketListener"), _output(output),
    _sock(0),_monitor(monitor),_workers(), _xmlPortNumber(xmlPortNumber)
{
    // install signal handler SIGUSR1
    unblockSignal(SIGUSR1);
    // block SIGUSR1, then unblock in pselect
    blockSignal(SIGUSR1);
}

UDPSampleOutput::XMLSocketListener::~XMLSocketListener()
{
    if (_sock) _sock->close();
    delete _sock;
    fireWorkers();
}

int UDPSampleOutput::XMLSocketListener::run()
{
    _sock = new n_u::ServerSocket(_xmlPortNumber);

    // get the existing signal mask
    sigset_t sigmask;
    pthread_sigmask(SIG_BLOCK,NULL,&sigmask);
    // unblock SIGUSR1 in pselect
    sigdelset(&sigmask,SIGUSR1);

    fd_set fdset;
    for (;!amInterrupted();) {
        int fd = _sock->getFd();
        FD_ZERO(&fdset);
        FD_SET(fd, &fdset);
        // set accept() timeout to 1 second, so that we can check on our workers
        struct timespec tmpto = {1,0};
        int res;
        if ((res = ::pselect(fd+1,&fdset,0,0,&tmpto,&sigmask)) < 0) {
            int ierr = errno;   // Inet4SocketAddress::toString changes errno
            if (ierr == EINTR) break;
            throw n_u::IOException(_output->getName(),"select",ierr);
        }
        struct TCPClientResponse resp;
        if (res > 0) {
            n_u::Socket* sock;
            unsigned short udpport = 0;
            try {
                sock = _sock->accept();       // accept a connection
            }
            catch(const n_u::IOException& e) {
                ILOG(("XMLSocketListener: ") << e.what());
                break;
            }
            // after the accept, the initial response should be quick,
            // unless the client has for some reason sent less than the
            // expected response and left the socket up, or some other connection
            // has occured.  We'll set a timeout of 1 sec to detect the above.
            sock->setTimeout(MSECS_PER_SEC);
            char* ep = (char*)&resp + sizeof(resp.magic) + sizeof(resp.clientUdpPort);
            // cerr << "response size=" << (long)(ep -(char*)&resp) << endl;
            try {
                for (char* bp = (char*)&resp; bp < ep; ) {
                    size_t l = sock->recv(bp,ep-bp);
                    bp += l;
                }
            }
            catch(const n_u::IOException& e) {
                ILOG(("XMLSocketListener: tcp connection failed from ") <<
                    sock->getRemoteSocketAddress().toAddressString() << ": " <<
                    e.what());
                sock->close();
                delete sock;
                continue;
            }
            if (ntohl(resp.magic) !=  InitialUDPDataRequestReply::MAGIC) {
                ILOG(("XMLSocketListener: tcp connection failed from ") <<
                    sock->getRemoteSocketAddress().toAddressString() << ": bad magic: " <<
                    hex << ntohl(resp.magic));
                sock->close();
                delete sock;
                continue;
            }
            udpport = ntohs(resp.clientUdpPort);
            ILOG(("XMLSocketListener: tcp connection from ") <<
                sock->getRemoteSocketAddress().toAddressString() << " udpport=" << udpport);
            sock->setTimeout(0);
            sock->setKeepAlive(true);
            sock->setKeepAliveIdleSecs(30);

            // if udpport == 0, the TCP socket will be closed and deleted
            // in the VariableListWorker after the XML has been sent.
            // The server does not monitor the connection in this case.
            // This generally isn't recommended when multicasting
            // data since the server will never know if anyone is listening.
            VariableListWorker* worker =
                new UDPSampleOutput::VariableListWorker(_output,sock,udpport != 0);
            worker->start();
            _workers.push_back(worker);
            if (udpport != 0) _monitor->addConnection(sock,udpport);
        }
        checkWorkers();
    }
    return RUN_OK;
}

void UDPSampleOutput::XMLSocketListener::checkWorkers()
{
    // check on my workers sending XML.
    list<VariableListWorker*>::iterator wi = _workers.begin();
    for ( ; wi != _workers.end(); ) {
        VariableListWorker* worker = *wi;
        if (!worker->isRunning()) {
            try {
                worker->join();
            }
            catch(const n_u::Exception& e) {
                PLOG(("%s: %s",getName().c_str(),e.what()));
            }
            wi = _workers.erase(wi);
            delete worker;
        }
        else ++wi;
    }
}

void UDPSampleOutput::XMLSocketListener::fireWorkers()
{
    list<VariableListWorker*>::iterator wi = _workers.begin();
    for ( ; wi != _workers.end(); ) {
        VariableListWorker* worker = *wi;
        worker->interrupt();
        try {
            worker->join();
        }
        catch(const n_u::Exception& e) {
            PLOG(("%s: %s",getName().c_str(),e.what()));
        }
        wi = _workers.erase(wi);
        delete worker;
    }
}

void UDPSampleOutput::XMLSocketListener::interrupt() 
{
    Thread::interrupt();
    _sock->close();
    fireWorkers();
}

UDPSampleOutput::VariableListWorker::VariableListWorker(UDPSampleOutput* output,
    n_u::Socket* sock,bool keepOpen):
        Thread("VariableListWorker"),
        _output(output),_sock(sock), _keepOpen(keepOpen)
{
}
UDPSampleOutput::VariableListWorker::~VariableListWorker()
{
}
void UDPSampleOutput::VariableListWorker::interrupt() 
{
    Thread::interrupt();
    _sock->close();
}

int UDPSampleOutput::VariableListWorker::run()
{

#ifdef READ_DSMS
    list<string> dsmnames;
    IOStream ios(*_sock);
    for ( ;; ) {
        char dsmname[128];
        int i = ios.readUtil(dsmname,sizeof(dsmname),'\n');
        if (i == 2 && dsmname[0] == ETX) break;
        dsnames.push_back(dsmname);
    }
#endif

    xercesc::DOMDocument* doc;
    try {
        doc = _output->getProjectDOM();
    }
    catch(const xercesc::DOMException& e) {
        throw nidas::core::XMLException(e);
    }

    try {
        XMLFdFormatTarget formatter(_sock->getRemoteSocketAddress().toAddressString(),
            _sock->getFd());
        XMLWriter writer;
        XMLStringConverter convname(_sock->getRemoteSocketAddress().toAddressString());
        xercesc::DOMLSOutput *output;
        output = XMLImplementation::getImplementation()->createLSOutput();
        output->setByteStream(&formatter);
        output->setSystemId((const XMLCh*)convname);
        writer.writeNode(output,*doc);
        output->release();
    }
    catch (const n_u::IOException& e) {
        _output->releaseProjectDOM();
        _sock->close();
        throw e;
    }
    catch (const nidas::core::XMLException& e) {
        _output->releaseProjectDOM();
        _sock->close();
        throw e;
    }
    _output->releaseProjectDOM();

    if (!_keepOpen) {
        _sock->close();
        delete _sock;
    }
    else {
        char eot = '\x04';
        _sock->send(&eot,1);
    }

    return RUN_OK;
}

/* static */
const unsigned int InitialUDPDataRequestReply::MAGIC = 0x76543210;
