// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2006, Copyright University Corporation for Atmospheric Research
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

#include "SerialPort.h"
#include "Logger.h"
#include <sys/ioctl.h>
#include <sys/param.h>	// MAXPATHLEN
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>

#include <sstream>

using namespace std;
using namespace nidas::util;

SerialPort::SerialPort(const std::string& name):
    _termios(),
    _fd(-1),_name(name),_state(OK),
    _savep(0),_savebuf(0),_savelen(0),
    _savealloc(0),_blocking(true)
{
}

SerialPort::SerialPort():
    _termios(),
    _fd(-1),_name("/dev/unknown"), _state(OK),
    _savep(0),_savebuf(0),_savelen(0),
    _savealloc(0),_blocking(true)
{
}

SerialPort::SerialPort(const std::string& name, int fd):
    _termios(fd,name),_fd(fd),_name(name),_state(OK),
    _savep(0),_savebuf(0),_savelen(0),
    _savealloc(0),_blocking(true)
{
    getBlocking();
}

SerialPort::SerialPort(const SerialPort& x):
    _termios(x._termios),
    _fd(0),_name(x._name), _state(OK),
    _savep(0),_savebuf(0),_savelen(0),
    _savealloc(0),_blocking(x._blocking)
{
}

SerialPort::~SerialPort()
{
    close();
    delete [] _savebuf;
}

void
SerialPort::close()
{
    if (_fd >= 0) {
        ::close(_fd);
        ILOG(("closing: ") << getName());
    }
    _fd = -1;
}

int
SerialPort::open(int mode)
{
    ILOG(("opening: ") << getName());

    if ((_fd = ::open(_name.c_str(),mode)) < 0)
        throw IOException(_name,"open",errno);
    _termios.apply(_fd,_name);
    setBlocking(_blocking);
    return _fd;
}

void SerialPort::applyTermios()
{
    _termios.apply(_fd,_name);
}

void
SerialPort::setBlocking(bool val)
{
    if (_fd < 0) {
        _blocking = val;
        return;
    }
    int flags;
    if ((flags = fcntl(_fd,F_GETFL)) < 0)
        throw IOException(_name,"fcntl F_GETFL",errno);

    if (val) flags &= ~O_NONBLOCK;
    else flags |= O_NONBLOCK;

    if (fcntl(_fd,F_SETFL,flags) < 0)
        throw IOException(_name,"fcntl F_SETFL",errno);
    _blocking = val;
}

bool
SerialPort::getBlocking() {
    if (_fd < 0) return _blocking;

    int flags;
    if ((flags = fcntl(_fd,F_GETFL)) < 0)
        throw IOException(_name,"fcntl F_GETFL",errno);

    _blocking = (flags & O_NONBLOCK) == 0;
    return _blocking;
}

int
SerialPort::getModemStatus()
{
    int modem=0;
    if (::ioctl(_fd, TIOCMGET, &modem) < 0)
        throw IOException(_name,"ioctl TIOCMGET",errno);
    return modem;
}

void
SerialPort::setModemStatus(int val)
{
    if (::ioctl(_fd, TIOCMSET, &val) < 0)
        throw IOException(_name,"ioctl TIOCMSET",errno);
}

void
SerialPort::clearModemBits(int bits)
{
    if (::ioctl(_fd, TIOCMBIC, &bits) < 0)
        throw IOException(_name,"ioctl TIOCMBIC",errno);
}

void
SerialPort::setModemBits(int bits)
{
    if (::ioctl(_fd, TIOCMBIS, &bits) < 0)
        throw IOException(_name,"ioctl TIOCMBIS",errno);
}

bool
SerialPort::getCarrierDetect()
{
    return (getModemStatus() & TIOCM_CAR) != 0;
}

string
SerialPort::modemFlagsToString(int modem)
{
    string res;

#ifdef SHOW_ALL_ON_OFF
    static const char *offon[]={"OFF","ON"};
#endif

    static int status[] = {
        TIOCM_LE, TIOCM_DTR, TIOCM_RTS, TIOCM_ST, TIOCM_SR,
        TIOCM_CTS, TIOCM_CAR, TIOCM_RNG, TIOCM_DSR};
    static const char *lines[] =
    {"LE","DTR","RTS","ST","SR","CTS","CD","RNG","DSR"};

    for (unsigned int i = 0; i < sizeof status / sizeof(int); i++) {
#ifdef SHOW_ALL_ON_OFF
        res += lines[i];
        res += '=';
        res += offon[(modem & status[i]) != 0];
        res += ' ';
#else
        if (modem & status[i]) res += string(lines[i]) + ' ';
#endif
    }
    return res;
}

void
SerialPort::drain()
{
    if (tcdrain(_fd) < 0)
        throw IOException(_name,"tcdrain",errno);
}

void
SerialPort::flushOutput()
{
    if (tcflush(_fd,TCOFLUSH) < 0)
        throw IOException(_name,"tcflush TCOFLUSH",errno);
}

void
SerialPort::flushInput()
{
    if (tcflush(_fd,TCIFLUSH) < 0)
        throw IOException(_name,"tcflush TCIFLUSH",errno);
}

void
SerialPort::flushBoth()
{
    if (tcflush(_fd,TCIOFLUSH) < 0)
        throw IOException(_name,"tcflush TCIOFLUSH",errno);
}

int
SerialPort::readUntil(char *buf, int len,char term)
{
    len--;		// allow for trailing null
    int toread = len;
    int rd,i,l;

    // check for data left from last read
    if (_savelen > 0) {

        l = toread < _savelen ? toread : _savelen;
        // #define DEBUG
#ifdef DEBUG
        cerr << "_savelen=" << _savelen << " l=" << l << endl;
#endif
        for (i = 0; i < l; i++) {
            toread--;_savelen--;
            if ((*buf++ = *_savep++) == term) break;
        }
        if (i < l) {	// term found
            *buf = '\0';
            return len - toread;
        }
#ifdef DEBUG
        cerr << "_savelen=" << _savelen << " l=" << l << " i=" << i << endl;
#endif
    }

    while (toread > 0) {
        switch(rd = read(buf,toread)) {
        case 0:		// EOD or timeout, user must figure out which
            *buf = '\0';
            return len - toread;
        default:
            for (; rd > 0;) {
                rd--;
                toread--;
#ifdef DEBUG
                cerr << "buf char=" << hex << (int)(unsigned char) *buf <<
                    " term=" << (int)(unsigned char) term << dec << endl;
#endif
                if (*buf++ == term) {
                    // save chars after term
                    if (rd > 0) {
                        if (rd > _savealloc) {
                            delete [] _savebuf;
                            _savebuf = new char[rd];
                            _savealloc = rd;
                        }
                        ::memcpy(_savebuf,buf,rd);
                        _savep = _savebuf;
                        _savelen = rd;
                    }
                    *buf = '\0';
                    return len - toread;
                }
            }
#ifdef DEBUG
            cerr << "rd=" << rd << " toread=" << toread << " _savelen=" << _savelen << endl;
#endif
            break;
        }
    }
    *buf = '\0';
    return len - toread;
}

int
SerialPort::readLine(char *buf, int len)
{
    return readUntil(buf,len,'\n');
}

int
SerialPort::write(const void *buf, int len)
{
    if ((len = ::write(_fd,buf,len)) < 0) {
        if (!_blocking && errno == EAGAIN) return 0;
        throw IOException(_name,"write",errno);
    }
    return len;
}

int
SerialPort::read(char *buf, int len)
{
    if ((len = ::read(_fd,buf,len)) < 0)
        throw IOException(_name,"read",errno);
    // set the state for buffered read methods
    _state = (len == 0) ? TIMEOUT_OR_EOF : OK;
#ifdef DEBUG
    cerr << "SerialPort::read len=" << len << endl;
#endif
    return len;
}

/**
 * Do a buffered read and return character read.
 * If '\0' is read, then do a check of timeoutOrEOF()
 * to see if the basic read returned 0.
 */
char
SerialPort::readchar()
{
    if (_savelen == 0) {
        if (_savealloc == 0) {
            delete [] _savebuf;
            _savealloc = 512;
            _savebuf = new char[_savealloc];
        }

        switch(_savelen = read(_savebuf,_savealloc)) {
        case 0:
            return '\0';
        default:
            _savep = _savebuf;
        }
    }
    _savelen--;
    return *_savep++;
}

namespace {
    const char* PTMX = "/dev/ptmx";

    /**
     * Return ptsname(fd), but raise IOException if the name is null.
     */
    const char*
    get_ptsname(int fd)
    {
        char* slave = ptsname(fd);
        if (!slave)
            throw IOException(PTMX, "ptsname", errno);
        return slave;
    }
}

/* static */
int SerialPort::createPty(bool hup)
{
    int fd;

    // could also use getpt() here.
    if ((fd = ::open(PTMX, O_RDWR|O_NOCTTY)) < 0) 
        throw IOException(PTMX, "open", errno);

    if (grantpt(fd) < 0) throw IOException(PTMX, "grantpt", errno);
    if (unlockpt(fd) < 0) throw IOException(PTMX, "unlockpt", errno);

    if (hup)
    {
        // maybe it's safe to do this always?
        const char* pts = get_ptsname(fd);
        DLOG(("opening and closing pty to set HUP: ") << pts);
        ::close(::open(pts, O_RDWR | O_NOCTTY));
    }
    return fd;
}


/* static */
void SerialPort::createLinkToPty(const std::string& link, int fd)
{
    const char* slave = get_ptsname(fd);

    bool dolink = true;
    struct stat linkstat;
    if (lstat(link.c_str(),&linkstat) < 0) {
        if (errno != ENOENT)
            throw IOException(link,"stat",errno);
    }
    else {
        if (S_ISLNK(linkstat.st_mode)) {
            char linkdest[MAXPATHLEN];
            int ld = readlink(link.c_str(),linkdest,MAXPATHLEN-1);
            if (ld < 0)
                throw IOException(link,"readlink",errno);
            linkdest[ld] = 0;
            if (strcmp(slave,linkdest)) {
                cerr << "Deleting " << link << " (a symbolic link to " << linkdest << ")" << endl;
                if (unlink(link.c_str()) < 0)
                    throw IOException(link,"unlink",errno);
            }
            else dolink = false;
        }
        else
            throw IOException(link,
                    "exists and is not a symbolic link","");

    }
    if (dolink) {
        cerr << "Linking " << slave << " to " << link << endl;
        if (symlink(slave,link.c_str()) < 0)
            throw IOException(link,"symlink",errno);
    }
}


/* static */
int SerialPort::createPtyLink(const std::string& link)
{
    int fd = createPty(false);
    createLinkToPty(link, fd);
    return fd;
}


/* static */
bool SerialPort::waitForOpen(int fd, int timeout)
{
    do {
        struct pollfd pfd = { .fd = fd, .events = 0, .revents = 0 };
        poll(&pfd, 1, 10);
        if (!(pfd.revents & POLLHUP))
            return true;
        sleep(1);
    }
    while (timeout < 0 || --timeout > 0);
    return false;
}
