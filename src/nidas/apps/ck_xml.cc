/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
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

#include <nidas/core/XMLParser.h>
#include <nidas/core/Project.h>
#include <nidas/core/Site.h>
#include <nidas/core/DSMConfig.h>
#include <nidas/core/DSMSensor.h>
#include <nidas/core/SampleTag.h>
#include <nidas/core/Variable.h>
#include <nidas/core/VariableConverter.h>
#include <nidas/core/CalFile.h>
#include <nidas/core/NidasApp.h>
#include <nidas/core/Socket.h>
#include <nidas/core/SocketAddrs.h>
#include <nidas/core/XMLException.h>
// #include <nidas/dynld/RawSampleInputStream.h>
#include <nidas/core/requestXMLConfig.h>

#include <nidas/dynld/A2DSensor.h>

#include <iostream>
#include <iomanip>
#include <list>

#include <unistd.h>
#include <stdlib.h>

using namespace nidas::core;
using namespace std;
// using namespace xercesc;

namespace n_u = nidas::util;

using nidas::util::LogScheme;
using nidas::util::Logger;
using nidas::util::LogConfig;

typedef std::map<std::string, const Variable*> variable_map_t;

class PConfig
{
public:
    PConfig():
        _xmlFile(),
        _sensorClasses(),
        _showCalFiles(false),
        _showHosts(false),
        _showVariables(false),
        _variables()
    {}

    int parseRunstring(NidasApp& app, int argc, char** argv);
    void usage(const char* argv0);

    bool
    parseRemoteSpecifier(const std::string& xmlspec,
                         std::string& host, int& port);

    void
    loadFile(const std::string& xmlfile, Project& project);

    void
    loadRemoteXML(const std::string& host, int port, Project& project);

    int main();

    void showAll(const Project& project);
    void showSensorClasses(const Project& project);
    void showCalFiles(const Project& project);

    void
    showHostNames(const Project& project);

    void
    showVariables();

    void
    loadVariables(const Project& project);

    void
    getHostNames(const Project& project, std::vector<std::string>& dsmnames);

    void
    resolveCalFile(CalFile* cf);

private:
    string _xmlFile;

    list<string> _sensorClasses;

    bool _showCalFiles;
    bool _showHosts;
    bool _showVariables;

    variable_map_t _variables;
};


int
PConfig::
parseRunstring(NidasApp& app, int argc, char** argv)
{
    NidasAppArg ShowHosts("-d", "",
                          "List the DSM names (host names)");
    NidasAppArg ShowCalFiles
        ("-c", "",
         "Display a listing of all cal files referenced in the xml");
    NidasAppArg ShowSensors
        ("-s", "<sensorclassname>",
         "Display dsm and sensor id for sensors of the given class");
    NidasAppArg ShowVariables
        ("--variables", "",
         "List all variable names and their rates."); 
    
    app.enableArguments(app.loggingArgs() |
                        app.Version | app.Help |
                        ShowHosts | ShowCalFiles | ShowSensors |
                        ShowVariables);

    ArgVector args(argv+1, argv+argc);
    app.startArgs(args);
    NidasAppArg* arg = 0;
    while ((arg = app.parseNext()))
    {
        if (arg == &ShowSensors)
        {
	    _sensorClasses.push_back(ShowSensors.getValue());
        }
        else if (app.helpRequested())
        {
            usage(argv[0]);
            return 1;
        }
    }
    _showCalFiles = ShowCalFiles.asBool();
    _showHosts = ShowHosts.asBool();
    _showVariables = ShowVariables.asBool();
    args = app.unparsedArgs();
    if (args.size() != 1)
    {
	usage(argv[0]);
	return 1;
    }
    _xmlFile = args[0];
    return 0;
}

void
PConfig::
usage(const char* argv0) 
{
    cerr << "Usage: "
         << argv0 << " [options] [-s sensorClass [-s ...] ] xml_specifier\n"
         << "  <xmlspecifier> can be a file name or host:port.\n"
         << "Options:\n"
         << NidasApp::getApplicationInstance()->usage()
         << "The default log level is warnings."
         << endl;
}



void
PConfig::
loadFile(const std::string& xmlfile, Project& project)
{
    XMLParser parser;

    // turn on validation
    parser.setDOMValidation(true);
    parser.setDOMValidateIfSchema(true);
    parser.setDOMNamespaces(true);
    parser.setXercesSchema(true);
    parser.setXercesSchemaFullChecking(true);
    parser.setXercesHandleMultipleImports(true);
    parser.setXercesDoXInclude(true);
    parser.setDOMDatatypeNormalization(false);

    xercesc::DOMDocument* doc = parser.parse(xmlfile);
    project.fromDOMElement(doc->getDocumentElement());
    doc->release();
}



// If xmlspec contains a colon and looks like a remote specifier of the
// form "host:port", then return true with host and port set accordingly.
// Otherwise leave host and port unchanged and return false.
bool
PConfig::
parseRemoteSpecifier(const std::string& xmlspec, std::string& host, int& port)
{
    bool parsed = false;
    size_t colon = xmlspec.find_first_of(':');
    if (colon != string::npos)
    {
        string xhost = xmlspec.substr(0, colon);
        string xport = xmlspec.substr(colon+1);
        int iport = atoi(xport.c_str());
        if (xhost.length() && iport > 0)
        {
            host = xhost;
            port = iport;
            parsed = true;
        }
    }
    return parsed;
}


void
PConfig::
loadRemoteXML(const std::string& host, int port, Project& project)
{
    ILOG(("requesting XML from ") << host << " on port " << port);

    // Address to use when fishing for the XML configuration.
    nidas::util::Inet4SocketAddress configSockAddr;
    try {
        configSockAddr = nidas::util::Inet4SocketAddress
            (nidas::util::Inet4Address::getByName(host),
             port); //NIDAS_SVC_REQUEST_PORT_UDP);
    }
    catch(const nidas::util::UnknownHostException& e)
    {
        // shouldn't happen
        ELOG(("") << e.what());
    }

    // Pull in the XML configuration from the DSM server.
    xercesc::DOMDocument* doc =
        nidas::core::requestXMLConfig(/*all=*/true, configSockAddr);

    project.fromDOMElement(doc->getDocumentElement());
    doc->release();
}


int PConfig::main()
{
    int res = 0;
    try {

        Project project;
        string host;
        int port;

        if (parseRemoteSpecifier(_xmlFile, host, port))
        {
            ILOG(("parsed remote xml specifier: host=") << host
                 << ", port=" << port);
            loadRemoteXML(host, port, project);
        }
        else
        {
            loadFile(_xmlFile, project);
        }

        if (!_sensorClasses.empty())
        {
            showSensorClasses(project);
        }
        else if (_showCalFiles)
        {
            showCalFiles(project);
        }
        else if (_showHosts)
        {
            showHostNames(project);
        }
        else if (_showVariables)
        {
            loadVariables(project);
            showVariables();
        }
        else
        {
            showAll(project);
        }
    }
    catch (const nidas::core::XMLException& e) {
        cerr << e.what() << endl;
        res = 1;
    }
    catch (const n_u::InvalidParameterException& e) {
        cerr << e.what() << endl;
        res = 1;
    }
    catch (n_u::IOException& e) {
        cerr << e.what() << endl;
        res = 1;
    }
    XMLImplementation::terminate();
    return res;
}


void
PConfig::
loadVariables(const Project& project)
{
    DLOG(("loading variables..."));
    for (SiteIterator si = project.getSiteIterator(); si.hasNext(); )
    {
        Site* site = si.next();
        for (DSMConfigIterator di = site->getDSMConfigIterator();
             di.hasNext(); )
        {
            const DSMConfig* dsm = di.next();
            for (SensorIterator si2 = dsm->getSensorIterator();
                 si2.hasNext(); )
            {
                DSMSensor* sensor = si2.next();
                for (SampleTagIterator ti = sensor->getSampleTagIterator();
                     ti.hasNext(); )
                {
                    const SampleTag* tag = ti.next();
                    if (!tag->isProcessed()) continue;
                    for (VariableIterator vi = tag->getVariableIterator();
                         vi.hasNext(); )
                    {
                        const Variable* var = vi.next();
                        std::string name = var->getName();
                        // If this tag has a non-zero station, then append
                        // the site name with the # notation.  I think it's
                        // correct to append the site name rather than DSM
                        // name, but I'm not certain...
                        if (tag->getStation() > 0)
                        {
                            name += "#";
                            name += site->getName();
                        }
                        DLOG(("  ") << name);
                        _variables[name] = var;
                    }
                }
            }
        }
    }
    DLOG(("Done."));
}


void
PConfig::
showVariables()
{
    variable_map_t::iterator it;
    for (it = _variables.begin(); it != _variables.end(); ++it)
    {
        const Variable* var = it->second;
        double rate = var->getSampleTag()->getRate();
        cout << setw(20) << it->first << " " << setw(5) << rate << "\n";
    }
}



void PConfig::showAll(const Project& project)
{
    for (SiteIterator si = project.getSiteIterator();
            si.hasNext(); ) {
        Site* site = si.next();
        for (DSMConfigIterator di = site->getDSMConfigIterator();
            di.hasNext(); ) {
            const DSMConfig* dsm = di.next();
            for (SensorIterator si2 = dsm->getSensorIterator(); 
                    si2.hasNext(); ) {
                DSMSensor* sensor = si2.next();
                cout << "site:" << site->getName();
                if (site->getNumber() > 0)
                    cout << ",stn#" << site->getNumber();
                cout << ", sensor: " << sensor->getName() << 
                    '(' << sensor->getDSMId() << ',' <<
                    sensor->getSensorId() << ')' << ',' <<
                    sensor->getClassName() << endl;
                for (SampleTagIterator ti = sensor->getSampleTagIterator();
                    ti.hasNext(); ) {
                    const SampleTag* tag = ti.next();
                    if (!tag->isProcessed()) continue;
                    cout << "  samp#" << tag->getSampleId() << ": ";
                    int iv = 0;
                    for (VariableIterator vi = tag->getVariableIterator();
                        vi.hasNext(); iv++) {
                        const Variable* var = vi.next();
                        if (iv)
                            cout << ',';
                        cout << var->getName();
                    }
                    cout << endl;
                }
            }
            cout << "-----------------------------------------" << endl;
        }
    }
}


void
PConfig::
resolveCalFile(CalFile* cf)
{
    DLOG(("opening ") << cf->getFile());
    try {
        cf->open();
        cout << ", calfile: " << cf->getCurrentFileName();
        cf->close();
    }
    catch(const n_u::IOException&e) {
        cout << ", calfile: " << e.what();
    }
}


void PConfig::showCalFiles(const Project& project)
{
    for (SiteIterator si = project.getSiteIterator();
            si.hasNext(); ) {
        Site* site = si.next();
        const list<DSMConfig*>& dsms = site->getDSMConfigs();
        list<DSMConfig*>::const_iterator di = dsms.begin();

        for (di = dsms.begin(); di != dsms.end(); ++di) {
            DSMConfig* dsm = *di;
            const list<DSMSensor*>& sensors = dsm->getSensors();
            list<DSMSensor*>::const_iterator si2;
            for (si2 = sensors.begin(); si2 != sensors.end(); ++si2) {
                DSMSensor* sensor = *si2;

                const map<string,CalFile*>& cfs = sensor->getCalFiles();

                if (!cfs.empty()) {
                    map<string,CalFile*>::const_iterator ci = cfs.begin();
                    cout << "site: " << site->getName()
                         << ", dsm: " << dsm->getName()
                         << ", sensor: " << sensor->getCatalogName()
                         << ' ' << sensor->getClassName()
                         << ' ' << sensor->getDeviceName()
                         << ' ' << sensor->getHeightString();
                    for ( ; ci != cfs.end(); ++ci) {
                        CalFile* cf = ci->second;
                        resolveCalFile(cf);
                    }
                    cout << endl;
                }
                const list<SampleTag*>& tags = sensor->getSampleTags();
                list<SampleTag*>::const_iterator ti;
                for (ti = tags.begin(); ti != tags.end(); ++ti) {
                    SampleTag* tag = *ti;
                    const vector<Variable*> vars = tag->getVariables();
                    vector<Variable*>::const_iterator vi;
                    for (vi = vars.begin(); vi != vars.end(); ++vi) {
                        Variable* var = *vi;
                        VariableConverter* vc = var->getConverter();
                        if (vc) {
                            CalFile* cf = vc->getCalFile();
                            if (cf) {
                                cout << "site: " << site->getName()
                                     << ", dsm: " << dsm->getName()
                                     << ", sensor: "
                                     << sensor->getCatalogName()
                                     << ' ' << sensor->getClassName()
                                     << ' ' << sensor->getDeviceName()
                                     << ", variable: " << var->getName();
                                resolveCalFile(cf);
                                cout << endl;
                            }
                        }
                    }
                }
            }
        }
    }
}

void PConfig::showSensorClasses(const Project& project)
{
    for (SensorIterator si2 = project.getSensorIterator(); si2.hasNext(); ) {
        DSMSensor* sensor = si2.next();
        if (std::find(_sensorClasses.begin(),_sensorClasses.end(),sensor->getClassName()) != _sensorClasses.end())
            cout << sensor->getDSMId() << ',' << sensor->getSensorId() << ' ' <<
                sensor->getClassName() << endl;
    }
}



void
PConfig::
getHostNames(const Project& project, std::vector<std::string>& dsmnames)
{
    // Traverse the project and write the results to the output stream
    // as selected in the query parameters.
    for (SiteIterator si = project.getSiteIterator(); si.hasNext(); )
    {
        Site* site = si.next();
        for (DSMConfigIterator di = site->getDSMConfigIterator(); di.hasNext(); )
        {
            const DSMConfig* dsm = di.next();
            dsmnames.push_back(site->expandString(dsm->getName()));
        }
    }
}


void
PConfig::
showHostNames(const Project& project)
{
    std::vector<std::string> names;
    getHostNames(project, names);
    for (unsigned int i = 0; i < names.size(); ++i)
    {
        std::cout << names[i] << "\n";
    }
}

extern int nidas::dynld::NIDAS_DYNLD_LINK_ME_DAMMIT;

int main(int argc, char** argv)
{
    cerr << "force link=" << nidas::dynld::NIDAS_DYNLD_LINK_ME_DAMMIT << endl;
    NidasApp app("ck_xml");
    PConfig pconfig;

    app.setApplicationInstance();

    // Set a default for warnings only.
    Logger* logger = Logger::getInstance();
    LogScheme scheme = logger->getScheme("ck_xml_default");
    scheme.addConfig(LogConfig("level=warning"));
    logger->setScheme(scheme);

    try {
        if (pconfig.parseRunstring(app, argc, argv))
        {
            return 1;
        }
    }
    catch (const NidasAppException& ex)
    {
        std::cerr << ex.what() << std::endl;
        std::cerr << "Use -h option to get usage information." << std::endl;
        return 1;
    }
    return pconfig.main();
}
