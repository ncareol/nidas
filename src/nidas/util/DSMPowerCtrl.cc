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

#include "DSMPowerCtrl.h"
#include "FtdiDSMPowerCtrl.h"
#include "Logger.h"


namespace nidas { namespace util {

DSMPowerCtrl::DSMPowerCtrl(GPIO_PORT_DEFS gpio)
: PowerCtrlIf(), _pPwrCtrl(0)
{
    DLOG(("Attempting to use FTDI Power Control..."));
    _pPwrCtrl = new FtdiDSMPowerCtrl(gpio);
    if (_pPwrCtrl) {
        if (!_pPwrCtrl->ifaceAvailable()) {
            DLOG(("FTDI Power Control not found, tear it down..."));
            delete _pPwrCtrl;
            _pPwrCtrl = 0;
        }
    }
    if (!_pPwrCtrl) {
        DLOG(("DSMPowerCtrl::DSMPowerCtrl(): Failed to instantiate FtdiDSMPowerCtrl object!!"));
        throw Exception("DSMPowerCtrl::DSMPowerCtrl()", "Failed to reserve memory for FtdiDSMPowerCtrl object.");
    }
    updatePowerState();
}


DSMPowerCtrl::~DSMPowerCtrl()
{
    DLOG(("DSMPowerCtrl::~DSMPowerCtrl(): destructing..."));
}

void DSMPowerCtrl::pwrOn()
{
    if (_pPwrCtrl) {
        _pPwrCtrl->pwrOn();
    }
    updatePowerState();
}

void DSMPowerCtrl::pwrOff()
{
    if (_pPwrCtrl) {
        _pPwrCtrl->pwrOff();
    }
    updatePowerState();
}

void DSMPowerCtrl::updatePowerState()
{
    if (_pPwrCtrl) {
        _pPwrCtrl->updatePowerState();
    }
}

void DSMPowerCtrl::print()
{
    if (_pPwrCtrl) {
        _pPwrCtrl->print();
    }
}

bool DSMPowerCtrl::ifaceAvailable()
{
    return _pPwrCtrl ? _pPwrCtrl->ifaceAvailable() : false;
}

void DSMPowerCtrl::enablePwrCtrl(bool enable)
{
    if (_pPwrCtrl) {
        _pPwrCtrl->enablePwrCtrl(enable);
    }
}

bool DSMPowerCtrl::pwrCtrlEnabled()
{
    return _pPwrCtrl ? _pPwrCtrl->pwrCtrlEnabled() : false;
}

void DSMPowerCtrl::setPower(POWER_STATE newPwrState)
{
    if (_pPwrCtrl) {
        _pPwrCtrl->setPower(newPwrState);
    }
}

void DSMPowerCtrl::setPowerState(POWER_STATE pwrState)
{
    if (_pPwrCtrl) {
        _pPwrCtrl->setPowerState(pwrState);
    }
}

POWER_STATE DSMPowerCtrl::getPowerState()
{
    POWER_STATE retval = ILLEGAL_POWER;
    if (_pPwrCtrl) {
        retval = _pPwrCtrl->getPowerState();
    }
    return retval;
}

void DSMPowerCtrl::pwrReset(uint32_t pwrOnDelayMs, uint32_t pwrOffDelayMs)
{
    if (_pPwrCtrl) {
        _pPwrCtrl->pwrReset(pwrOnDelayMs, pwrOffDelayMs);
    }
}

bool DSMPowerCtrl::pwrIsOn()
{
    bool retval = false;
    if (_pPwrCtrl) {
        retval = _pPwrCtrl->pwrIsOn();
    }
    return retval;
}


}} //namespace nidas { namespace util {
