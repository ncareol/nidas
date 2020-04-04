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

#ifndef NIDAS_CORE_DYNAMIC_LOADER_H
#define NIDAS_CORE_DYNAMIC_LOADER_H

#include <nidas/util/ThreadSupport.h>
#include <nidas/util/Exception.h>

#include <map>

namespace nidas { namespace core {

/**
 * Class providing dynamic loader functionality of
 * system functions dlopen() and dlsym() to look up symbols.
 */
class DynamicLoader {

public:

    /**
     * Get a pointer to the singleton instance of DynamicLoader.
     */
    static DynamicLoader* getInstance() throw(nidas::util::Exception);

    static void add(const std::string& name, void* ptr);

    /**
     * Search the main program itself, and its currently loaded
     * libraries for a symbol. Throws an exception if the lookup fails.
     */
    void *lookup(const std::string& name) throw(nidas::util::Exception);

    /**
     * Return a pointer to a symbol from the given library.
     * @ param library: name of the library.
     * @ param name: name of the symbol to look up.
     *
     * If the library name is an empty string, then the libraries linked with
     * the program, and any currently loaded dynamic libraries will
     * be searched.
     * If the library name is absolute (begins with a forward slash),
     * then it is loaded. Otherwise a search for the library is done, using
     * the program's library directory search path, which is controlled
     * by compile time flags, the LD_LIBRARY_PATH environment variable,
     * and ld.so/ldconfig/ld.so.conf.
     * lookup() throws an exception if the library could not be found and
     * loaded or the symbol could not be found in the library.
     * If the symbol is found, the library will remain loaded, and
     * symbols in that library can then be found via lookup(name),
     * or by specifying an emptry string for the library.
     */
    void *lookup(const std::string& library,const std::string& name)
    	throw(nidas::util::Exception);

private:
    DynamicLoader() throw(nidas::util::Exception);

    /** No copy. */
    DynamicLoader(const DynamicLoader&);

    /** No assignment */
    DynamicLoader& operator=(const DynamicLoader&);

    ~DynamicLoader();

    /**
     * Handle, returned by dlopen(), of the program itself, and its
     * linked and dynamically loaded libraries.
     */
    void*  _defhandle;

    /**
     * Handles, by library name, returned by dlopen() of libraries
     * that are currently open, because one or more symbols have
     * been found in them.
     */
    std::map<std::string,void*>  _libhandles;

    static DynamicLoader* _instance;

    static nidas::util::Mutex _instanceLock;

    static std::map<std::string,void*> _sym_map;

};

}}	// namespace nidas namespace core

#endif
