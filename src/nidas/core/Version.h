/*
 ********************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate$

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL$
 ********************************************************************

*/

#ifndef NIDAS_CORE_VERSION_H
#define NIDAS_CORE_VERSION_H

namespace nidas { namespace core {

/**
 * Class of static member functions providing version strings.
 */
class Version
{
public:
    static const char* getSoftwareVersion() { return _version; }
    static const char* getArchiveVersion() { return "1"; }

private:
    static const char* _version;
};

}}	// namespace nidas namespace core

#endif
