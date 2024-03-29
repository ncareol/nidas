/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
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

#include "Sample.h"

#include <iostream>

const float nidas::core::floatNAN = nanf("");

const double nidas::core::doubleNAN = nan("");

using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

#ifdef MUTEX_PROTECT_REF_COUNTS
/* static */
// n_u::Mutex Sample::refLock;
#endif

#ifndef PROTECT_NSAMPLES
/* static */
int Sample::_nsamps(0);
#else
/* static */
n_u::MutexCount<int> Sample::_nsamps(0);
#endif

#if defined(ENABLE_VALGRIND) && !defined(PROTECT_NSAMPLES)

#include <valgrind/helgrind.h>

class Sample::InitValgrind
{
public:
  InitValgrind()
  {
    VALGRIND_HG_DISABLE_CHECKING(&Sample::_nsamps, sizeof(Sample::_nsamps));
  }
};

static Sample::InitValgrind ig;

#endif

Sample* nidas::core::getSample(sampleType type, unsigned int len)
{
    Sample* samp = 0;
    unsigned int lin = len;
    try {
        switch(type) {
        case CHAR_ST:
            samp = getSample<char>(len);
            break;
        case UCHAR_ST:
            samp = getSample<unsigned char>(len);
            break;
        case SHORT_ST:
            len /= sizeof(short);
            if (len * sizeof(short) != lin) return 0;
            samp = getSample<short>(len);
            break;
        case USHORT_ST:
            len /= sizeof(short);
            if (len * sizeof(short) != lin) return 0;
            samp = getSample<unsigned short>(len);
            break;
        case INT32_ST:
            assert(sizeof(int) == 4);
            len /= sizeof(int);
            if (len * sizeof(int) != lin) return 0;
            samp = getSample<int>(len);
            break;
        case UINT32_ST:
            assert(sizeof(unsigned int) == 4);
            len /= sizeof(unsigned int);
            if (len * sizeof(unsigned int) != lin) return 0;
            samp = getSample<unsigned int>(len);
            break;
        case FLOAT_ST:
            len /= sizeof(float);
            if (len * sizeof(float) != lin) return 0;
            samp = getSample<float>(len);
            break;
        case DOUBLE_ST:
            len /= sizeof(double);
            if (len * sizeof(double) != lin) return 0;
            samp = getSample<double>(len);
            break;
        case INT64_ST:
            len /= sizeof(long long);
            if (len * sizeof(long long) != lin) return 0;
            samp = getSample<long long>(len);
            break;
        case UNKNOWN_ST:
        default:
            return 0;
        }
    }
    catch (const SampleLengthException& e) {
        return 0;
    }
    return samp;
}


SampleT<char>* nidas::core::getSample(const char* data)
{
    unsigned int len = strlen(data)+1;
    SampleT<char>* samp = 
        SamplePool<SampleT<char> >::getInstance()->getSample(len);
    strcpy(samp->getDataPtr(), data);
    return samp;
}
