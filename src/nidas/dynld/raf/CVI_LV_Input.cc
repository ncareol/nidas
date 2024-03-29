// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
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

#include "CVI_LV_Input.h"
#include <nidas/core/ServerSocketIODevice.h>

using namespace nidas::core;
using namespace nidas::dynld::raf;
using namespace std;

namespace n_u = nidas::util;

NIDAS_CREATOR_FUNCTION_NS(raf,CVI_LV_Input)

CVI_LV_Input::CVI_LV_Input(): _tt0(0), prevCVTdiff(0)
{
}

void CVI_LV_Input::open(int flags)
{
    CharacterSensor::open(flags);
    init();
}

IODevice* CVI_LV_Input::buildIODevice()
{
    ServerSocketIODevice* dev = new ServerSocketIODevice();
    dev->setTcpNoDelay(true);
    return dev;
}

float CVI_LV_Input::CVTdiff(float cvtdiff_in)
{
    float cvtdiff = cvtdiff_in;

    /*
     * SOCRATES flights 3-7; Fix timing error due to formatting change that dropped
     * the 0.5 on times after 100,000 seconds.  Times went 100000, 100002, 100002,
     * 100004, 100004, etc
     */

    // Don't bother under 100000 or if we have the .5.
    if (cvtdiff_in >= 100000.0 && (cvtdiff_in - (int)cvtdiff_in) == 0.0)
    {
        if (cvtdiff_in == 100000.0)  //  at 100000 just add .5.
            cvtdiff += 0.5;
        else
        {
            float thisTime = cvtdiff_in;
            if (thisTime == prevCVTdiff)
                cvtdiff += 0.5;
            else
                cvtdiff -= 0.5;
        }
        prevCVTdiff = cvtdiff_in;
    }

    return cvtdiff;
}

bool CVI_LV_Input::process(const Sample *samp,
                           std::list<const Sample*>&results) throw()
{

    // invoke the standard scanf processing.
    list<const Sample*> tmpresults;
    CharacterSensor::process(samp, tmpresults);

    list<const Sample *>::const_iterator it = tmpresults.begin();
    for (; it != tmpresults.end(); ++it) {
        samp = *it;
        if (samp->getType() != FLOAT_ST) {   // shouldn't happen
            samp->freeReference();
            continue;
        }

        const SampleT<float>* fsamp = static_cast<const SampleT<float>*>(samp);

        int nd = fsamp->getDataLength();
        if (nd < 2) {
            samp->freeReference();
            continue;
        }

        dsm_time_t tt = fsamp->getTimeTag();

        const float* fptr = fsamp->getConstDataPtr();
        float cvtdiff = CVTdiff(fptr[0]);  // we may need to correct time below.

        // seconds of day timetag echoed back by LabView
        // The received value (which is what we actually sent to LabView)
        // does not roll back to 0 after 00:00 midnight, but keeps incrementing,
        // so we mod it here.
        double ttback = fmodf(cvtdiff, 86400.0f);

        SampleT<float>* outs = getSample<float>(nd);

        dsm_time_t tnew = _tt0 + (dsm_time_t)(ttback * USECS_PER_SEC);

        if (::llabs(tnew - tt) > USECS_PER_HALF_DAY) {
            _tt0 = tt - (tt % USECS_PER_DAY);
            tnew = _tt0 + (dsm_time_t)(ttback * USECS_PER_SEC);
            // correct for possibility that we could be off by a day here,
            // either the dsm or dsm_server gets restarted after 00:00 UTC
            if (tnew > tt + USECS_PER_HALF_DAY) _tt0 -= USECS_PER_DAY;
            else if (tnew < tt - USECS_PER_HALF_DAY) _tt0 += USECS_PER_DAY;
            tnew = _tt0 + (dsm_time_t)(ttback * USECS_PER_SEC);
        }

        // LabView can send back records with 0.0 for the seconds of day
        // when it is starting up.
        if (::llabs(tnew - tt) > 10 * USECS_PER_SEC) {
            tnew = tt;
            _tt0 = 0;
        }

        outs->setTimeTag(tnew);
        outs->setId(fsamp->getId());
        float* fptr2 = outs->getDataPtr();
        *fptr2++ = (float)(tt - tnew) / USECS_PER_SEC;
        for (int i = 1; i < nd; i++) *fptr2++ = fptr[i];
        results.push_back(outs);
        samp->freeReference();
    }
    return !results.empty();
}
