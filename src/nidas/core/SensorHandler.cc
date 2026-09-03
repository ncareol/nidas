// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2004, Copyright University Corporation for Atmospheric Research
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

#include "SensorHandler.h"
#include "DSMEngine.h"
#include <nidas/util/Logger.h>
#include <nidas/util/UTime.h>

#include <cerrno>
#include <unistd.h>
#include <csignal>

using namespace std;
using namespace nidas::core;

namespace n_u = nidas::util;

SensorHandler::
SensorHandler(unsigned short rserialPort):Thread("SensorHandler"),
    _allSensors(),
    _pollingMutex(), _pollingChanged(false),
    _openedSensors(),_polledSensors(),
    _newOpenedSensors(), _pendingSensorClosures(), _pendingSensorReopens(),
    _remoteSerialSocketPort(rserialPort), _rserial(0),
    _newRserials(), _pendingRserialClosures(), _activeRserialConns(),
#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT
    _epollfd(-1), _events(0), _nevents(0),
#elif POLLING_METHOD == POLL_PSELECT
    _rfdset(),_efdset(),_nselect(0),_fds(0),
    _polled(0), _nfds(0),_nAllocFds(0), _timeout(),
#elif POLLING_METHOD == POLL_POLL
    _fds(0),
    _polled(0), _nfds(0),_nAllocFds(0), _timeout(),
#endif
#ifdef USE_NOTIFY_PIPE
    _notifyPipe(0),
#endif
    _sensorCheckTime(0),_sensorStatsTime(0),
    _sensorCheckIntervalMsecs(-1),
    _sensorCheckIntervalUsecs(0),
    _sensorStatsInterval(0),
    _opener(this),
    _fullBufferReads(),_acceptingOpens(true)
{

#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT
    assert(EPOLLIN == N_POLLIN && EPOLLERR == N_POLLERR && EPOLLHUP == N_POLLHUP);

#ifdef EPOLLRDHUP
    assert(EPOLLRDHUP == N_POLLRDHUP);
#endif

#endif
    // block SIGUSR1. It will be atomically unblocked in epoll_pwait or pselect
    blockSignal(SIGUSR1);

    setSensorStatsInterval(5 * MSECS_PER_SEC);
    _sensorStatsTime = n_u::timeCeiling(n_u::getSystemTime(), _sensorStatsInterval);
}

/**
 * Close any remaining sensors. Before this is called
 * the run method should be finished.
 */
SensorHandler::~SensorHandler()
{
    delete _rserial;
#ifdef USE_NOTIFY_PIPE
    delete _notifyPipe;
#endif

    list<PolledDSMSensor*>::const_iterator pi = _polledSensors.begin();
    for ( ; pi  != _polledSensors.end(); ++pi) {
        PolledDSMSensor* psensor = *pi;
        psensor->close();
        delete psensor;
    }

    list<DSMSensor*>::const_iterator si;
    for (si = _allSensors.begin(); si != _allSensors.end(); ++si) {
        DSMSensor* sensor = *si;
        // The DSMConfig object may have been deleted at this point. Set
        // Set it to NULL in the sensors in case a sensor tries to access
        // the DSMConfig, for example via DSMSensor::getName(), in their
        // destructor.
        sensor->setDSMConfig(0);
        delete sensor;
    }

#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT
    if (_epollfd >= 0) ::close(_epollfd);
    delete [] _events;
#endif
#if POLLING_METHOD == POLL_PSELECT || POLLING_METHOD == POLL_POLL
    delete [] _fds;
    delete [] _polled;
#endif
}

void SensorHandler::signalHandler(int /*sig*/, siginfo_t*)
{
    // DLOG(("SensorHandler::signalHandler(), sig=%s (%d)",strsignal(sig),sig));
}

void SensorHandler::calcStatistics(dsm_time_t tnow)
{
    _sensorStatsTime += _sensorStatsInterval;
    if (_sensorStatsTime < tnow) {
        // cerr << "tnow-_sensorStatsTime=" << (tnow - _sensorStatsTime) << endl;
        _sensorStatsTime = n_u::timeCeiling(tnow, _sensorStatsInterval);
    }
    list<DSMSensor*> allCopy = getAllSensors();
    list<DSMSensor*>::const_iterator si;

    for (si = allCopy.begin(); si != allCopy.end(); ++si) {
        DSMSensor *sensor = *si;
        sensor->calcStatistics(_sensorStatsInterval);
    }
}

void SensorHandler::checkTimeouts(dsm_time_t tnow)
{
    _sensorCheckTime += _sensorCheckIntervalUsecs;
    if (_sensorCheckTime < tnow) {
        // cerr << "tnow-_sensorStatsTime=" << (tnow - _sensorStatsTime) << endl;
        _sensorCheckTime = n_u::timeCeiling(tnow, _sensorCheckIntervalUsecs);
    }

    list<PolledDSMSensor*>::iterator pi;
    // _polledSensors is private to this thread, no need to lock
    for (pi = _polledSensors.begin(); pi != _polledSensors.end(); ++pi ) {
        PolledDSMSensor* psensor = *pi;
        if (psensor->checkTimeout()) scheduleReopen(psensor);
    }
}

/* returns a copy of our sensor list. */
list<DSMSensor*> SensorHandler::getAllSensors() const
{
    n_u::Synchronized autosync(_pollingMutex);
    return _allSensors;
}

/* returns a copy of our opened sensors. */
list<DSMSensor*> SensorHandler::getOpenedSensors() const
{
    n_u::Synchronized autosync(_pollingMutex);
    return _openedSensors;
}

SensorHandler::PolledDSMSensor::PolledDSMSensor(DSMSensor* sensor,
        SensorHandler* handler) throw(n_u::IOException): 
    _sensor(sensor),_handler(handler),
    _nTimeoutChecks(0), _nTimeoutChecksMax(-1), _lastCheckInterval(0)
{

#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT

#if POLLING_METHOD == POLL_EPOLL_ET
    if (::fcntl(getFd(),F_SETFL,O_NONBLOCK) < 0)
        throw n_u::IOException(getName(),"fcntl O_NONBLOCK",errno);
#endif

    epoll_event event = epoll_event();
#if POLLING_METHOD == POLL_EPOLL_ET
    event.events = EPOLLIN | EPOLLET;   // edge-triggered epoll
#else
    event.events = EPOLLIN;             // level-triggered epoll
#endif

#ifdef EPOLLRDHUP
    event.events |= EPOLLRDHUP;
#endif

    event.data.ptr = (Polled*)this;

    if (::epoll_ctl(_handler->getEpollFd(),EPOLL_CTL_ADD,getFd(),&event) < 0)
        throw n_u::IOException(getName(),"EPOLL_CTL_ADD",errno);
#endif
}

void SensorHandler::PolledDSMSensor::close() throw(n_u::IOException)
{
    if (getFd() >= 0) {
#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT
        if (::epoll_ctl(_handler->getEpollFd(),EPOLL_CTL_DEL,getFd(),NULL) < 0) {
            n_u::IOException e(getName(),"EPOLL_CTL_DEL",errno);
            _sensor->close();
            throw e;
        }
#endif
        _sensor->close();
    }
}

void SensorHandler::incrementFullBufferReads(const DSMSensor* sensor)
{
    if (!(_fullBufferReads[sensor]++ % 100))
        ILOG(("%s: %u full buffer reads",sensor->getName().c_str(),
                    _fullBufferReads[sensor]));
}

bool
SensorHandler::PolledDSMSensor::handlePollEvents(uint32_t events) throw()
{
    bool exhausted = false;
    if (events & N_POLLIN) {
        try {
            exhausted = _sensor->readSamples();
            if (!exhausted)
                _handler->incrementFullBufferReads(_sensor);
            _nTimeoutChecks = 0;
        }
        catch(n_u::IOException & ioe) {
            // report timeouts as a notice, not an error
            if (ioe.getErrno() == ETIMEDOUT)
                NLOG(("%s: %s",getName().c_str(), ioe.what()));
            else
                PLOG(("%s: %s", getName().c_str(), ioe.what()));
            if (_sensor->reopenOnIOException())
                _handler->scheduleReopen(this);
            else
                _handler->scheduleClose(this);
            return true;
        }
    }
    if (events & (N_POLLERR | N_POLLRDHUP | N_POLLHUP))
    {
        if (events & N_POLLERR)
            WLOG(("%s: POLLERR event", getName().c_str()));
        if (events & N_POLLRDHUP)
            NLOG(("%s: POLLRDHUP event", getName().c_str()));
        else if (events & N_POLLHUP)
            NLOG(("%s: POLLHUP event", getName().c_str()));
        if (_sensor->reopenOnIOException())
            _handler->scheduleReopen(this); // Try to reopen
        else
            _handler->scheduleClose(this);    // report error but don't reopen
        exhausted = true;
    }
    return exhausted;
}

bool SensorHandler::PolledDSMSensor::checkTimeout()
{
    // If data was just received, this check will increment _nTimeoutChecks to 1.
    // Therefore after the increment, _nTimeoutChecks is one greater than the
    // actual number of timeouts, hence post-increment is used in this check.

    // _nTimeout for a sensor which is not generating data but which has an
    // infinite timeout setting (of zero) could rollover and then be equal
    // to _nTimeoutChecksMax of -1, so we check _sensor->getTimeoutMsecs() in
    // the *very* unlikely event that happens.

    if (_nTimeoutChecks++ == _nTimeoutChecksMax && _sensor->getTimeoutMsecs() > 0) {
        _sensor->incrementTimeoutCount();
        if ((_sensor->getTimeoutCount() % 10) == 0)
            WLOG(("%s: timeout #%d (%.3f sec)",
                    getName().c_str(),
                    _sensor->getTimeoutCount(),
                    (float)_sensor->getTimeoutMsecs()/ MSECS_PER_SEC));
        _nTimeoutChecks = 0;
        return true;
    }
    return false;
}

void SensorHandler::PolledDSMSensor::setupTimeouts(int checkIntervalMsecs)
{
    if (checkIntervalMsecs <= 0) {
        // not monitoring timeouts, checkTimeout will not be called
        _nTimeoutChecks = 0;
        _lastCheckInterval = checkIntervalMsecs;
        return;
    }

    if (checkIntervalMsecs == _lastCheckInterval) return;

    int sto = _sensor->getTimeoutMsecs();
    if (sto > 0) {
        _nTimeoutChecksMax = (sto + checkIntervalMsecs - 1) / checkIntervalMsecs;
        if (_nTimeoutChecksMax < 1) _nTimeoutChecksMax = 1;
    }
    else _nTimeoutChecksMax = -1;

    // if the new check interval is smaller than the last one
    if (checkIntervalMsecs < _lastCheckInterval)
        _nTimeoutChecks *= (_lastCheckInterval / checkIntervalMsecs);
    else if (_lastCheckInterval > 0)
        _nTimeoutChecks /= (checkIntervalMsecs / _lastCheckInterval);

    _lastCheckInterval = checkIntervalMsecs;
}

#ifdef USE_NOTIFY_PIPE

SensorHandler::NotifyPipe::NotifyPipe(SensorHandler* handler)
    throw(n_u::IOException) : _fds(),_handler(handler)
{

    if (::pipe(_fds) < 0)
        throw n_u::IOException("SensorHandler", "pipe", errno);

#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT

#if POLLING_METHOD == POLL_EPOLL_ET
    if (::fcntl(_fds[0],F_SETFL,O_NONBLOCK) < 0)
        throw n_u::IOException("SensorHandler NotifyPipe","fcntl O_NONBLOCK",errno);
#endif

    epoll_event event = epoll_event();
#if POLLING_METHOD == POLL_EPOLL_ET
    event.events = EPOLLIN | EPOLLET;   // edge-triggered epoll
#else
    event.events = EPOLLIN;             // level-triggered epoll
#endif
    event.data.ptr = (Polled*)this;

    if (::epoll_ctl(_handler->getEpollFd(),EPOLL_CTL_ADD,_fds[0],&event) < 0)
        throw n_u::IOException("SensorHandler::NotifyPipe","EPOLL_CTL_ADD",errno);
#endif
}

SensorHandler::NotifyPipe::~NotifyPipe()
{
    try {
        close();
    }
    catch (const n_u::IOException& e) {
        PLOG(("%s", e.what()));
    }
}

void SensorHandler::NotifyPipe::close() throw(n_u::IOException)
{
    int fd0= _fds[0];
    _fds[0] = -1;
    int fd1= _fds[1];
    _fds[1] = -1;
    if (fd0 >= 0) {
#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT
        if (::epoll_ctl(_handler->getEpollFd(),EPOLL_CTL_DEL,fd0,NULL) < 0) {
            n_u::IOException e("SensorHandler::NotifyPipe","EPOLL_CTL_DEL",errno);
            ::close(fd0);
            if (fd1 >= 0) ::close(fd1);
            throw e;
        }
#endif
        if (::close(fd0) == -1) {
            if (fd1 >= 0) ::close(fd1);
            throw n_u::IOException("SensorHandler::NotifyPipe","close",errno);
        }
    }
    if (fd1 >= 0) ::close(fd1);
}

bool
SensorHandler::NotifyPipe::handlePollEvents(uint32_t events) throw()
{
    bool exhausted = false;
    if (events & N_POLLIN) {
        char buf[4];
        ssize_t l = read(_fds[0], buf, sizeof(buf));
        if (l < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                l = 0;
            }
            else {
                n_u::IOException e("SensorHandler::NotifyPipe","read",errno);
                PLOG(("%s",e.what()));
            }
        }
        exhausted = (size_t)l < sizeof(buf);
    }

    if (events & (N_POLLERR | N_POLLHUP | N_POLLRDHUP))
        PLOG(("%s", "SensorHandler::NotifyPipe epoll exception"));

    return exhausted;
}

void SensorHandler::NotifyPipe::notify() throw()
{
    if (::write(_fds[1],this,1) < 0) {
        n_u::IOException e("SensorHandler::NotifyPipe","write",errno);
        WLOG(("%s",e.what()));
    }
}

#endif

/**
 * Thread function, epoll loop.
 */
int SensorHandler::run() throw(n_u::Exception)
{

    // This run method must respond to two events from outside threads:
    // 1. sensorOpen(): a sensor is added to those to be polled
    // 2. interrupt(): user wants this run method to exit
    //
    // Since this run method is typically waiting for activity on its
    // set of file descriptors, we need a way for the above events to
    // interrupt that wait so that the value of _pollingChanged or
    // isInterrupted() can be checked in a timely manner.
    // This is done in one of two ways depending on the availability
    // of system calls that can atomically unblock and catch signals.
    // System calls pselect, ppoll and epoll_pwait can atomically unblock
    // and catch signals, whereas  select, poll and epoll_wait do not.
    //
    // If pselect, ppoll, or epoll_pwait are used, then methods sensorIsOpen()
    // and interrupt() set the _pollingChanged, or interrupt() flags, then
    // send SIGUSR1 to this thread.  Since SIGUSR1 is otherwise blocked
    // in this thread, the signal will only be delivered to the polling call.
    //
    // If instead, poll() or epoll_wait are used, then one of the file
    // descriptors that is polled is the read end of NotifyPipe.
    // The sensorOpen() and interrupt() methods set the flags, then call
    // NotifyPipe::notify() to send a byte over the pipe to cause the
    // polling wait to return and check the flag.

    dsm_time_t rtime = 0;

#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT
    if (_epollfd < 0) {
        _epollfd = epoll_create(10);
        if (_epollfd == -1)
            throw n_u::IOException("SensorHandler", "epoll_create", errno);
    }
#endif

#if !defined(USE_NOTIFY_PIPE) || POLLING_METHOD == POLL_PSELECT || defined(HAVE_EPOLL_PWAIT) || defined(HAVE_PPOLL)
    // get the existing signal mask
    sigset_t sigmask;
    pthread_sigmask(SIG_BLOCK,NULL,&sigmask);
    // unblock SIGUSR1 in epoll_pwait, pselect
    sigdelset(&sigmask,SIGUSR1);
#endif

#ifdef USE_NOTIFY_PIPE
    delete _notifyPipe;
    _notifyPipe = 0;
    _notifyPipe = new NotifyPipe(this);
#endif

    _opener.start();

    delete _rserial;
    _rserial = 0;
    if (_remoteSerialSocketPort > 0) {
        try {
            _rserial = new RemoteSerialListener(_remoteSerialSocketPort,this);
        }
        catch(const n_u::IOException & e)
        {
            n_u::Logger::getInstance()->log(LOG_WARNING,
                                            "%s: continuing anyhow",
                                            e.what());
        }
    }

    unsigned int nsamplesAlloc = 0;
    _pollingChanged = true;

#if POLLING_METHOD == POLL_EPOLL_ET
    // When doing edge-triggered epoll, one must keep a list of
    // file descriptors which still have data available after their
    // last read, since epoll will not receive another POLLIN event
    // from them until their state transitions from not-POLLIN to
    // POLLIN. An Polled object stays on this list of leftovers until
    // a handlePollEvents() indicates all available data was read.
    // Do man epoll for more information.
    //
    // Polled objects whose last read did not consume all data available.
    list<Polled*> leftovers;
#endif

    for (;!isInterrupted();) {

        if (_pollingChanged)
            handlePollingChange();

#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT

#if POLLING_METHOD == POLL_EPOLL_ET
        // if there are leftover reads, set timeout to 0
        int pollTimeout = leftovers.empty() ? _sensorCheckIntervalMsecs : 0;
#else
        int pollTimeout = _sensorCheckIntervalMsecs;
#endif

#ifdef HAVE_EPOLL_PWAIT
        int nfd =::epoll_pwait(_epollfd, _events, _nevents, pollTimeout,&sigmask);
#else
        int nfd =::epoll_wait(_epollfd, _events, _nevents, pollTimeout);
#endif

        if (nfd <= 0) {      // poll error, including receipt of signal, or timeout
            if (nfd < 0) {
                if (errno == EINTR) continue;   // signal received, probably SIGUSR1
                n_u::IOException e("SensorHandler", "epoll_wait", errno);
                PLOG(("%s",e.what()));
                break;
            }
            // poll timeout
            rtime = n_u::getSystemTime();
            if (_sensorCheckIntervalMsecs > 0 && rtime > _sensorCheckTime)
                checkTimeouts(rtime);
            if (rtime > _sensorStatsTime) calcStatistics(rtime);
            continue;
        }

        rtime = n_u::getSystemTime();

        struct epoll_event* event = _events;
        for (int ifd = 0; ifd < nfd; ifd++,event++) {
            // Polled* pp = reinterpret_cast<Polled*>(event->data.ptr);
            Polled* pp = (Polled*)event->data.ptr;
#if POLLING_METHOD == POLL_EPOLL_ET
            if (!pp->handlePollEvents(event->events)) leftovers.push_back(pp);
#else
//            PolledDSMSensor* pDSMSensor = reinterpret_cast<PolledDSMSensor*>(pp);
//            assert(pp);
//            ILOG(("Current polled sensor: %s", pDSMSensor->getName().c_str()));
            pp->handlePollEvents(event->events);
#endif
        }

#if POLLING_METHOD == POLL_EPOLL_ET
        // read the file descriptors with leftover data just once before polling
        // again, so that one descriptor can't bogart the attention of this thread.
        // This will result in two reads of a high-traffic file descriptor after it
        // first transitions from not-EPOLLIN to EPOLLIN, and one read-per-poll
        // thereafter.
        if (!leftovers.empty()) {
            list<Polled*>::iterator li = leftovers.begin();
            for ( ; li != leftovers.end(); ) {
                Polled* pp = *li;
                if (pp->handlePollEvents(N_POLLIN)) li = leftovers.erase(li);
                else ++li;
            }
        }
#endif

#elif POLLING_METHOD == POLL_PSELECT

        fd_set rfdset = _rfdset;
        fd_set efdset = _efdset;
        int nfd = ::pselect(_nselect,&rfdset,NULL,&efdset,
                (_sensorCheckIntervalMsecs > 0 ? &_timeout : NULL),&sigmask);

        if (nfd <= 0) {      // select error, including receipt of signal, or timeout
            if (nfd < 0) {
                if (errno == EINTR) continue;   // signal received, probably SIGUSR1
                n_u::IOException e("SensorHandler", "pselect", errno);
                PLOG(("%s",e.what()));
                break;
            }
            // poll timeout, nfd==0
        }
        rtime = n_u::getSystemTime();

        unsigned int ifd;
        for (ifd = 0; nfd > 0 && ifd < _nfds; ifd++) {
            int fd = _fds[ifd];
            uint32_t events = 0;
            if (FD_ISSET(fd,&rfdset)) {
                events |= N_POLLIN;
                nfd--;
            }
            if (FD_ISSET(fd,&efdset)) {
                events |= N_POLLERR;
                nfd--;
            }
            if (events) {
                Polled* pp = _polled[ifd];
                pp->handlePollEvents(events);
            }
        }
#elif POLLING_METHOD == POLL_POLL

#ifdef HAVE_PPOLL
        int nfd = ::ppoll(_fds,_nfds,
                (_sensorCheckIntervalMsecs > 0 ? &_timeout : NULL),&sigmask);
#else
        int nfd = ::poll(_fds,_nfds, _sensorCheckIntervalMsecs);
#endif

        if (nfd <= 0) {      // poll error, including receipt of signal, or timeout
            if (nfd < 0) {
                if (errno == EINTR) continue;   // signal received, probably SIGUSR1
                n_u::IOException e("SensorHandler", "poll", errno);
                PLOG(("%s",e.what()));
                break;
            }
            // poll timeout, nfd==0
        }

        rtime = n_u::getSystemTime();

        struct pollfd* pfdp = _fds;
        for (unsigned int ifd = 0; nfd > 0 && ifd < _nfds; ifd++,pfdp++) {
            if (pfdp->revents) {
                Polled* pld = _polled[ifd];
                // convert revents to unsigned before casting to a uint32_t
                pld->handlePollEvents((unsigned short)pfdp->revents);
                nfd--;
            }
        }
#endif

        if (_sensorCheckIntervalMsecs > 0 && rtime > _sensorCheckTime)
            checkTimeouts(rtime);

        if (rtime > _sensorStatsTime) {
            calcStatistics(rtime);

            // watch for sample memory leaks
            unsigned int nsamp = 0;
            list<SamplePoolInterface*> pools =
                SamplePools::getInstance()->getPools();
            for (list<SamplePoolInterface*>::const_iterator pi =
                 pools.begin(); pi != pools.end(); ++pi) {
                SamplePoolInterface *pool = *pi;
                nsamp += pool->getNSamplesAlloc();
            }
            if (nsamp > 20 && nsamp > (nsamplesAlloc + nsamplesAlloc / 2)) {
                for (list<SamplePoolInterface*>::const_iterator pi =
                     pools.begin(); pi != pools.end(); ++pi) {
                    SamplePoolInterface *pool = *pi;
                    n_u::Logger::getInstance()->log(LOG_INFO,
                        "pool nsamples alloc=%d, nsamples out=%d",
                        pool->getNSamplesAlloc(), pool->getNSamplesOut());
                }
                nsamplesAlloc = nsamp;
            }
        }
    }                           // poll loop until interrupt

    if (_rserial) _rserial->close();

    _pollingMutex.lock();
    _acceptingOpens = false;
    _pollingMutex.unlock();

    if (_opener.isRunning()) _opener.interrupt();

    // _pendingSensorClosures and _newOpenedSensors will not be altered
    // after setting _acceptingOpens to false above.
    // _activeRserialConns will not be modified since _rserial has been closed.

    list<RemoteSerialConnection*>::iterator ci;
    for (ci = _activeRserialConns.begin(); ci != _activeRserialConns.end(); ++ci)
        _pendingRserialClosures.insert(*ci);

    int n = 0;

    list<PolledDSMSensor*>::const_iterator pi;
    for (pi = _polledSensors.begin(); pi != _polledSensors.end(); ++pi,n++)
        _pendingSensorClosures.insert(*pi);

    _pollingChanged = true;

    // There's a small chance of newly opened sensors. Close them.
    // These are not wrapped with a PolledDSMSensor, so
    // they aren't just added to pendingSensorClosures
    list<DSMSensor*>::const_iterator si;
    for (si = _newOpenedSensors.begin(); si != _newOpenedSensors.end(); ++si,n++) {
        DSMSensor* sensor = *si;
        try {
            sensor->close();
        }
        catch (const n_u::IOException& e) {
        }
    }
    _newOpenedSensors.clear();

    n_u::Logger::getInstance()->log(LOG_INFO,
            "SensorHandler finishing, closing remaining %d sensors ",n);

    handlePollingChange();

#ifdef USE_NOTIFY_PIPE
    _notifyPipe->close();
#endif

    return RUN_OK;
}

/*
 * Interrupt this polling thread.  The SensorOpener thread will
 * likewise be interrupted before the run method exits.
 */
void SensorHandler::interrupt()
{
    Thread::interrupt();
#ifdef USE_NOTIFY_PIPE
    _notifyPipe->notify();
#else
    kill(SIGUSR1);
#endif
}

/*
 * Join this thread and join the SensorOpener.
 */
int SensorHandler::join() throw(nidas::util::Exception)
{
    if (!_opener.isJoined())
         _opener.join();
    int res = Thread::join();
    return res;
}

/*
 * Called on startup to add a sensor to this handler.
 */
void SensorHandler::addSensor(DSMSensor * sensor)
{
    _pollingMutex.lock();
    _allSensors.push_back(sensor);
    _pollingMutex.unlock();
    _opener.openSensor(sensor);
}

/*
 * Called from the SensorOpener thread indicating a sensor is
 * opened and ready.
 */
void SensorHandler::sensorIsOpen(DSMSensor * sensor) throw()
{
    _pollingMutex.lock();
    if (_acceptingOpens) {
        _newOpenedSensors.push_back(sensor);
        _pollingChanged = true;
        _pollingMutex.unlock();

#ifdef USE_NOTIFY_PIPE
        _notifyPipe->notify();
#else
        kill(SIGUSR1);
#endif
    }
    else {
        _pollingMutex.unlock();
        // if not _acceptingOpens, do not add this sensor to
        // any pending list. Just close it and it will
        // be deleted in the destructor.
        try {
            sensor->close();
        }
        catch (const n_u::IOException &e) {
        }
    }
}

/*
 * Private method to schedule a DSMSensor to be closed and not reopened.
 */
void SensorHandler::scheduleClose(PolledDSMSensor* psensor) throw()
{
    _pollingMutex.lock();
    _pendingSensorClosures.insert(psensor);
    _pollingChanged = true;
    _pollingMutex.unlock();
#ifdef USE_NOTIFY_PIPE
    _notifyPipe->notify();
#else
    kill(SIGUSR1);
#endif
}

/*
 * Public method to schedule a DSMSensor to be closed and reopened.
 */
void SensorHandler::scheduleReopen(PolledDSMSensor* psensor) throw()
{
    _pollingMutex.lock();
    _pendingSensorReopens.insert(psensor);
    _pollingChanged = true;
    _pollingMutex.unlock();
#ifdef USE_NOTIFY_PIPE
    _notifyPipe->notify();
#else
    kill(SIGUSR1);
#endif
}

void SensorHandler::add(DSMSensor* sensor) throw()
{
    try {
        PolledDSMSensor* psensor = new PolledDSMSensor(sensor,this);

        _pollingMutex.lock();
        _openedSensors.push_back(sensor);
        _pollingMutex.unlock();

        _polledSensors.push_back(psensor);
        sensor->setSensorState(SENSOR_ACTIVE);
    }
    catch(const n_u::IOException & e) {
        PLOG(("%s: %s", sensor->getName().c_str(), e.what()));
    }
}

/*
 * Private method to close a PolledDSMSensor, remove it from lists and delete it.
 */
void SensorHandler::remove(PolledDSMSensor* psensor) throw()
{
    try {
        psensor->close();
    }
    catch(const n_u::IOException & e) {
        PLOG(("%s: %s", psensor->getName().c_str(), e.what()));
    }

    DSMSensor* sensor = psensor->getDSMSensor();

    _pollingMutex.lock();
    list<DSMSensor*>::iterator si;
    si = find(_openedSensors.begin(), _openedSensors.end(), sensor);
    assert(si != _openedSensors.end());
    _openedSensors.erase(si);
    _pollingMutex.unlock();

    list<PolledDSMSensor*>::iterator pi;
    pi = find(_polledSensors.begin(), _polledSensors.end(), psensor);
    assert(pi != _polledSensors.end());
    _polledSensors.erase(pi);

    delete psensor;
}

/*
 * Public method to add a RemoteSerial connection
 */
void SensorHandler::scheduleAdd(RemoteSerialConnection* conn) throw()
{
    _pollingMutex.lock();
    _newRserials.push_back(conn);
    _pollingChanged = true;
    _pollingMutex.unlock();
#ifdef USE_NOTIFY_PIPE
    _notifyPipe->notify();
#else
    kill(SIGUSR1);
#endif
}
/*
 * Schedule a RemoteSerialConnection to be closed.
 */
void SensorHandler::scheduleClose(RemoteSerialConnection * conn) throw()
{
    _pollingMutex.lock();
    _pendingRserialClosures.insert(conn);
    _pollingChanged = true;
    _pollingMutex.unlock();
#ifdef USE_NOTIFY_PIPE
    _notifyPipe->notify();
#else
    kill(SIGUSR1);
#endif
}

/*
 * Private method to add a RemoteSerial connection
 */
void SensorHandler::add(RemoteSerialConnection* conn) throw()
{
    try {
        conn->readSensorName();

        DSMSensor* sensor = 0;

        list<DSMSensor*>::const_iterator si;
        for (si = _allSensors.begin(); si != _allSensors.end(); ++si) {
            DSMSensor *snsr = *si;
            if (snsr->getDeviceName() == conn->getSensorName()) {
                sensor = snsr;
                break;
            }
        }

        if (sensor) {
            CharacterSensor* csensor = dynamic_cast<CharacterSensor*>(sensor);
            if(csensor) {
                conn->setSensor(csensor); // may throw n_u::IOException

                _pollingMutex.lock();
                _activeRserialConns.push_back(conn);
                _pollingMutex.unlock();

                NLOG(("added rserial connection for device %s",
                                                sensor->getDeviceName().c_str()));
                return;
            }
            ILOG(("%s is not a CharacterSensor",sensor->getName().c_str()));
        }
        conn->sensorNotFound();
    }
    catch(const n_u::IOException & e) {
        PLOG(("%s", e.what()));
    }
    try {
        conn->close();
    }
    catch(const n_u::IOException & e) {
        PLOG(("%s", e.what()));
    }
    delete conn;
}

/*
 * Private method to close a RemoteSerialConnection, remove it from lists and delete it.
 */
void SensorHandler::remove(RemoteSerialConnection* conn) throw()
{
    list<RemoteSerialConnection*>::iterator ci;

    _pollingMutex.lock();
    ci = find(_activeRserialConns.begin(), _activeRserialConns.end(),conn);

    if (ci != _activeRserialConns.end())
        _activeRserialConns.erase(ci);
    else
        WLOG(("%s",
            "SensorHandler::remove(RemoteSerialConnection*) couldn't find connection for %s",
                            conn->getSensorName().c_str()));
    _pollingMutex.unlock();

    try {
        conn->close();
    }
    catch(const n_u::IOException & e) {
        PLOG(("%s", e.what()));
    }
    delete conn;
}

void SensorHandler::setupTimeouts(int sensorCheckIntervalMsecs)
{
#if POLLING_METHOD == POLL_PSELECT || POLLING_METHOD == POLL_POLL
    if (sensorCheckIntervalMsecs > 0) {
        _timeout.tv_sec = sensorCheckIntervalMsecs / MSECS_PER_SEC;
        _timeout.tv_nsec = (sensorCheckIntervalMsecs % MSECS_PER_SEC) * NSECS_PER_MSEC;
    }
#endif
    // reading _openedSensors from my thread, no need for lock
    list<PolledDSMSensor*>::iterator pi;
    for (pi = _polledSensors.begin(); pi != _polledSensors.end(); ++pi ) {
        PolledDSMSensor* psensor = *pi;
        psensor->setupTimeouts(sensorCheckIntervalMsecs);
    }
}

void SensorHandler::handlePollingChange()
{
    _pollingMutex.lock();
    bool changed = _pollingChanged;
    _pollingChanged = false;
    _pollingMutex.unlock();

    if (changed) {

        _pollingMutex.lock();
        set<PolledDSMSensor*> tmpsensors = _pendingSensorClosures;
        _pendingSensorClosures.clear();
        _pollingMutex.unlock();

        set<PolledDSMSensor*>::const_iterator psi = tmpsensors.begin();
        for (; psi != tmpsensors.end(); ++psi) {
            PolledDSMSensor *psensor = *psi;
            remove(psensor);
        }

        _pollingMutex.lock();
        tmpsensors = _pendingSensorReopens;
        _pendingSensorReopens.clear();
        _pollingMutex.unlock();

        psi = tmpsensors.begin();
        for (; psi != tmpsensors.end(); ++psi) {
            PolledDSMSensor *psensor = *psi;
            DSMSensor* sensor = psensor->getDSMSensor();
            remove(psensor);
            if (_acceptingOpens)
            {
                // power off sensor before entering the reopen loop.  if the
                // sensor open starts failing because the device has gone
                // away, then no amount of power cycling will fix that.
                // instead, power it off once here, then it will be powered on
                // in the next open() call after the reopen delay.  if that
                // open() does not succeed and keeps failing, then at least
                // the sensor power is not being cycled every ten seconds.
                if (auto serial = dynamic_cast<SerialSensor*>(sensor)) {
                    serial->powerOff();
                }
                _opener.reopenSensor(sensor);
            }
        }

        _pollingMutex.lock();
        set<RemoteSerialConnection*> tmprserials = _pendingRserialClosures;
        _pendingRserialClosures.clear();
        _pollingMutex.unlock();

        set<RemoteSerialConnection*>::iterator csi =
            tmprserials.begin();
        for (; csi != tmprserials.end(); ++csi) {
            RemoteSerialConnection *conn = *csi;
            remove(conn);
        }

        _pollingMutex.lock();
        list<DSMSensor*> newsensors = _newOpenedSensors;
        _newOpenedSensors.clear();
        _pollingMutex.unlock();

        list<DSMSensor*>::const_iterator si = newsensors.begin();
        for ( ; si != newsensors.end(); ++si) {
            DSMSensor *sensor = *si;
            add(sensor);
        }

        _pollingMutex.lock();
        list<RemoteSerialConnection*> tmplrserials = _newRserials;
        _newRserials.clear();
        _pollingMutex.unlock();

        list<RemoteSerialConnection*>::const_iterator cli;
        cli = tmplrserials.begin();
        for ( ; cli != tmplrserials.end(); ++cli) {
            RemoteSerialConnection *conn = *cli;
            add(conn);
        }

        int minTimeoutMsecs = INT_MAX;

        int nsensors = 0;
#if POLLING_METHOD == POLL_PSELECT || POLLING_METHOD == POLL_POLL
        vector<int> fds;
        vector<Polled*> polled;
#endif

        // reading _openedSensors from my thread, no need for lock
        list<PolledDSMSensor*>::const_iterator pli = _polledSensors.begin();
        for ( ; pli != _polledSensors.end(); ++pli) {
            PolledDSMSensor *psensor = *pli;
            nsensors++;
            int sto = psensor->getTimeoutMsecs();
            // For now, don't check more than once a second
            if (sto > 0 && sto < minTimeoutMsecs)
                minTimeoutMsecs = std::max(sto,MSECS_PER_SEC);
#if POLLING_METHOD == POLL_PSELECT || POLLING_METHOD == POLL_POLL
            assert(psensor->getFd() >= 0);
            fds.push_back(psensor->getFd());
            polled.push_back(psensor);
#endif
        }

#if POLLING_METHOD == POLL_PSELECT || POLLING_METHOD == POLL_POLL
        for (cli = _activeRserialConns.begin(); cli != _activeRserialConns.end(); ++cli) {
            RemoteSerialConnection* conn = *cli;
            fds.push_back(conn->getFd());
            polled.push_back(conn);
        }
        if (_rserial && _rserial->getFd() >= 0) {
            fds.push_back(_rserial->getFd());
            polled.push_back(_rserial);
        }

#ifdef USE_NOTIFY_PIPE
        if (_notifyPipe->getFd() >= 0) {
            fds.push_back(_notifyPipe->getFd());
            polled.push_back(_notifyPipe);
        }
#endif

#endif
        if (minTimeoutMsecs < INT_MAX) {
            _sensorCheckIntervalMsecs = minTimeoutMsecs;
            _sensorCheckIntervalUsecs = _sensorCheckIntervalMsecs * USECS_PER_MSEC;
            _sensorCheckTime = n_u::timeCeiling(n_u::getSystemTime(), _sensorCheckIntervalUsecs);
        }
        else {
            _sensorCheckIntervalMsecs = -1;
            _sensorCheckIntervalUsecs = 0;
        }
        setupTimeouts(_sensorCheckIntervalMsecs);

        ILOG(("%d active sensors",nsensors));

#if POLLING_METHOD == POLL_EPOLL_ET || POLLING_METHOD == POLL_EPOLL_LT
        int nfds = 1 + nsensors;
        if (nfds > _nevents) {
            delete [] _events;
            _nevents = nfds + 5;
            _events = new epoll_event[_nevents];
        }
#elif POLLING_METHOD == POLL_PSELECT
        if (fds.size() > _nAllocFds) {
            _nAllocFds = fds.size() + 5;
            delete [] _fds;
            _fds = new int[_nAllocFds];

            delete [] _polled;
            _polled = new Polled*[_nAllocFds];

        }
        _nfds = fds.size();
        std::copy(fds.begin(),fds.end(),_fds);
        std::copy(polled.begin(),polled.end(),_polled);

        _nselect = 0;
        FD_ZERO(&_rfdset);
        FD_ZERO(&_efdset);
        for (unsigned int i = 0; i < _nfds; i++) {
            int fd = _fds[i];
            assert(fd >= 0 && fd < FD_SETSIZE);     // FD_SETSIZE=1024
            FD_SET(fd,&_rfdset);
            FD_SET(fd,&_efdset);
            if (fd >= _nselect) _nselect = fd + 1;
        }

        // DILOG(("_nselect=%d,_nfds=%d,_nAllocFds=%d", _nselect,_nfds,_nAllocFds));

#elif POLLING_METHOD == POLL_POLL
        if (fds.size() > _nAllocFds) {

            _nAllocFds = fds.size() + 5;
            delete [] _fds;
            _fds = new struct pollfd[_nAllocFds];

            delete [] _polled;
            _polled = new Polled*[_nAllocFds];

        }
        for (unsigned i = 0; i < fds.size(); i++) {
            _fds[i].fd = fds[i];
#ifdef POLLRDHUP
            _fds[i].events = POLLIN | POLLRDHUP;
#else
            _fds[i].events = POLLIN;
#endif
        }
        _nfds = fds.size();
        std::copy(polled.begin(),polled.end(),_polled);
#endif
    }
}
