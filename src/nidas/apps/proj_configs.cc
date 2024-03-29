/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
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

    Utility program to list, add and terminate the configurations
    for a project.
*/

#include <nidas/core/ProjectConfigs.h>

#include <iostream>

#include <unistd.h>
#include <getopt.h>

using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;
using nidas::util::UTime;

class ProjConfigIO 
{

public:

    ProjConfigIO();

    int parseRunstring(int argc, char** argv);

    static int usage(const char* argv0);

    int run();

    void listConfigs(bool allinfo);

    void addConfigByTime();

    void termConfig();

    void getConfig();

    void getConfigForTime();

    enum tasks { NUTTIN_TO_DO, LIST_CONFIG_NAMES, LIST_CONFIGS,
        ADD_CONFIG_BY_TIME, TERM_CONFIG, GET_CONFIG, GET_CONFIG_FOR_TIME };

private:

    /**
     * XML file containing project configurations.
     */
    string xmlFile;

    /**
     * What to do, per runstring arguments.
     */
    enum tasks task;

    /**
     * Name of project configuration.
     */
    string cname;

    /**
     * Time of start of project configuration
     */
    n_u::UTime cbegin;  

    /**
     * Time of end of project configuration
     */
    n_u::UTime cend;  

    /**
     * Name of xml containing nidas configuration for a
     * project.
     */
    string cxml;  

    string timeformat;

    ProjectConfigs configs;

};

/* static */
int ProjConfigIO::usage(const char* argv0)
{
    cerr << "Usage: " << argv0 << " [-a ...] [-c [time]] [-f [...]] [-g ...] [-l] [-n] [-t ...] configs_xml_file\n\
-a: add a configuration entry to configs_xml_file\n\
-c: list configurations corresponding to a time, where the format of the time is specified\n\
    with the -f option. If no time is specified, then list current configuration.\n\
-f: print formatted times\n\
-g: get a configuation entry by name, with times formatted as YYYY mmm dd HH:MM:SS\n\
-l: list configurations in configs_xml_file\n\
-n: list configuration names in configs_xml_file\n\
-t: terminate a configuration entry in configs_xml_file\n\
\n\
Syntax:\n\
-a name xmlfile \"start\" [\"end\"]]\n\
    Add a configuration entry to the configs_xml_file with a given name,\n\
    with the given XML file name, starting at the time \"start\" and\n\
    ending at the time \"end\". If \"end\" is not given it defaults to\n\
    \"start\" plus two years.\n\
-f [timeformat]\n\
    Specify time input and output format for options -a, -c, -t, -g  and -l.\n\
    Default format is \"%Y %b %d %H:%M:%S\". If -f option is not specified,\n\
    input and output times will be in integer seconds since 1970 Jan 1 00:00 UTC\n\
-t \"end\"\n\
    Terminate the last configuration entry in configs_xml_file\n\
    with the given end time\n\
-g name\n\
    Return the XML file name, begin time and end time attributes for all\n\
    configuration entries with the given name\n\
\n\
Start and end times should be enclosed in quotes so they are seen\n\
as one argument. Input time formats are as supported by nidas::util::UTime::parse,\n\
space separated year month day hh:mm:ss, in UTC.\n\
\n\
Examples:\n\
Add a configuration called \"rf07\", with an XML file named research.xml, starting at 2007 Jan 13 13:40 UTC\n" <<
argv0 << " -f \"%Y %b %d %H:%M\" -a rf07 \'$CONFIGS/research.xml\' \"2007 Jan 13 13:40\"\n\
\n\
Terminate the above configuration at 2007 Jan 14 01:40 UTC\n" <<
argv0 << " -f \"%Y %b %d %H:%M\" -t \"2007 Jan 14 01:40\"\n\
\n";
    
    return 1;
}

ProjConfigIO::ProjConfigIO(): xmlFile(),
    task(NUTTIN_TO_DO),cname(),cbegin(UTime::MIN),
    cend(UTime::MAX),cxml(),timeformat(),configs()
{
}

int ProjConfigIO::parseRunstring(int argc, char** argv)
{
    extern char *optarg;       /* set by getopt() */
    extern int optind;       /* "  "     "     */
    int opt_char;     /* option character */

    string beginTime;
    string endTime;

    while ((opt_char = getopt(argc, argv, "a:cfg:lnt:")) != -1) {

#ifdef DEBUG
        cerr << "opt_char=" << (char) opt_char << ", optind=" << optind <<
            ", argc=" << argc << ", optarg=" << (optarg ? optarg : "") << endl;
        if (optind < argc) cerr << "argv[optind]=" << argv[optind] << endl;
#endif

	switch (opt_char) {
        case 'a':   // add config
            // args: name, xml, start, [end]
            //  -a tf01 "2006 aug 16 00:30" $XXX/$YYY/config.xml
            task = ADD_CONFIG_BY_TIME;
            cname = optarg;

            if (optind >= argc - 1 || argv[optind][0] == '-')
                return usage(argv[0]);
            cxml = argv[optind++];

            if (optind >= argc - 1 || argv[optind][0] == '-')
                return usage(argv[0]);
            beginTime = argv[optind++];

            // end time is optional
            if (optind >= argc - 1 || argv[optind][0] == '-') break;
            endTime = argv[optind++];
            break;
        case 'c':
	    task = GET_CONFIG_FOR_TIME;
            if (optind >= argc - 1 || argv[optind][0] == '-')
                cbegin = n_u::UTime();
            else beginTime = argv[optind++];
	    break;
	case 'f':
            if (optind >= argc - 1 || argv[optind][0] == '-')
                timeformat = "%Y %b %d %H:%M:%S";
            else timeformat = argv[optind++];
	    break;
	case 'g':
	    task = GET_CONFIG;
            cname = optarg;
	    break;
	case 'l':
	    task = LIST_CONFIGS;
	    break;
	case 'n':
	    task = LIST_CONFIG_NAMES;
	    break;
	case 't':   // terminate last config
            // args: name, start, end, xml
            //  -e "2006 aug 16 00:30"
	    task = TERM_CONFIG;
            endTime = optarg;
	    break;
	case '?':
	    return usage(argv[0]);
	}
    }
    if (beginTime.length() > 0) {
        try {
            if (timeformat.length() > 0) 
                cbegin = n_u::UTime::parse(true,beginTime,timeformat);
            else
                cbegin = n_u::UTime::parse(true,beginTime);
        }
        catch (const n_u::ParseException& e) {
            cerr << e.what() << endl;
            return usage(argv[0]);
        }
        cend = cbegin + USECS_PER_DAY * 365 * 2;
    }
    if (endTime.length() > 0) {
        try {
            if (timeformat.length() > 0) 
                cend = n_u::UTime::parse(true,endTime,timeformat);
            else
                cend = n_u::UTime::parse(true,endTime);
        }
        catch (const n_u::ParseException& e) {
            cerr << e.what() << endl;
            return usage(argv[0]);
        }
    }
    if (task == NUTTIN_TO_DO) return usage(argv[0]);
    if (optind < argc) xmlFile = argv[optind++];
    if (xmlFile.length() == 0) return usage(argv[0]);
    if (optind != argc) return usage(argv[0]);
    return 0;
}

int ProjConfigIO::run() 
{
    try {
        configs.parseXML(xmlFile);

        switch(task) {
        case LIST_CONFIGS:
            listConfigs(true);
            break;
        case LIST_CONFIG_NAMES:
            listConfigs(false);
            break;
        case ADD_CONFIG_BY_TIME:
            addConfigByTime();
            break;
        case TERM_CONFIG:
            termConfig();
            break;
        case GET_CONFIG:
            getConfig();
            break;
        case GET_CONFIG_FOR_TIME:
            getConfigForTime();
            break;
        default:
            return 1;
        }
        XMLImplementation::terminate();
        return 0;
    }
    catch(const nidas::core::XMLException& e) {
        cerr << e.what() << endl;
        return 1;
    }
    catch(const n_u::InvalidParameterException& e) {
        cerr << e.what() << endl;
        return 1;
    }
    catch(const n_u::IOException& e) {
        cerr << e.what() << endl;
        return 1;
    }
}

void ProjConfigIO::listConfigs(bool allinfo)
{

    if (timeformat.length() > 0) cout << n_u::setTZ<char>("GMT") <<
        n_u::setDefaultFormat<char>(timeformat);

    const list<const ProjectConfig*>& cfgs = configs.getConfigs();

    list<const ProjectConfig*>::const_iterator ci = cfgs.begin();

    for( ; ci != cfgs.end(); ++ci) {
        const ProjectConfig* cfg = *ci;
        cout << cfg->getName();
        if (allinfo) {
            cout << ' ' << cfg->getXMLName() << ' ';
            if (timeformat.length() > 0)
                cout << cfg->getBeginTime() << " - " << cfg->getEndTime() << endl;
            else
                cout << cfg->getBeginTime().toUsecs()/USECS_PER_SEC << ' ' <<
                    cfg->getEndTime().toUsecs()/USECS_PER_SEC << endl;
        }
        else cout << endl;

    }
}

void ProjConfigIO::getConfig()
{

    if (timeformat.length() > 0) cout << n_u::setTZ<char>("GMT") <<
        n_u::setDefaultFormat<char>(timeformat);

    const list<const ProjectConfig*>& cfgs = configs.getConfigs();

    list<const ProjectConfig*>::const_iterator ci = cfgs.begin();

    for( ; ci != cfgs.end(); ++ci) {
        const ProjectConfig* cfg = *ci;

        // may be more than one match
        if (cfg->getName() == cname) {
            cout << cfg->getXMLName() << ' ';
            if (timeformat.length() > 0)
                cout << cfg->getBeginTime() << " - " << cfg->getEndTime() << endl;
            else
                cout << cfg->getBeginTime().toUsecs()/USECS_PER_SEC << ' ' <<
                    cfg->getEndTime().toUsecs()/USECS_PER_SEC << endl;
        }
    }
}

void ProjConfigIO::getConfigForTime()
{

    if (timeformat.length() > 0) cout << n_u::setTZ<char>("GMT") <<
        n_u::setDefaultFormat<char>(timeformat);

    const list<const ProjectConfig*>& cfgs = configs.getConfigs();

    list<const ProjectConfig*>::const_iterator ci = cfgs.begin();

    for( ; ci != cfgs.end(); ++ci) {
        const ProjectConfig* cfg = *ci;
        if (cbegin >= cfg->getBeginTime() &&
                cbegin < cfg->getEndTime()) {
            cout << cfg->getName() << ' ' << cfg->getXMLName() << ' ';
            if (timeformat.length() > 0)
                cout << cfg->getBeginTime() << " - " << cfg->getEndTime() << endl;
            else
                cout << cfg->getBeginTime().toUsecs()/USECS_PER_SEC << ' ' <<
                    cfg->getEndTime().toUsecs()/USECS_PER_SEC << endl;
        }
    }
}

void ProjConfigIO::addConfigByTime()
{

    ProjectConfig* ncfg = new ProjectConfig;
    ncfg->setName(cname);
    ncfg->setBeginTime(cbegin);
    ncfg->setEndTime(cend);
    ncfg->setXMLName(cxml);

    configs.addConfigByTime(ncfg);

#ifdef DEBUG
    const list<const ProjectConfig*>& cfgs = configs.getConfigs();
    list<const ProjectConfig*>::const_iterator ci = cfgs.begin();
    for( ; ci != cfgs.end(); ++ci) {
        const ProjectConfig* cfg = *ci;
        cout << cfg->getName() << endl;
    }
#endif

    configs.writeXML(xmlFile);
}

void ProjConfigIO::termConfig()
{
    const list<const ProjectConfig*>& cfgs = configs.getConfigs();

    if (cfgs.size() == 0) throw n_u::InvalidParameterException("no configs");

    const ProjectConfig* cfg = cfgs.back();

    ProjectConfig* newcfg = new ProjectConfig(*cfg);

    configs.removeConfig(cfg);

    newcfg->setEndTime(cend);
    configs.addConfigByTime(newcfg);

    configs.writeXML(xmlFile);
}

int main(int argc, char** argv) 
{
    ProjConfigIO proj;

    int res = proj.parseRunstring(argc,argv);
    if (res != 0) return res;

    return proj.run();
}
