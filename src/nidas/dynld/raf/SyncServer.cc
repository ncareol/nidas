// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2005, Copyright University Corporation for Atmospheric Research
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

#include "SyncServer.h"

#include <ctime>

#include <nidas/core/FileSet.h>
#include <nidas/dynld/SampleOutputStream.h>
#include <nidas/core/SampleOutputRequestThread.h>
#include <nidas/core/XMLParser.h>
#include <nidas/core/DSMSensor.h>
#include <nidas/core/Project.h>
#include <nidas/util/Process.h>
#include <nidas/util/Logger.h>

#include <set>
#include <map>
#include <iostream>
#include <iomanip>

using namespace nidas::core;
using namespace nidas::dynld;
using namespace nidas::dynld::raf;
using namespace std;

namespace n_u = nidas::util;

using nidas::dynld::raf::SyncServer;

SyncServer::SyncServer():
    Thread("SyncServer"),
    _pipeline(), _syncGen(), 
    _inputStream(0), _outputStream(0),
    _xmlFileName(), _dataFileNames(),
    _address(new n_u::Inet4SocketAddress(DEFAULT_PORT)),
    _sorterLengthSecs(SORTER_LENGTH_SECS),
    _rawSorterLengthSecs(RAW_SORTER_LENGTH_SECS),
    _sampleClient(0),
    _stop_signal(0),
    _firstSample(0),
    _startTime(0),
    _startWindow(LONG_LONG_MIN),
    _endWindow(LONG_LONG_MAX)
{
}


SyncServer::
~SyncServer()
{
    // Make sure we destroy whatever we created.
    DLOG(("SyncServer: destructor"));

    // The SyncServer can be deleted after acquiring the first sample but
    // before distributing it, so make sure it gets released.
    if (_firstSample)
    {
        DLOG(("SyncServer: releasing _firstSample ") << _firstSample);
        _firstSample->freeReference();
        _firstSample = 0;
    }
    delete _stop_signal;
    delete _inputStream;
    delete _outputStream;
    _inputStream = 0;
    _outputStream = 0;
    _stop_signal = 0;
}


void
SyncServer::
initProject()
{
    DLOG(("SyncServer::initProject() starting."));
    std::string expxmlpath = n_u::Process::expandEnvVars(_xmlFileName);
    Project* project = Project::getInstance();
    project->parseXMLConfigFile(expxmlpath);
    project->setConfigName(_xmlFileName);
    DLOG(("SyncServer::initProject() finished."));
}


void
SyncServer::
initSensors(SampleInputStream& sis)
{
    set<DSMSensor*> sensors;
    Project* project = Project::getInstance();
    SensorIterator ti = project->getSensorIterator();
    for ( ; ti.hasNext(); ) {
        DSMSensor* sensor = ti.next();
        if (sensors.insert(sensor).second) {
            sis.addSampleTag(sensor->getRawSampleTag());
            sensors.insert(sensor);
            sensor->init();
        }
    }
}


int
SyncServer::
run() throw(n_u::Exception)
{
    DLOG(("SyncServer::run() started."));
    try {
        read();
        DLOG(("SyncServer::run() finished."));
        return 0;
    }
    catch (n_u::Exception& e) {
        stop();
        cerr << e.what() << endl;
        XMLImplementation::terminate(); // ok to terminate() twice
        DLOG(("SyncServer::run() caught exception."));
	return 1;
    }
}


void
SyncServer::
interrupt()
{
    Thread::interrupt();
    _pipeline.interrupt();
}


void
SyncServer::
stop()
{
    // This should not throw any exceptions since it gets called from
    // within exception handlers.
    _inputStream->close();
    _syncGen.disconnect(_pipeline.getProcessedSampleSource());
    if (_outputStream)
        _syncGen.disconnect(_outputStream);
    _pipeline.interrupt();
    _pipeline.join();
    SampleOutputRequestThread::destroyInstance();
    // Can't do this here since there may be other objects (eg
    // SyncRecordReader) still holding onto samples.  Programs which want
    // to do this cleanup (like sync_server) need to do it on their own
    // once all the Sample users are done.
    //
    //    SamplePools::deleteInstance();
    delete _inputStream;
    delete _outputStream;
    _inputStream = 0;
    _outputStream = 0;
    if (_stop_signal)
    {
        _stop_signal->stop();
        delete _stop_signal;
        _stop_signal = 0;
    }
}


void
SyncServer::
openStream()
{
    DLOG(("SyncServer::openStream()"));
    IOChannel* iochan = 0;

    nidas::core::FileSet* fset =
        nidas::core::FileSet::getFileSet(_dataFileNames);

    iochan = fset->connect();

    // RawSampleStream owns the iochan ptr.
    _inputStream = new RawSampleInputStream(iochan);
    RawSampleInputStream& sis = *_inputStream;

    // Apply some sample filters in case the file is corrupted.
    sis.setMaxDsmId(2000);
    sis.setMaxSampleLength(64000);
    sis.setMinSampleTime(n_u::UTime::parse(true,"2006 jan 1 00:00"));
    sis.setMaxSampleTime(n_u::UTime::parse(true,"2020 jan 1 00:00"));

    sis.readInputHeader();
    SampleInputHeader header = sis.getInputHeader();
    if (_xmlFileName.length() == 0)
        _xmlFileName = header.getConfigName();

    // Read the very first sample from the stream, before any clients are
    // connected, just to get the start time.  It will be distributed after
    // the clients are connected.
    _firstSample = sis.readSample();
    _startTime = _firstSample->getTimeTag();
    DLOG(("SyncServer: first sample ") << _firstSample 
         << " at time " << n_u::UTime(_startTime).format());
    DLOG(("SyncServer::openStream() finished."));
}


void
SyncServer::
sendSyncHeader()
{
    DLOG(("SyncServer::sendHeader()"));
    SyncRecordSource* syncsource = _syncGen.getSyncRecordSource();
    // Trigger the sync header in the SyncRecordSource.  The time tag for
    // the header should have already been set by the call to
    // SyncRecordGenerator::init().
    syncsource->sendSyncHeader();
}


void
SyncServer::
init() throw(n_u::Exception)
{
    DLOG(("SyncServer::init() starting."));
    openStream();
    initProject();
    initSensors(*_inputStream);

    _pipeline.setRealTime(false);
    _pipeline.setRawSorterLength(_rawSorterLengthSecs);
    _pipeline.setProcSorterLength(_sorterLengthSecs);
	
    // Even though the time length of the raw sorter is typically
    // much smaller than the length of the processed sample sorter,
    // (currently 1 second vs 900 seconds) its heap size needs to be
    // proportionally larger since the raw samples include the fast
    // 2DC data, and the processed 2DC samples are much smaller.
    // Note that if more memory than this is needed to sort samples
    // over the length of the sorter, then the heap is dynamically
    // increased. There isn't much penalty in choosing too small of
    // a value.
    _pipeline.setRawHeapMax(50 * 1000 * 1000);
    _pipeline.setProcHeapMax(100 * 1000 * 1000);
    _pipeline.connect(_inputStream);

    _syncGen.connect(_pipeline.getProcessedSampleSource());

    // The SyncRecordGenerator::init() method pre-loads calibrations into
    // the project variable converters, but it must be called *after* the
    // SyncRecordGenerator has been connected to a source, since that's
    // when SyncRecordSource traverses its variables.
    _syncGen.init(_startTime);

    // SyncRecordGenerator is now connected to the pipeline output, all
    // that remains is connecting the output of the generator.  By default
    // the output goes to a socket, but that will be disabled if another
    // SampleClient instance (ie SyncRecordReader) has been specified
    // instead.
    if (! _sampleClient)
    {
        nidas::core::ServerSocket* servSock = 
            new nidas::core::ServerSocket(*_address.get());
        IOChannel* ioc = servSock->connect();
        if (ioc != servSock) {
            servSock->close();
            delete servSock;
        }

        // The SyncServer is a SampleConnectionRequester client of the
        // output stream, so it will be notified when the output is closed
        // and can shut down cleanly.
        _outputStream = new SampleOutputStream(ioc, this);
        SampleOutputStream& output = *_outputStream;

        // don't try to reconnect. On an error in the output socket
        // writes will cease, but this process will keep reading samples.
        output.setReconnectDelaySecs(-1);
        _syncGen.connect(&output);
    }
    else
    {
        _syncGen.addSampleClient(_sampleClient);
    }
    DLOG(("SyncServer::init() finished."));
}


void
SyncServer::
setTimeWindow(nidas::util::UTime start, nidas::util::UTime end)
{
    _startWindow = start.toUsecs();
    _endWindow = end.toUsecs();
}


void
SyncServer::
handleSample(Sample* sample)
{
    RawSampleInputStream& sis = *_inputStream;

    // Either send this sample on or forget about it.
    dsm_time_t st = sample->getTimeTag();
    if (st >= _startWindow && st <= _endWindow)
    {
        sis.distribute(sample);
    }
    else
    {
        sample->freeReference();
    }
}


void
SyncServer::
read(bool once) throw(n_u::IOException)
{
    DLOG(("SyncServer::read() entered."));
    bool eof = false;
    RawSampleInputStream& sis = *_inputStream;

    if (_firstSample)
    {
        DLOG(("SyncServer: handling firstSample"));
        handleSample(_firstSample);
        _firstSample = 0;
    }
    try {
        while (!once)
        {
            if (isInterrupted()) break;
            // readSample() either returns a sample or throws an exception,
            // but test the returned pointer just to be safe.
            Sample* sample = sis.readSample();
            if (!sample)
            {
                eof = true;
                break;
            }
            handleSample(sample);
        }
    }
    catch (n_u::EOFException& xceof)
    {
        eof = true;
        cerr << xceof.what() << endl;
    }
    catch (n_u::IOException& ioe)
    {
        stop();
        throw(ioe);
    }
    // In the normal eof case, flush the sorter pipeline.
    if (eof)
    {
        _pipeline.flush();
        stop();
    }
}


void SyncServer::connect(SampleOutput*) throw()
{
}

void SyncServer::disconnect(SampleOutput*) throw()
{
    this->interrupt();
}

