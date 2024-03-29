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

#include "ParoSci_202BG_Calibration.h"
#include <nidas/util/Logger.h>
#include <nidas/core/PhysConstants.h>
#include <nidas/core/VariableConverter.h>

using namespace nidas::dynld;
using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

ParoSci_202BG_Calibration::ParoSci_202BG_Calibration():
    _U0(floatNAN),_b(floatNAN),_P0(floatNAN)
{
    for (unsigned int i = 0; i < sizeof(_Y)/sizeof(_Y[0]); i++)
       _Y[i] = floatNAN;
    for (unsigned int i = 0; i < sizeof(_C)/sizeof(_C[0]); i++)
        _C[i] = floatNAN;
    for (unsigned int i = 0; i < sizeof(_D)/sizeof(_D[0]); i++)
        _D[i] = floatNAN;
    for (unsigned int i = 0; i < sizeof(_T)/sizeof(_T[0]); i++)
        _T[i] = floatNAN;
}

void ParoSci_202BG_Calibration::readCalFile(CalFile* cf, dsm_time_t tt)
{
    // Read CalFile of calibration parameters.
    while(tt >= cf->nextTime().toUsecs()) {
        float d[18];
        try {
            n_u::UTime calTime;
            int n = cf->readCF(calTime, d,sizeof d/sizeof(d[0]));
            if (n > 0) setU0(d[0]);
            if (n > 3) setYs(0.0,d[1],d[2],d[3]);
            if (n > 6) setCs(d[4]*MBAR_PER_KPA,d[5]*MBAR_PER_KPA,d[6]*MBAR_PER_KPA);
            if (n > 8) setDs(d[7],d[8]);
            if (n > 13) setTs(d[9],d[10],d[11],d[12],d[13]);
            if (n > 17) setCommonModeCoefs(d[14],d[15]/MBAR_PER_KPA,
                d[16]/MBAR_PER_KPA,d[17]*MBAR_PER_KPA);
        }
        catch(const n_u::EOFException& e)
        {
        }
        catch(const n_u::Exception& e)
        {
            n_u::Logger::getInstance()->log(LOG_WARNING,"%s: %s",
                cf->getCurrentFileName().c_str(),e.what());
            setU0(floatNAN);
            setYs(floatNAN,floatNAN,floatNAN,floatNAN);
            setCs(floatNAN,floatNAN,floatNAN);
            setDs(floatNAN,floatNAN);
            setTs(floatNAN,floatNAN,floatNAN,floatNAN,floatNAN);
            throw e;
        }
    }
}

double ParoSci_202BG_Calibration::computeTemperature(double usec)
{
    // cerr << "usec=" << usec << " _Y=" << _Y[0] << ' ' << _Y[1] << ' ' << _Y[2] << ' ' << _Y[3] << endl;
    return Polynomial::eval(usec,_Y,sizeof(_Y)/sizeof(_Y[0]));
}

double ParoSci_202BG_Calibration::computePressure(double tper, double pper)
{
    double C = Polynomial::eval(tper,_C,sizeof(_C)/sizeof(_C[0]));
    double D = Polynomial::eval(tper,_D,sizeof(_D)/sizeof(_D[0]));
    double T0 = Polynomial::eval(tper,_T,sizeof(_T)/sizeof(_T[0]));
    double Tfact = (1.0 - T0 * T0 / pper / pper);
    double p = C * Tfact * (1.0 - D * Tfact);
    return p;
}

double ParoSci_202BG_Calibration::correctPressure(double pgauge, double pstatic)
{
    double pd = pstatic - _P0;
    double pcorr = pgauge + _a[0] * pd + _a[1] * pd * pd + _b * pd * pgauge;
    return pcorr;
}
