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
/*
 * Read an XML configuration, find an instance of a DSC_A2DSensor,
 * which interfaces to a Diamond Systems Corporation Analog I/O module.
 * Open the sensor instance using the config in the XML, and read
 * samples back.  This is the start of a program for doing
 * calibrations on the A2D.
*/

#include <fstream>
#include <sys/stat.h>

#include <unistd.h>
#include <getopt.h>
#include <iomanip>

#include <nidas/util/Process.h>
#include <nidas/core/NidasApp.h>
#include <nidas/core/Project.h>
#include <nidas/core/XMLParser.h>
#include <nidas/core/DSMConfig.h>
#include <nidas/dynld/DSC_A2DSensor.h>
#include <nidas/dynld/DSC_AnalogOut.h>


using namespace nidas::core;
using namespace nidas::dynld;
using namespace std;

namespace n_u = nidas::util;

// const n_u::EndianConverter * littleEndian = n_u::EndianConverter::getConverter(n_u::EndianConverter::EC_LITTLE_ENDIAN);

class DmdA2dCk
{
public:

    DmdA2dCk();

    static int main(int argc, char** argv) throw();

    int parseRunstring(int argc, char** argv);

    int usage(const char* argv0);

    int run() throw();

private:
    NidasApp _app;

    /** AnalogOut device name. */
    string _aoutDev;

};

DmdA2dCk::DmdA2dCk():
    _app("data_stats"), _aoutDev()
{
    _app.setApplicationInstance();
    _app.setupSignals();
}

int main(int argc, char** argv)
{
    return DmdA2dCk::main(argc,argv);
}


int DmdA2dCk::parseRunstring(int argc, char** argv)
{

    NidasAppArg Aout("-a","device","name of analog out device",
            "/dev/dmmat_d2a0");
    _app.enableArguments(_app.XmlHeaderFile | _app.loggingArgs() |
                        _app.Version | _app.Help | Aout);

    try {
        ArgVector args = _app.parseArgs(argc, argv);
        if (_app.helpRequested())
        {
            return usage(argv[0]);
        }
        _app.parseInputs(args);
    }
    catch (NidasAppException& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }
    _aoutDev = Aout.getValue();
    return 0;
}

int DmdA2dCk::usage(const char* argv0)
{
    cerr <<
        "Usage: " << argv0 << " [options] ...\n";
    cerr <<
        "Standard options:\n"
         << _app.usage() <<
        "Example:\n" <<
        argv0 << " -x dmd.xml\n" << endl;
    return 1;
}

/* static */
int DmdA2dCk::main(int argc, char** argv) throw()
{
    DmdA2dCk a2dck;
    int result;
    if (! (result = a2dck.parseRunstring(argc, argv)))
    {
        try {
            result = a2dck.run();
        }
        catch (n_u::Exception& e)
        {
            cerr << e.what() << endl;
            XMLImplementation::terminate(); // ok to terminate() twice
            result = 1;
        }
    }
    return result;
}

class AutoProject
{
public:
    AutoProject() { Project::getInstance(); }
    ~AutoProject() { Project::destroyInstance(); }
};

int DmdA2dCk::run() throw()
{
    AutoProject aproject;
    DSC_A2DSensor *a2d = 0;
    DSC_AnalogOut aout;

    try
    {
	string xmlFileName = _app.xmlHeaderFile();

        xmlFileName = n_u::Process::expandEnvVars(xmlFileName);

        n_u::auto_ptr<xercesc::DOMDocument> doc(parseXMLConfigFile(xmlFileName));
        Project::getInstance()->fromDOMElement(doc->getDocumentElement());

        DSMConfigIterator di = Project::getInstance()->getDSMConfigIterator();
        const DSMConfig * dsm;

        for ( ; !a2d && di.hasNext(); )
        {
            dsm = di.next();
            const list<DSMSensor *>& sensors = dsm->getSensors();

            list<DSMSensor *>::const_iterator si;
            for (si = sensors.begin(); !a2d && si != sensors.end(); ++si)
            {
                a2d = dynamic_cast<DSC_A2DSensor *>((*si));
            }
        }

        if (!a2d) {
            cerr << "DSC_A2DSensor not found in XML" << endl;
            return 1;
        }

        cerr << "Found DSC_A2DSensor " << a2d->getDeviceName() <<
            " for dsm " << dsm->getName() << endl;
    }
    catch (XMLException& ioe)
    {
        cerr << ioe.what() << endl;
	return 1;
    }

    int result = 0;

    try {

        a2d->validate();
        a2d->open(O_RDONLY);
        a2d->init();

        // const Parameter * parm = p->sensor->getParameter("RESOLUTION");

        aout.setDeviceName(_aoutDev);
        aout.open();
        cerr << "num Analog outputs=" << aout.getNumOutputs() << endl;
        for (int i = 0; i < aout.getNumOutputs(); i++) {
            cerr << "min voltage " << i << " = " << aout.getMinVoltage(i) << endl;
            cerr << "max voltage " << i << " = " << aout.getMaxVoltage(i) << endl;
        }

        // read and discard all analog samples that are in the buffer
        while (!a2d->readSamples());

        // step through voltage range of outputNum output, by vstep.
        int outputNum = 0;
        float vstep = 0.5;
        for (float vout = aout.getMinVoltage(outputNum);
                vout <= aout.getMaxVoltage(outputNum);
                vout += vstep) {

            if (_app.interrupted()) break;
            aout.setVoltage(outputNum, vout);

            // read and discard ndiscard samples
            int ndiscard = 10;
            for (int i = 0; i < ndiscard; i++) {
                const Sample *samp = a2d->readSample();
                samp->freeReference();
            }

            int nsamp = 20;

            for (int isamp = 0; isamp < nsamp; isamp++) {
                if (_app.interrupted()) break;
                const Sample *samp = a2d->readSample();

                list<const Sample*> results;

                // process sample
                a2d->process(samp,results);

                // free raw sample
                samp->freeReference();

                // iterate over processed samples
                list<const Sample*>::const_iterator psi = results.begin();

                for ( ; psi != results.end(); ++psi) {
                    const Sample *psamp = *psi;
                    n_u::UTime tt(psamp->getTimeTag());
                    string tstr = tt.format(true,"%H:%M:%S.%3f");
                    cout << tstr << ' ';
                    int nval = psamp->getDataLength();

                    for (int i = 0; i < nval; i++) {
                        double val = psamp->getDataValue(i);
                        cout << val << ' ';
                    }
                    cout << endl;
                    // free processed sample
                    psamp->freeReference();
                }
            }   // read nsamp
        }   // step over output voltages
    }
    catch (n_u::InvalidParameterException& ipe) {
        cerr << ipe.what() << endl;
        result = 1;
    }
    catch (n_u::IOException& ioe) {
        cerr << ioe.what() << endl;
        result = 1;
    }

    try {
        a2d->close();
        aout.close();
    }
    catch (n_u::IOException& ioe)
    {
        cerr << ioe.what() << endl;
	result = 1;
    }
    return result;
}
