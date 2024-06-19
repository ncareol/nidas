// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2008, Copyright University Corporation for Atmospheric Research
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

#include "util.h"
#include "Process.h"

#include <sstream>
#include <iomanip>
#include <math.h>

using namespace std;


namespace nidas { namespace util {


std::string replaceBackslashSequences(const std::string& str)
{
    string::size_type bs;
    string res = str;
    for (string::size_type ic = 0; (bs = res.find('\\',ic)) != string::npos;
    	ic = bs) {
	bs++;
	if (bs == res.length()) break;
        switch(res[bs]) {
	case 'e':
	    res.erase(bs,1);
	    res[bs-1] = '\e';
	    break;
	case 'f':
	    res.erase(bs,1);
	    res[bs-1] = '\f';
	    break;
	case 'n':
	    res.erase(bs,1);
	    res[bs-1] = '\n';
	    break;
	case 'r':
	    res.erase(bs,1);
	    res[bs-1] = '\r';
	    break;
	case 't':
	    res.erase(bs,1);
	    res[bs-1] = '\t';
	    break;
	case '\\':
	    res.erase(bs,1);
	    res[bs-1] = '\\';
	    break;
	case 'x':	//  \xhh	hex
	    if (bs + 2 >= res.length()) break;
	    {
		istringstream ist(res.substr(bs+1,2));
		int hx;
		ist >> hex >> hx;
		if (!ist.fail()) {
		    res.erase(bs,3);
		    res[bs-1] = (char)(hx & 0xff);
		}
	    }
	    break;
	case '0':	//  \000   octal
	case '1':
	case '2':
	case '3':
	    if (bs + 2 >= res.length()) break;
	    {
		istringstream ist(res.substr(bs,3));
		int oc;
		ist >> oct >> oc;
		if (!ist.fail()) {
		    res.erase(bs,3);
		    res[bs-1] = (char)(oc & 0xff);
		}
	    }
	    break;
	}
    }
    return res;
}

/* static */
std::string addBackslashSequences(const std::string& str)
{
    string res;
    for (unsigned int ic = 0; ic < str.length(); ic++) {
	char c = str[ic];
        switch(c) {
	case '\e':
	    res.append("\\e");
	    break;
	case '\f':
	    res.append("\\f");
	    break;
	case '\n':
	    res.append("\\n");
	    break;
	case '\r':
	    res.append("\\r");
	    break;
	case '\t':
	    res.append("\\t");
	    break;
	case '\\':
	    res.append("\\\\");
	    break;
	default:
	    // in C locale isprint returns true for
	    // alpha-numeric, punctuation and the space character
	    // but not other white-space characters like tabs,
	    // newlines, carriage-returns,  form-feeds, etc.
	    if (::isprint(c)) res.push_back(c);
	    else {
		ostringstream ost;
		ost << "\\x" << hex << setw(2)
			<< setfill('0') << (int)(unsigned char) c;
		res.append(ost.str());
	    }
	        
	    break;
	}
    }
    return res;
}

void trimString(std::string& str)
{
    for (string::iterator si = str.end(); si != str.begin(); ) {
        --si;
        if (!isspace(*si)) break;
        si = str.erase(si);
    }
}

void replaceCharsIn(std::string& in,const std::string& pat, const std::string& rep)
{
    string::size_type patpos;
    while ((patpos = in.find(pat,0)) != string::npos)
        in.replace(patpos,pat.length(),rep);
}

std::string replaceChars(const std::string& in,const std::string& pat, const std::string& rep)
{
    string res = in;
    replaceCharsIn(res,pat,rep);
    return res;
}


float dirFromUV(float u, float v){
    float dir = nanf("");
    if (!(u==0 && v==0)){
        dir = atan2f(-u, -v) * 180.0 / M_PI;
        if (dir < 0.0) dir += 360.0;
    }
    return dir;
}


void
derive_uv_from_spd_dir(float& u, float& v, float& spd, float& dir)
{
    dir = fmod(dir, 360.0);
    if (dir < 0.0)
        dir += 360.0;
    if (spd == 0)
    {
        u = v = 0;
    }
    else
    {
        u = -spd * ::sin(dir * M_PI / 180.0);
        v = -spd * ::cos(dir * M_PI / 180.0);
    }
}


void
derive_spd_dir_from_uv(float& spd, float& dir, float& u, float& v)
{
    dir = dirFromUV(u, v);
    spd = ::sqrt(u*u + v*v);
}


}} // namespace nidas::util
