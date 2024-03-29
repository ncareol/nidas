// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2007, Copyright University Corporation for Atmospheric Research
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
#ifndef NIDAS_CORE_SERVERSOCKETIODEVICE_H
#define NIDAS_CORE_SERVERSOCKETIODEVICE_H

#include "IODevice.h"
#include <nidas/util/Socket.h>
#include <nidas/util/ParseException.h>
#include <nidas/util/auto_ptr.h>


namespace nidas { namespace core {

/**
 * An IODevice supporting a TCP or UNIX server socket.
 * This class has a critical limitation and isn't currently used anywhere
 * in NIDAS. The IODevice::open() method should not block, and
 * this class violates that because open() does a
 * nidas::util::ServerSocket::accept() which can block forever.
 * To really support this class we need to spawn a ServerSocket
 * listening thread.
 */
class ServerSocketIODevice : public IODevice {

public:

    /**
     * Create a SocketIODevice.  No IO operations to the sensor
     * are performed in the constructor (hence no IOExceptions).
     */
    ServerSocketIODevice();

    virtual ~ServerSocketIODevice();

    /**
     * The file descriptor used when reading from this SocketIODevice.
     */
    int getReadFd() const
    {
	if (_socket) return _socket->getFd();
	return -1;
    }

    /**
     * The file descriptor used when writing to this sensor.
     */
    int getWriteFd() const {
	if (_socket) return _socket->getFd();
	return -1;
    }

    /**
     * Open the socket.
     *
     * @throws nidas::util::IOException
     * @throws nidas::util::InvalidParameterException
     **/
    void open(int flags);

    /**
     * Read from the sensor.
     *
     * @throws nidas::util::IOException
     **/
    size_t read(void *buf, size_t len)
    {
        return _socket->recv(buf,len);
    }

    /**
     * Read from the sensor with a timeout in milliseconds.
     *
     * @throws nidas::util::IOException
     **/
    size_t read(void *buf, size_t len, int msecTimeout)
    {
	size_t l = 0;
	try {
		_socket->setTimeout(msecTimeout);
		l = _socket->recv(buf,len,msecTimeout);
		_socket->setTimeout(0);
	}
	catch(const nidas::util::IOException& e) {
		_socket->setTimeout(0);
		throw e;
        }
	return l;
    }

    /**
     * Write to the sensor.
     *
     * @throws nidas::util::IOException
     **/
    size_t write(const void *buf, size_t len)
    {
        return _socket->send(buf,len);
    }

    /**
     * Perform an ioctl on the device. Not necessary for a socket,
     * and will throw an IOException.
     *
     * @throws nidas::util::IOException
     **/
    void ioctl(int, void*, size_t)
    {
        throw nidas::util::IOException(getName(),
		"ioctl","not supported on SocketIODevice");
    }

    /**
     * close the sensor (and any associated FIFOs).
     *
     * @throws nidas::util::IOException
     **/
    void close();

    /**
     * @throws nidas::util::IOException
     **/
    void setTcpNoDelay(bool val)
    {
        _tcpNoDelay = val;
    }

    /**
     * @throws nidas::util::IOException
     **/
    bool getTcpNoDelay()
    {
        return _tcpNoDelay;
    }

protected:

    /**
     * @throws nidas::util::IOException
     **/
    void closeServerSocket();

private:

    /**
     * The type of the destination address, AF_INET or AF_UNIX.
     */
    int _addrtype;

    /**
     * Path name of AF_UNIX socket.
     */
    std::string _unixPath;

    /**
     * Port number that is parsed from sensor name.
     */
    int _sockPort;

    /**
     * The destination socket address.
     */
    nidas::util::auto_ptr<nidas::util::SocketAddress> _sockAddr;

    /**
     * The listen socket.  This isn't in an auto_ptr because
     * one must close the socket prior to deleting it.
     * The nidas::util::Socket destructor does not close
     * the file descriptor.
     */
    nidas::util::ServerSocket* _serverSocket;

    nidas::util::Socket* _socket;

    bool _tcpNoDelay;

    /** No copying. */
    ServerSocketIODevice(const ServerSocketIODevice&);

    /** No assignment. */
    ServerSocketIODevice& operator=(const ServerSocketIODevice&);

};

}}	// namespace nidas namespace core

#endif
