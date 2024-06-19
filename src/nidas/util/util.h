// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
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

/*! \namespace nidas::util
    \brief General utility classes.
    nidas::util contains classes of general utility, like Socket, Thread, etc.
    The classes use only the standard Unix system libraries and libstdc++,
    and have no dependency on other external packages such as
    an XML parser, or a logging package.
 */

#ifndef NIDAS_UTIL_UTIL_H
#define NIDAS_UTIL_UTIL_H

#include <string>

#include "IOException.h"

namespace nidas { namespace util {

/**
 * Utility function for replacing backslash sequences in a string.
 *  \\n=newline, \\r=carriage-return, \\t=tab, \\\\=backslash
 *  \\xhh=hex, where hh are (exactly) two hex digits and
 *  \\000=octal, where 000 are exactly three octal digits.
 */
std::string replaceBackslashSequences(const std::string& str);

/* note that the above back slashes above are doubled so that
 * doxygen displays them as one back slash.  One does
 * not double them in the parameter string.
 */

/**
 * Utility function for substituting backslash sequences back
 * into a string.
 */
std::string addBackslashSequences(const std::string& str);

/**
 * Utility to remove white space characters (matching isspace()) from end of string.
 */
void trimString(std::string& str);

/**
 * Replace all occurences of pat in string in with rep.
 */
void replaceCharsIn(std::string& in,const std::string& pat, const std::string& rep);

std::string replaceChars(const std::string& in,const std::string& pat, const std::string& rep);

/**
 * Calculate wind direction in degrees from U and V wind components,
 * or if U and V are both zero return NAN as wind direction is undefined.
 */
float dirFromUV(float u, float v);

/**
 * Normalize dir, then derive u and v from spd and direction.
 *
 * u, v are the components of wind direction, positive u in the north
 * direction, positive v in east direction, where "north" can be the
 * instrument's reference azimuth in instrument coordinate space, or north can
 * be geographic north. If spd is zero, then u and v are set to zero
 * regardless of direction.  @see derive_spd_dir_from_uv().
 */
void
derive_uv_from_spd_dir(float& u, float& v, float& spd, float& dir);

/**
 * Derive speed and direction from wind components u, v.
 *
 * Unlike u, v which are positive in the direction the wind is blowing
 * towards, direction is where the wind is blowing from.  If both u and v are
 * zero, then dir is set to nan.
 */
void
derive_spd_dir_from_uv(float& spd, float& dir, float& u, float& v);

}}	// namespace nidas::util

#endif
