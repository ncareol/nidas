/*
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate: 2007-01-31 11:23:38 -0700 (Wed, 31 Jan 2007) $

    $LastChangedRevision: 3648 $

    $LastChangedBy: cjw $

    $HeadURL: http://svn/svn/nidas/trunk/src/nidas/core/TCPSocketIODevice.h $

*/
#ifndef NIDAS_CORE_TCPSOCKETIODEVICE_H
#define NIDAS_CORE_TCPSOCKETIODEVICE_H

#include <nidas/core/SocketIODevice.h>

namespace nidas { namespace core {

/**
 * An IODevice consisting of a TCP socket.
 */
class TCPSocketIODevice : public SocketIODevice {

public:

    /**
     * Create a TCPSocketIODevice.  No IO operations
     * are performed in the constructor, hence no IOExceptions.
     */
    TCPSocketIODevice();

    ~TCPSocketIODevice();

    /**
     * Open the socket, which does a socket connect to the remote address
     * which is parsed from the contents of getName().
     * See SocketIODevice::open() and SocketIODevice::parseAddress().
     */
    void open(int flags)
    	throw(nidas::util::IOException,nidas::util::InvalidParameterException);

    /**
     * The file descriptor used when reading from this SocketIODevice.
     */
    int getReadFd() const
    {
	if (_socket) return _socket->getFd();
	return -1;
    }

    /**
     * The file descriptor used when writing to this device.
     */
    int getWriteFd() const {
	if (_socket) return _socket->getFd();
    	return -1;
    }

    /**
     * Read from the device.
     */
    size_t read(void *buf, size_t len) throw(nidas::util::IOException)
    {
        return _socket->recv(buf,len);
    }

    /**
     * Read from the device with a timeout in milliseconds.
     */
    size_t read(void *buf, size_t len, int msecTimeout)
        throw(nidas::util::IOException);

    /**
     * Write to the device.
     */
    size_t write(const void *buf, size_t len) throw(nidas::util::IOException) 
    {
        return _socket->send(buf,len);
    }

    /**
     * close the device.
     */
    void close() throw(nidas::util::IOException);

    void setTcpNoDelay(bool val) throw(nidas::util::IOException)
    {
        _tcpNoDelay = val;
    }

    bool getTcpNoDelay() throw(nidas::util::IOException)
    {
	return _tcpNoDelay;
    }

    void setKeepAliveIdleSecs(int val) throw(nidas::util::IOException)
    {
	_keepAliveIdleSecs = val;
    }

    int getKeepAliveIdleSecs() const throw(nidas::util::IOException)
    {
	return _keepAliveIdleSecs;
    }

protected:

    /**
     * The socket. This isn't in an auto_ptr because
     * one must close the socket prior to deleting it.
     * The nidas::util::Socket destructor does not close
     * the file descriptor.
     */
    nidas::util::Socket* _socket;

    bool _tcpNoDelay;

    int _keepAliveIdleSecs;

};

}}	// namespace nidas namespace core

#endif
