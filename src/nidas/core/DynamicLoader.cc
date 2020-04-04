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

#include "DynamicLoader.h"

#include <dlfcn.h>

using namespace nidas::core;

namespace n_u = nidas::util;

DynamicLoader* DynamicLoader::_instance = 0;
n_u::Mutex DynamicLoader::_instanceLock;
std::map<std::string,void*> DynamicLoader::_sym_map;

DynamicLoader* DynamicLoader::getInstance() throw(n_u::Exception) {
    if (!_instance) {
	n_u::Synchronized autosync(_instanceLock);
	if (!_instance) _instance = new DynamicLoader();
    }
    return _instance;
}

void DynamicLoader::add(const std::string& name, void* ptr) {
    std::cerr << "adding " << name << " to _sym_map" << std::endl;
    _sym_map[name] = ptr;
}

DynamicLoader::DynamicLoader() throw(n_u::Exception): _defhandle(0),_libhandles()
{
    // default handle for loading symbols from the program and
    // libraries that are already linked or loaded into the program.
    _defhandle = dlopen(NULL,RTLD_LAZY);
    if (_defhandle == 0) throw n_u::Exception(dlerror());

    // a lookup on library "" will search the default handle.
    _libhandles[""] = _defhandle;
}

DynamicLoader::~DynamicLoader()
{
}

void *
DynamicLoader::
lookup(const std::string& name) throw(n_u::Exception) 
{
    n_u::Synchronized autosync(_instanceLock);

    std::map<std::string,void*>::const_iterator fi = _sym_map.find(name);
    if (fi != _sym_map.end()) {
        std::cerr << name << " found in _sym_map" << std::endl;
        return fi->second;
    }
    std::cerr << name << " not found in _sym_map" << std::endl;

    dlerror();  // clear existing error
    void* sym = dlsym(_defhandle,name.c_str());
    const char* errptr = dlerror();
    // man page of dylsym recommends checking non-NULL dlerror() rather
    // than a NULL return from dlsym, since a valid symbol could be located
    // at (void*)0, and if this lookup is to be truely general purpose is
    // should support looking up symbols that resolve to address 0.
    // However if code outside this class in other threads is calling dl
    // routines, there is a very tiny chance that dlerror() may have been
    // reset between the dlsym() and the second dlerror() in this thread.
    // So the error condition is a bit undetermined in multithreaded apps.
    // The calling code is not expected to check for a NULL pointer,
    // only catch an exception. To avoid a seg fault we'll treat a symbol
    // at address 0 as an error.
    // It's a tradeoff, with little likelihood of failure either way...
    if (errptr || !sym) {
        std::string errStr;
        if (errptr) errStr = errptr;
        else errStr = name + ": unknown error";
    	throw n_u::Exception(
                std::string("DynamicLoader::lookup: ") + errStr);
    }
    return sym;
}

void *
DynamicLoader::
lookup(const std::string& library,const std::string& name)
    throw(n_u::Exception) 
{

    // dlerror() returns a readable string describing the most recent error that
    // occurred from dlopen(), dlsym() or dlclose() since the last call to  dlerror(). 
    // That t'aint very compatible with multi-threading. We'll lock a mutex here
    // to have a little more certainty that the string relates to the immediately
    // proceeding dl call by this class from the calling thread. This, of course,
    // has no control of calls to dl routines outside of this class.

    n_u::Synchronized autosync(_instanceLock);

    std::map<std::string,void*>::const_iterator fi = _sym_map.find(name);
    if (fi != _sym_map.end()) {
        std::cerr << name << " found in _sym_map" << std::endl;
        return fi->second;
    }
    std::cerr << name << " not found in _sym_map" << std::endl;

    std::map<std::string,void*>::iterator mi = _libhandles.find(library);

    void* libhandle;

    if (mi == _libhandles.end()) {
        libhandle = dlopen(library.c_str(), RTLD_LAZY| RTLD_GLOBAL);
	if (libhandle == 0) {
            // In case other code is calling dl functions, check for
            // non-null dlerror(), since constructing a std::string from a
            // NULL char pointer results in a crash.
            // Hopefully the pointer remains valid here. 
            const char* errptr = dlerror();
            std::string errStr = library + ": unknown error";
            if (errptr) errStr = std::string(errptr);
            // dlerror() string contains the library name (or the program name
            // for the default handle), so we don't need to repeat them in the exc msg.
	    throw n_u::Exception
		(std::string("DynamicLoader::lookup, open: ") + errStr);
        }
    }
    else libhandle = mi->second;

    dlerror();  // clear existing error
    void* sym = dlsym(libhandle,name.c_str());
    const char* errptr = dlerror();
    // see other lookup method about error handling.
    if (errptr || !sym) {
        std::string errStr;
        if (errptr) errStr = errptr;
        else errStr = library + ": " + name + ": unknown error";

        // If no symbols have been found in this library so far, unload it.
        if (mi == _libhandles.end()) dlclose(libhandle);

        // dlerror() string contains the library name (or the program name
        // for the default handle) and the symbol name. Don't need to repeat
        // them in the exception message.
        throw n_u::Exception(
                std::string("DynamicLoader::lookup: ") + errStr);
    }
    // If a symbol is found, the library handle is left open, and
    // all its symbols can then be found via the default library handle.
    // If the user always searches the default handle first,
    // then successive symbols will not be found directly via this handle.
    // So generally mi will be equal to end() here, so checking
    // doesn't save much, but, hey we try...
    if (mi == _libhandles.end()) _libhandles[library] = libhandle;
    return sym;
}
