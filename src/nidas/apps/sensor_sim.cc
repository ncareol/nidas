/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
/* -*- mode: c++; c-basic-offset: 4; -*- */
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

#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <cstring>

#include <nidas/core/Looper.h>
#include <nidas/core/NidasApp.h>

#include <nidas/core/CharacterSensor.h>
#include <nidas/util/SerialPort.h>
#include <nidas/util/SerialOptions.h>
#include <nidas/util/auto_ptr.h>

using namespace std;
using namespace nidas::core;
using std::cin;

namespace n_u = nidas::util;

enum sens_type
{
    CSAT3,
    FROM_FILE,
    FROM_BINARY_FILE,
    FIXED,
    ISS_CAMPBELL,
    UNKNOWN
};

enum sep_type
{
    EOM_SEPARATOR,
    BOM_SEPARATOR,
    UNK_SEPARATOR,
};

/**
 * Base class for sensor simulators.
 */
class SensorSimulator: public LooperClient
{
public:
    SensorSimulator(n_u::SerialPort* p, bool prompted, string prompt,
                    float rate, int nmessages):
        _port(p),
        _prompted(prompted),
        _prompt(prompt),
        _rate(rate),
        _nmessages(nmessages),
        _interrupted(false)
    {
    }

    /**
     * Implement this to send a message from the simulated sensor.
     *
     * @throws nidas::util::IOException
     */
    virtual void sendMessage() = 0;

    /**
     * Subclasses generate the message to send, then call writeMessage()
     * to send it out.
     */
    virtual void
    writeMessage(const std::string& msg)
    {
        _port->write(msg.c_str(), msg.length());
    }

    /** overloaded function useful for writing binary data */
    virtual void
    writeMessage(const char* buf, std::streamsize l)
    {
        _port->write(buf, l);
    }

    /**
     * Default implementation of run will call the sendMessage() method
     * either after receipt of a prompt or at the given rate if
     * the sensor is not prompted.  run() will return when
     * isInterrupted() is true.
     */
    virtual void run();

    /**
     * Stop the simulation.
     */
    void
    interrupt()
    {
        _interrupted = true;
    }

    bool
    isInterrupted() const
    {
        return _interrupted;
    }

    n_u::SerialPort*
    port()
    {
        return _port;
    }

protected:
    Looper* getLooper();

    void looperNotify() override;
    void readPrompts();

    n_u::SerialPort* _port;
    bool _prompted;
    string _prompt;
    float _rate;
    int _nmessages;
    bool _interrupted;

    static Looper* _looper;

    SensorSimulator(const SensorSimulator&);
    SensorSimulator& operator=(const SensorSimulator&);
};

Looper* SensorSimulator::_looper = 0;

Looper*
SensorSimulator::getLooper()
{
    if (!_looper)
        _looper = new Looper();
    return _looper;
}

void
SensorSimulator::looperNotify()
{
    if (_interrupted)
    {
        _looper->removeClient(this);
        _looper->interrupt();
        return;
    }
    try
    {
        if (_nmessages >= 0 && _nmessages-- == 0)
            interrupt();
        else
            sendMessage();
    }
    catch (n_u::IOException& e)
    {
        cerr << e.what() << endl;
    }
}

void
SensorSimulator::readPrompts()
{
    const char* sop = _prompt.c_str();
    const char* eop = sop + _prompt.length();
    const char* pp = sop;
    for (;;)
    {
        if (isInterrupted())
            break;
        char c = _port->readchar();
        if (c == *pp)
        {
            if (++pp == eop)
            {
                if (_nmessages >= 0 && _nmessages-- == 0)
                    interrupt();
                else
                    sendMessage();
                pp = sop;
            }
        }
        else if (c == *(pp = sop))
            pp++;
        else
            cerr << "unrecognized prompt char: \"" << c << "\"" << endl;
    }
}

void
SensorSimulator::run()
{
    if (_prompted)
    {
        readPrompts();
    }
    else
    {
        unsigned long msecPeriod = (unsigned long)rint(MSECS_PER_SEC / _rate);
        // cerr << "msecPeriod=" << msecPeriod << endl;

        getLooper();
        _looper->addClient(this, msecPeriod, 0);
        _looper->join();
    }
}

/**
 * Send a fixed message at a given rate or after a prompt.
 */
class FixedSim: public SensorSimulator
{
public:
    FixedSim(n_u::SerialPort* p, const string& m, enum sep_type septype,
             string sep, bool prompted, string prompt, float rate,
             int nmessages);
    void sendMessage();

private:
    string _msg;
    enum sep_type _septype;
    string _separator;
};

FixedSim::FixedSim(n_u::SerialPort* p, const string& msg,
                   enum sep_type septype, string sep, bool prompted,
                   string prompt, float rate, int nmessages):
    SensorSimulator(p, prompted, prompt, rate, nmessages),
    _msg(),
    _septype(septype),
    _separator(sep)
{
    switch (_septype)
    {
    case BOM_SEPARATOR:
        _msg = _separator + msg;
        break;
    case EOM_SEPARATOR:
        _msg = msg + _separator;
        break;
    default:
        break;
    }
}

void
FixedSim::sendMessage()
{
    writeMessage(_msg);
}

/**
 * Read serial records from a file and send them at a
 * given rate or after a prompt.
 **/
class FileSim: public SensorSimulator
{
public:
    FileSim(n_u::SerialPort* p, const string& path, enum sep_type septype,
            string separator, bool prompted, string prompt, float rate,
            int nmessages, bool once, bool verbose = false):
        SensorSimulator(p, prompted, prompt, rate, nmessages),
        _path(path),
        _infile(),
        _in(0),
        _msg(),
        _septype(septype),
        _separator(separator),
        _reopen(false),
        _onceThru(once),
        _verbose(verbose),
        _binary(false)
    {
        open();
    }

    FileSim(n_u::SerialPort* p, const string& path, float rate, bool once,
            bool verbose = false):
        SensorSimulator(p, false, "", rate, -1),
        _path(path),
        _infile(),
        _in(0),
        _msg(),
        _septype(UNK_SEPARATOR),
        _separator(""),
        _reopen(false),
        _onceThru(once),
        _verbose(verbose),
        _binary(true)
    {
        open();
    }

    void
    open()
    {
        close();
        if (_verbose)
            std::cerr << "opening " << _path << " ...\n";
        if (_path.length() == 0)
        {
            throw n_u::Exception("FileSim requires an input file.");
        }
        if (_path == "-")
        {
            _in = &std::cin;
        }
        else
        {
            // Enable exceptions to get at any failure messages on open,
            // then disable them again.  If opening ever fails, disable
            // reopen flag so we don't keep attempting it.
            try
            {
                _infile.clear();
                _infile.exceptions(ios::failbit | ios::badbit);
                _infile.open(_path.c_str());
                _infile.exceptions(std::ios::goodbit);
                _in = &_infile;
                _reopen = true;
            }
            catch (const std::exception& failure)
            {
                _reopen = false;
                throw n_u::IOException(_path, "open", errno);
            }
        }
        if (_verbose)
            std::cerr << _path << " opened.\n";
    }

    void
    close()
    {
        _in = 0;
        _infile.close();
    }

    void
    rewind()
    {
        // Implement rewind with a seek rather than re-opening.
        if (_reopen)
        {
            if (_verbose)
                std::cerr << "FileSim: rewinding " << _path << std::endl;
            _infile.clear();
            _infile.seekg(0);
            return;
        }
        close();
        if (_reopen)
            try
            {
                if (_verbose)
                    std::cerr << "FileSim: re-opening input file " << _path
                              << std::endl;
                open();
            }
            catch (n_u::Exception& e)
            {
                std::cerr << "FileSim: exception on file input: " << e.what()
                          << std::endl;
            }
    }

    void sendMessage();
    void sendASCIIMessage();
    void sendBinaryMessage();

private:
    string _path;
    std::ifstream _infile;
    std::istream* _in;
    std::string _msg;
    enum sep_type _septype;
    string _separator;
    bool _reopen;
    bool _onceThru;
    bool _verbose;
    bool _binary;
    FileSim(const FileSim&);
    FileSim& operator=(const FileSim&);
};

void
FileSim::sendMessage()
{
    if (_binary)
        sendBinaryMessage();
    else
        sendASCIIMessage();
}

void
FileSim::sendBinaryMessage()
{
    // Grab the next chunk of input.
    char buf[128];

    if (_in && !_in->read(buf, sizeof(buf)))
    {
        interrupt();
        return;
    }
    std::streamsize l = _in->gcount();
    writeMessage(buf, l);
}

void
FileSim::sendASCIIMessage()
{
    // Grab the next line from input.  If the standard input has finished,
    // repeat the last message forever, otherwise loop over the file.
    string msg;

    if (_in && !std::getline(*_in, msg))
    {
        if (_onceThru)
        {
            interrupt();
            return;
        }
        rewind();
        if (_in && !std::getline(*_in, msg))
        {
            // empty file, quit trying
            std::cerr << "FileSim: file is empty, reopens disabled.\n";
            close();
            _reopen = false;
        }
    }
    // if file is still open, then we read a new message
    if (_in)
    {
        switch (_septype)
        {
        case BOM_SEPARATOR:
            msg = _separator + msg;
            break;
        case EOM_SEPARATOR:
            msg = msg + _separator;
            break;
        default:
            break;
        }
        n_u::replaceBackslashSequences(msg);
        _msg = msg;
    }
    if (_verbose)
        std::cout << _msg;
    writeMessage(_msg);
}

/**
 * Simulate a CSAT3 sonic.
 */
class Csat3Sim: public SensorSimulator
{
public:
    Csat3Sim(n_u::SerialPort* p, float rate, int nmessages):
        SensorSimulator(p, false, "", rate, nmessages),
        _cntr(0)
    {
    }
    void run();
    void sendMessage();

private:
    int _cntr;
};

void
Csat3Sim::sendMessage()
{
    if (_cntr % 64 == 99)
    {
        // every once in a while send a bunch of junk
        unsigned char outbuf[8192];
        unsigned int len = sizeof(outbuf) / sizeof(outbuf[0]);
        for (unsigned int i = 0; i < len; i++)
            outbuf[i] = i % 0xff;
        _port->write((const char*)outbuf, len);
    }
    else
    {
        unsigned char outbuf[] = { 0, 4, 0,    4,    0,    4,
                                   0, 4, 0x40, 0x05, 0x55, 0xaa };
        outbuf[8] = (outbuf[8] & 0xc0) + (_cntr & 0x3f);
        _port->write((const char*)outbuf, 12);
    }
    _cntr++;
}

void
Csat3Sim::run()
{
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(_port->getFd(), &rfds);
    int nfds = _port->getFd() + 1;
    struct timeval timeout = { 0, (int)rint(USECS_PER_SEC / _rate) };
    bool running = false;
    bool datamode = false;
    int nquest = 0;

    _cntr = 0;

    for (;;)
    {
        fd_set rtfds = rfds;
        struct timeval tval = timeout;

        int res = ::select(nfds, &rtfds, 0, 0, &tval);
        if (res < 0)
            throw n_u::IOException(_port->getName(), "select", errno);
        if (res == 0)
        {
            if (running && datamode)
            {
                if (_cntr == _nmessages)
                    break;
                sendMessage();
            }
        }
        else if (FD_ISSET(_port->getFd(), &rtfds))
        {
            res--;
            char buf[8];
            int nc = _port->read(buf, sizeof(buf));
            const char* eob = buf + nc;
            for (const char* cp = buf; cp < eob; cp++)
            {
                switch (*cp)
                {
                case '&':
                    running = !running;
                    // cerr << "running=" << running << endl;
                    nquest = 0;
                    break;
                case 'D':
                    datamode = true;
                    nquest = 0;
                    break;
                case 'P':
                    break;
                case 'T':
                    datamode = false;
                    nquest = 0;
                    break;
                case '\r':
                    if (nquest == 2 && !datamode)
                    {
                        const char* outmsg = "\
ET= 60 ts=i XD=d GN=434a TK=1 UP=5 FK=0 RN=1 IT=1 DR=102 rx=2 fx=038 BX=0 AH=1  AT=0 RS=1 BR=0 RI=1 GO=00000 HA=0 6X=3 3X=2 PD=2 SD=0 ?d sa=1\r\
WM=o ar=0 ZZ=0 DC=1  ELo=010 010 010 ELb=010 010 010 TNo=99b d TNb=97a JD= 007\r\
C0o=-2-2-2 C0b=-2-2-2 RC=0 tlo=8 8 8 tlb=8 8 8 DTR=01740 CA=1 TD=  duty=086     AQ= 60 AC=1 CD=0 SR=1 UX=0 MX=0 DTU=02320 DTC=01160 RD=o ss=1 XP=2 RF=018 DS=007 SN0367 28may04 HF=005 JC=3 CB=3 MD=5 DF=05000 RNA=1 rev 3.0a cs=29072 &=0 os= \r>";
                        _port->write(outmsg, strlen(outmsg));
                    }
                    nquest = 0;
                    break;
                case '?':
                    nquest++;
                    break;
                default:
                    cerr << "Unknown character received: 0x" << hex
                         << (int)(unsigned char)*cp << dec << endl;
                    break;
                }
            }
        }
    }
}

class SensorSimApp
{
public:
    SensorSimApp() = default;
    int parseRunstring(int argc, char** argv);
    static int usage(const char* argv0);
    int main();

private:
    string _device {};
    enum sens_type _type {UNKNOWN};
    enum sep_type _septype {EOM_SEPARATOR};
    string _outputMessage {};
    string _separator {"\r\n"};
    bool _prompted {false};
    string _prompt {};
    bool _openpty {false};
    bool _verbose {false};
    float _rate {1.0};
    int _nmessages {-1};
    string _fixedMessage {};
    string _inputFile {};
    bool _onceThru {false};
    static string defaultTermioOpts;
    string _termioOpts {SensorSimApp::defaultTermioOpts};
    bool _continue {false};
    int _timeout {0};
    bool _wait_for_hup {false};
};

/* static */
string SensorSimApp::defaultTermioOpts = "9600n81lnr";


void
handle_alarm(int signum)
{
    if (signum == SIGALRM)
    {
        cerr << "timeout reached, exiting." << endl;
        exit(9);
    }
}



int
SensorSimApp::parseRunstring(int argc, char** argv)
{
    extern char* optarg; /* set by getopt() */
    extern int optind;   /* "  "     "     */
    int opt_char;        /* option character */

    while ((opt_char = getopt(argc, argv,
                              "b:B:ce:f:F:igm:n:o:p:r:tvCa:H")) != -1)
    {
        switch (opt_char)
        {
        case 'b':
            _septype = BOM_SEPARATOR;
            _separator = n_u::replaceBackslashSequences(optarg);
            break;
        case 'B':
            _type = FROM_BINARY_FILE;
            _inputFile = optarg;
            _onceThru = true;
            break;
        case 'c':
            _type = CSAT3;
            _termioOpts = "9600n81lnr";
            break;
        case 'e':
            _septype = EOM_SEPARATOR;
            _separator = n_u::replaceBackslashSequences(optarg);
            break;
        case 'f':
            _type = FROM_FILE;
            _inputFile = optarg;
            _onceThru = true;
            break;
        case 'F':
            _type = FROM_FILE;
            _inputFile = optarg;
            _onceThru = false;
            break;
        case 'i':
            _type = ISS_CAMPBELL;
            _termioOpts = "9600n81lnr";
            break;
        case 'm':
            _type = FIXED;
            _outputMessage = optarg;
            break;
        case 'n':
            _nmessages = atoi(optarg);
            break;
        case 'o':
            _termioOpts = optarg;
            break;
        case 'p':
            _prompted = true;
            _prompt = n_u::replaceBackslashSequences(optarg);
            break;
        case 'r':
            _rate = atof(optarg);
            break;
        case 't':
            _openpty = true;
            break;
        case 'v':
            _verbose = true;
            break;
        case 'C':
            _continue = true;
            break;
        case 'a':
            _timeout = atoi(optarg);
            break;
        case 'H':
            _wait_for_hup = true;
            break;
        case '?':
            return usage(argv[0]);
        }
    }
    if (optind == argc - 1)
        _device = string(argv[optind++]);
    if (_device.length() == 0)
        return usage(argv[0]);
    if (_type == UNKNOWN)
        return usage(argv[0]);
    if (optind != argc)
        return usage(argv[0]);
    return 0;
}

int
SensorSimApp::usage(const char* argv0)
{
    cerr << "\
Usage: " << argv0 << " [options ...] device\n"
         << "\
  -b sep: send separator at beginning of message\n\
    separator can contain backslash sequences, like \\r, \\n or \\xhh,\n\
    where hh are two hex digits\n\
  -B file_input: binary input file of simulated sensor data.\n\
     Reads 128 byte chunks of file and sends it verbatim at the\n\
     rate specified with -r.\n\
     Read standard input if file_input is '-'. Read until EOF.\n\
     After opening the device,\n\
     " << argv0
         << " will do a kill -STOP on itself\n\
     before sending any messages.  Do \"kill -CONT %1\" from the\n\
     shell to resume execution, or see -C option.\n\
  -c: simulate CSAT3 sonic anemometer (9600n81lnr, unprompted)\n\
  -e sep: send separator at end of message. Default record separator\n\
    option is \"-e \\n\"\n\
  -f file_input: input file of simulated sensor data.\n\
     Read standard input if file_input is '-'. Read until EOF.\n\
     Newlines in the file are replaced by the -b or -e option strings\n\
     before being sent. After opening the device\n\
     " << argv0
         << " will do a kill -STOP on itself\n\
     before sending any messages.  Do \"kill -CONT %1\" from the\n\
     shell to resume execution, or see -C option.\n\
  -F file_input: Like -f, but loop over the file until -n n messages\n\
    have been sent.  If the file is the standard input,\n\
    repeat the last message. Newlines in the file are replaced by the\n\
    -b or -e option strings before being sent.\n\
  -i: simulate ISS Campbell (9600n81lnr,unprompted)\n\
  -m msg:  send a fixed output message at the specified rate\n\
    Use -b or -e option to add a separator\n\
  -n n: number of messages to output, default is send till ctrl-C\n\
    If n is > 0, then "
         << argv0 << " will do a kill -STOP on itself\n\
    after opening the device, before sending any messages.\n\
    Do \"kill -CONT %1\" from the shell to resume execution\n\
  -o termio_opts, see below. Default is "
         << defaultTermioOpts << "\n\
  -p prompt: read given prompt string on serial port before sending data record\n\
  -r rate: generate data at given rate, in Hz (for unprompted sensor)\n\
  -C: continue immediately rather than waiting for the CONT signal\n\
  -H: continue when HUP clears rather than waiting for CONT signal\n\
  -v: Verbose mode.  Echo simulated output and other messages.\n\
  -t: create pseudo-terminal device instead of opening serial device\n\
  -a seconds: set a timeout in seconds to exit with alarm()\n\
  device: Name of serial device or pseudo-terminal, e.g. /dev/ttyS1, or /tmp/pty/dev0\n\n\
" << n_u::SerialOptions::usage()
         << "\n\
" << endl;
    return 1;
}

int
SensorSimApp::main()
{
    try
    {
        n_u::auto_ptr<n_u::SerialPort> port;
        n_u::auto_ptr<SensorSimulator> sim;

        if (_openpty)
        {
            int fd = n_u::SerialPort::createPty(_wait_for_hup);
            n_u::SerialPort::createLinkToPty(_device, fd);
            port.reset(new n_u::SerialPort("/dev/ptmx", fd));
        }
        else
        {
            port.reset(new n_u::SerialPort(_device));
            port->open(O_RDWR);
        }

        n_u::SerialOptions options;
        options.parse(_termioOpts);

        port->termios() = options.getTermios();
        port->applyTermios();

        switch (_type)
        {
        case CSAT3:
            sim.reset(new Csat3Sim(port.get(), _rate, _nmessages));
            break;
        case FROM_FILE:
            sim.reset(new FileSim(port.get(), _inputFile, _septype, _separator,
                                  _prompted, _prompt, _rate, _nmessages,
                                  _onceThru, _verbose));
            break;
        case FROM_BINARY_FILE:
            sim.reset(new FileSim(port.get(), _inputFile, _rate, _onceThru,
                                  _verbose));
            break;
        case FIXED:
            sim.reset(new FixedSim(port.get(), _outputMessage, _septype,
                                   _separator, _prompted, _prompt, _rate,
                                   _nmessages));
            break;
        case ISS_CAMPBELL:
            sim.reset(new FileSim(port.get(), _inputFile, _septype, _separator,
                                  _prompted, _prompt, _rate, _nmessages,
                                  _onceThru, _verbose));
            break;
        case UNKNOWN:
            return 1;
        }

        if (!_openpty)
            port->open(O_RDWR);

        // After terminal is opened, STOP and wait for instructions...
        if (!_continue && (_nmessages >= 0 || _onceThru))
        {
            if (_wait_for_hup)
            {
                // do not continue until HUP clears.
                if (_verbose)
                    cerr << "waiting for pts to be opened ..." << endl;
                int timeout = (_timeout != 0 ? _timeout : -1);
                bool opened = n_u::SerialPort::waitForOpen(port->getFd(),
                                                           timeout);
                if (!opened)
                {
                    cerr << "timeout waiting for HUP to clear." << endl;
                    exit(1);
                }
            }
            else
            {
                if (_verbose)
                    cerr << "waiting for kill -CONT %1 ..." << endl;
                kill(getpid(), SIGSTOP);
            }
        }

        if (_timeout > 0)
        {
            NidasApp::addSignal(SIGALRM, handle_alarm);
            alarm(_timeout);
        }

        sim->run();

        port->drain();
        if (_openpty)
        {
            cerr << "sleeping for 5 seconds before closing pty..." << endl;
            sleep(5);
        }
        port->close();
    }
    catch (n_u::Exception& ex)
    {
        cerr << ex.what() << endl;
        if (_openpty)
            ::unlink(_device.c_str());
        return 1;
    }
    if (_openpty)
        ::unlink(_device.c_str());
    return 0;
}

int
main(int argc, char** argv)
{
    SensorSimApp sim;
    int res;
    if ((res = sim.parseRunstring(argc, argv)) != 0)
        return res;
    return sim.main();
}
