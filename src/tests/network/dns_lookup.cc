/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2009, Copyright University Corporation for Atmospheric Research
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
#include <nidas/core/SocketAddrs.h>
#include <nidas/core/Sample.h>
#include <nidas/util/UTime.h>
#include <nidas/util/Inet4Address.h>

#include <iostream>
#include <iomanip>

using namespace std;

namespace n_u = nidas::util;
using namespace nidas::core;

int main(int /*argc*/, char** /*argv*/)
{
    
    string host;
    string revhost;
    n_u::Inet4Address addr;

    dsm_time_t t1 = 0;
    dsm_time_t t2;

    try {
        host = NIDAS_MULTICAST_ADDR;
        t1 = n_u::getSystemTime();
        addr = n_u::Inet4Address::getByName(host);
        t2 = n_u::getSystemTime();
        cout << "host \"" << host << "\" to addr: " << addr.getHostAddress() <<
            ", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
        t1 = n_u::getSystemTime();
        revhost = addr.getHostName();
        t2 = n_u::getSystemTime();
        cout << "addr: " << addr.getHostAddress() << " to host \"" << revhost <<
            "\", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
    }
    catch(const n_u::UnknownHostException& e) {
        t2 = n_u::getSystemTime();
        cerr << e.what() <<
            ", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
    }
    cout << endl;

    try {
        host = "quacka-quacka";
        t1 = n_u::getSystemTime();
        addr = n_u::Inet4Address::getByName(host);
        t2 = n_u::getSystemTime();
        cout << "host \"" << host << "\" to addr: " << addr.getHostAddress() <<
            ", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
        t1 = n_u::getSystemTime();
        revhost = addr.getHostName();
        t2 = n_u::getSystemTime();
        cout << "addr: " << addr.getHostAddress() << " to host \"" << revhost <<
            "\", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
    }
    catch(const n_u::UnknownHostException& e) {
        t2 = n_u::getSystemTime();
        cerr << e.what() <<
            ", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
    }
    cout << endl;

    try {
        host = "www.google.com";
        t1 = n_u::getSystemTime();
        addr = n_u::Inet4Address::getByName(host);
        t2 = n_u::getSystemTime();
        cout << "host \"" << host << "\" to addr: " << addr.getHostAddress() <<
            ", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
        t1 = n_u::getSystemTime();
        revhost = addr.getHostName();
        t2 = n_u::getSystemTime();
        cout << "addr: " << addr.getHostAddress() << " to host \"" << revhost <<
            "\", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
    }
    catch(const n_u::UnknownHostException& e) {
        t2 = n_u::getSystemTime();
        cerr << e.what() <<
            ", lookup delay=" << fixed << setprecision(2) << setw(7) <<
                (float)(t2-t1)/USECS_PER_SEC << " sec" << endl;
    }
    return 0;
}
