// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2018, Copyright University Corporation for Atmospheric Research
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
#ifndef NIDAS_UTIL_SERIALPOWERCTRL_H
#define NIDAS_UTIL_SERIALPOWERCTRL_H

#include "XcvrGPIO.h"
#include "SysfsGpio.h"
#include "PowerCtrlIf.h"

namespace nidas { namespace util {

/*
 *  This class specializes PowerCtrlIF by providing a manual means to enable/disable power control
 */
class SensorPowerCtrl : public PowerCtrlIf
{
public:
    SensorPowerCtrl(GPIO_PORT_DEFS port);
    virtual ~SensorPowerCtrl()
    {
        DLOG(("SensorPowerCtrl::~SensorPowerCtrl(): destructing..."));
    }

    virtual void pwrOn();
    virtual void pwrOff();
    virtual void print()
    {
        std::cout << "Port" << _port << " ";
        _pPwrCtrl->print();
    }

    virtual bool ifaceAvailable() { return _pPwrCtrl ? _pPwrCtrl->ifaceAvailable() : true; }

    virtual void updatePowerState();
    virtual void enablePwrCtrl(bool enable)
    {
        if (_pPwrCtrl) {
            _pPwrCtrl->enablePwrCtrl(enable);
        }
    }

    virtual void setPower(POWER_STATE pwrState)
    {
        if (_pPwrCtrl) {
            _pPwrCtrl->setPower(pwrState);
        }
    }

    virtual void setPowerState(POWER_STATE pwrState)
    {
        if (_pPwrCtrl) {
            _pPwrCtrl->setPowerState(pwrState);
        }
    }

    virtual POWER_STATE getPowerState()
    {
        POWER_STATE retval = ILLEGAL_POWER;
        if (_pPwrCtrl) {
            retval = _pPwrCtrl->getPowerState();
        }
        return retval;
    }

    virtual void pwrReset(uint32_t pwrOnDelayMs=0, uint32_t pwrOffDelayMs=0)
    {
        if (_pPwrCtrl) {
            _pPwrCtrl->pwrReset(pwrOnDelayMs, pwrOffDelayMs);
        }
    }

    virtual bool pwrIsOn()
    {
        bool retval = false;
        if (_pPwrCtrl) {
            retval = _pPwrCtrl->pwrIsOn();
        }
        return retval;
    }

    virtual bool pwrCtrlEnabled()
    {
        bool retval = false;
        if (_pPwrCtrl) {
            retval = _pPwrCtrl->pwrCtrlEnabled();
        }
        return retval;
    }

    // This utility converts a binary power configuration to a string
    static const std::string rawPowerToStr(unsigned char powerCfg);
    // This utility converts a binary power configuration to state representation
    static POWER_STATE rawPowerToState(unsigned char powerCfg);

private:
    GPIO_PORT_DEFS _port;
    PowerCtrlIf* _pPwrCtrl;

    /*
     *  No copying
     */
    SensorPowerCtrl(const SensorPowerCtrl& rRight);
    SensorPowerCtrl(SensorPowerCtrl& rRight);
    const SensorPowerCtrl& operator=(const SensorPowerCtrl& rRight);
    SensorPowerCtrl & operator=(SensorPowerCtrl& rRight);

};

}} //namespace nidas { namespace util {

#endif //NIDAS_UTIL_POWERCTRLIF_H
