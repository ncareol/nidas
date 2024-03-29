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

#ifndef NIDAS_CORE_LOOPERCLIENT_H
#define NIDAS_CORE_LOOPERCLIENT_H

namespace nidas { namespace core {

/**
 * Interface of a client of Looper. An object that wants
 * a method called periodically should implement
 * LooperClient::looperNotify() and register itself with a
 * Looper via Looper::addClient().
 */
class LooperClient {
public:

  virtual ~LooperClient() {}

  /**
   * Method called by Looper. This method should not be a
   * heavy user of resources, since the notification of
   * other clients is delayed until this method finishes.
   * If much work is to be done, this method should
   * post a semaphore for another worker thread to proceed.
   */
  virtual void looperNotify() = 0;

};

}}	// namespace nidas namespace core

#endif
