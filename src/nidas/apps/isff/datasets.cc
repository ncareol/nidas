/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2014, Copyright University Corporation for Atmospheric Research
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

    Utility program to list datasets for a project.
*/

#include <nidas/core/Datasets.h>

#include <iostream>

#include <unistd.h>
#include <getopt.h>

using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

class DatasetsApp 
{

public:

    DatasetsApp();

    int parseRunstring(int argc, char** argv);

    static int usage(const char* argv0);

    int run();

    void listDatasetNames();

    void showEnv();

    enum tasks { NUTTIN_TO_DO, LIST_DATASET_NAMES, SHOW_BASH_ENV, SHOW_CSH_ENV };

private:

    /**
     * XML file containing datasets.
     */
    string _xmlFile;

    /**
     * What to do, per runstring arguments.
     */
    enum tasks _task;

    /**
     * Name of dataset.
     */
    string _datasetName;

    Datasets _datasets;

};

/* static */
int DatasetsApp::usage(const char* argv0)
{
    cerr << "Usage: " << argv0 << " [-b name | -c name ] [-n]  datasets_xml_file\n\
    -b: display bourne shell (bash) commands to setup environment for the dataset\n\
    -c: display csh shell commands to setup environment for the dataset\n\
    -n: list dataset names\n\
\n\
Syntax:\n\
-g name\n\
    Return the attributes for the given dataset\n\
\n\
Examples:\n\
\n";

    return 1;
}

DatasetsApp::DatasetsApp(): _xmlFile(),
    _task(NUTTIN_TO_DO),_datasetName(), _datasets()
{
}

int DatasetsApp::parseRunstring(int argc, char** argv)
{
    extern char *optarg;       /* set by getopt() */
    extern int optind;       /* "  "     "     */
    int opt_char;     /* option character */

    while ((opt_char = getopt(argc, argv, "b:c:n")) != -1) {

#ifdef DEBUG
        cerr << "opt_char=" << (char) opt_char << ", optind=" << optind <<
            ", argc=" << argc << ", optarg=" << (optarg ? optarg : "") << endl;
        if (optind < argc) cerr << "argv[optind]=" << argv[optind] << endl;
#endif

	switch (opt_char) {
	case 'b':
	    _task = SHOW_BASH_ENV;
            _datasetName = optarg;
	    break;
	case 'c':
	    _task = SHOW_CSH_ENV;
            _datasetName = optarg;
	    break;
	case 'n':
	    _task = LIST_DATASET_NAMES;
	    break;
	case '?':
	    return usage(argv[0]);
	}
    }

    if (_task == NUTTIN_TO_DO) return usage(argv[0]);
    if (optind < argc) _xmlFile = argv[optind++];
    if (_xmlFile.length() == 0) return usage(argv[0]);
    if (optind != argc) return usage(argv[0]);
    return 0;
}

int DatasetsApp::run() 
{
    try {
        _datasets.parseXML(_xmlFile,false);

        switch(_task) {
        case LIST_DATASET_NAMES:
            listDatasetNames();
            break;
        case SHOW_BASH_ENV:
        case SHOW_CSH_ENV:
            showEnv();
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

void DatasetsApp::listDatasetNames()
{

    list<Dataset> datasets = _datasets.getDatasets();

    list<Dataset>::const_iterator ci = datasets.begin();

    for( ; ci != datasets.end(); ++ci) {
        const Dataset& dataset = *ci;
        cout << dataset.getName() << endl;
    }
}

void DatasetsApp::showEnv()
{

    const Dataset& dataset = _datasets.getDataset(_datasetName);

    switch(_task) {
    case SHOW_BASH_ENV:
        cout << "export DATASET=\"" << dataset.getName() << "\";" << endl;
        break;
    case SHOW_CSH_ENV:
        cout << "setenv DATASET \"" << dataset.getName() << "\";" << endl;
        break;
    default:
        break;
    }

    std::map<std::string,std::string> vars = dataset.getEnvironmentVariables();

    std::map<std::string,std::string>::const_iterator ei = vars.begin();

    for( ; ei != vars.end(); ++ei) {
        switch(_task) {
        case SHOW_BASH_ENV:
            cout << "export " << ei->first << "=\"" << ei->second << "\";" << endl;
            break;
        case SHOW_CSH_ENV:
            cout << "setenv " << ei->first << " \"" << ei->second << "\";" << endl;
            break;
        default:
            break;
        }
    }
}

int main(int argc, char** argv) 
{
    DatasetsApp proj;

    int res = proj.parseRunstring(argc,argv);
    if (res != 0) return res;

    return proj.run();
}
