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

#include "UHSAS_Serial.h"
#include <nidas/core/PhysConstants.h>
#include <nidas/core/Parameter.h>
#include <nidas/core/SampleTag.h>
#include <nidas/core/Variable.h>
#include <nidas/core/VariableConverter.h>
#include <nidas/util/Logger.h>
#include <nidas/util/UTime.h>
#include <nidas/util/IOTimeoutException.h>

#include <sstream>
#include <iomanip>

using namespace nidas::core;
using namespace nidas::dynld::raf;
using namespace std;

namespace n_u = nidas::util;

NIDAS_CREATOR_FUNCTION_NS(raf,UHSAS_Serial)

const n_u::EndianConverter* UHSAS_Serial::fromLittle = n_u::EndianConverter::getConverter(n_u::EndianConverter::EC_LITTLE_ENDIAN);

static const unsigned char setup_pkt[] =
	{
	0xff, 0xff, 0x10, 0xff, 0xff, 0x0e, 0x01, 0xff, 0xff, 0x0a, 0x04,
	0xff, 0xff, 0x0b, 0x00, 0x19, 0xff, 0xff, 0x18, 0xfa, 0x00, 0xff,
	0xff, 0x1c, 0x88, 0x13, 0xff, 0xff, 0x19, 0x32, 0x00, 0xff, 0xff,
	0xff, 0x1d, 0x88, 0x13, 0xff, 0xff, 0x1a, 0xfa, 0x00, 0xff, 0xff,
	0x1e, 0x88, 0x13, 0xff, 0xff, 0x1b, 0x32, 0x00, 0xff, 0xff, 0x1f,
	0xc4, 0x09, 0xff, 0xff, 0x08, 0x9a, 0x77, 0xff, 0xff, 0x08, 0xa7,
	0x35, 0xff, 0xff, 0x08, 0x22, 0x22, 0xff, 0xff, 0x08, 0x00, 0x00,
	0xff, 0xff, 0x08, 0xec, 0x11, 0xff, 0xff, 0x0d, 0x03, 0xff, 0xff,
	0x0c, 0x01, 0xff, 0xff, 0x09, 0x00, 0xff, 0xff, 0x14,

	0x00, 0x00, 0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0x41,
	0x00, 0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0x41, 0x00, 0x41, 0x00,
	0x42, 0x00, 0x42, 0x00, 0x42, 0x00, 0x42, 0x00, 0x42, 0x00, 0x42,
	0x00, 0x42, 0x00, 0x43, 0x00, 0x43, 0x00, 0x43, 0x00, 0x44, 0x00,
	0x44, 0x00, 0x45, 0x00, 0x45, 0x00, 0x46, 0x00, 0x47, 0x00, 0x48,
	0x00, 0x49, 0x00, 0x4a, 0x00, 0x4c, 0x00, 0x4e, 0x00, 0x50, 0x00,
	0x53, 0x00, 0x56, 0x00, 0x59, 0x00, 0x5d, 0x00, 0x62, 0x00, 0x68,
	0x00, 0x6e, 0x00, 0x76, 0x00, 0x7f, 0x00, 0x89, 0x00, 0x96, 0x00,
	0xa4, 0x00, 0xb5, 0x00, 0xc8, 0x00, 0xdf, 0x00, 0xfa, 0x00, 0x1a,
	0x01, 0x3f, 0x01, 0x6a, 0x01, 0x9c, 0x01, 0xd7, 0x01, 0x1c, 0x02,
	0x69, 0x02, 0xc0, 0x02, 0x25, 0x03, 0x98, 0x03, 0x1b, 0x04, 0xaf,
	0x04, 0x57, 0x05, 0x15, 0x06, 0xe8, 0x06, 0xd5, 0x07, 0xdc, 0x08,
	0xfe, 0x09, 0x3c, 0x0b, 0x99, 0x0c, 0x15, 0x0e, 0xb4, 0x0f, 0x75,
	0x11, 0x5f, 0x13, 0x71, 0x15, 0xb4, 0x17, 0x2e, 0x1a, 0xe3, 0x1c,
	0xd9, 0x1f, 0x0e, 0x23, 0x7a, 0x26, 0x08, 0x2a, 0x9b, 0x2d, 0x10,
	0x31, 0x52, 0x34, 0x44, 0x37, 0xf2, 0x39, 0x65, 0x3c, 0xb4, 0x3e,
	0xf5, 0x40, 0x44, 0x43, 0xc2, 0x45, 0xa0, 0x48, 0x28, 0x4c, 0xac,
	0x50, 0x3d, 0x56, 0x69, 0x5c, 0x55, 0x62, 0x08, 0x67, 0x80, 0x6a,
	0x9d, 0x6c, 0xb3, 0x6d, 0xb3, 0x6d,
// Block 2.
	0xff, 0xff, 0x0b, 0x00, 0x19, 0xff, 0xff, 0x18, 0xfa, 0x00, 0xff,
	0xff, 0x1c, 0x88, 0x13, 0xff, 0xff, 0x19, 0x32, 0x00, 0xff, 0xff,
	0x1d, 0x88, 0x13, 0xff, 0xff, 0x1a, 0xfa, 0x00, 0xff, 0xff, 0x1e,
	0x88, 0x13, 0xff, 0xff, 0x1b, 0x32, 0x00, 0xff, 0xff, 0x1f, 0xc4,
	0x09, 0xff, 0xff, 0x08, 0x9a, 0x77, 0xff, 0xff, 0x08, 0xa7, 0x35,
	0xff, 0xff, 0x08, 0x22, 0x22, 0xff, 0xff, 0x08, 0x00, 0x00, 0xff,
	0xff, 0x08, 0xec, 0x11, 0xff, 0xff, 0x0d, 0x00, 0xff, 0xff, 0x0c,
	0x01, 0xff, 0xff, 0x09, 0x00, 0xff, 0xff, 0x15,

	0x00, 0x00, 0x7a, 0x00, 0x7a, 0x00, 0x7b, 0x00, 0x7b, 0x00, 0x7c,
	0x00, 0x7c, 0x00, 0x7d, 0x00, 0x7e, 0x00, 0x80, 0x00, 0x81, 0x00,
	0x83, 0x00, 0x85, 0x00, 0x87, 0x00, 0x8a, 0x00, 0x8d, 0x00, 0x90,
	0x00, 0x95, 0x00, 0x9a, 0x00, 0xa0, 0x00, 0xa6, 0x00, 0xae, 0x00,
	0xb8, 0x00, 0xc3, 0x00, 0xd0, 0x00, 0xdf, 0x00, 0xf0, 0x00, 0x05,
	0x01, 0x1d, 0x01, 0x39, 0x01, 0x5a, 0x01, 0x80, 0x01, 0xad, 0x01,
	0xe2, 0x01, 0x20, 0x02, 0x68, 0x02, 0xbc, 0x02, 0x1f, 0x03, 0x92,
	0x03, 0x1a, 0x04, 0xb8, 0x04, 0x71, 0x05, 0x49, 0x06, 0x47, 0x07,
	0x6f, 0x08, 0xca, 0x09, 0x5f, 0x0b, 0x3a, 0x0d, 0x65, 0x0f, 0xee,
	0x11, 0xe6, 0x14, 0x60, 0x18, 0x6f, 0x1c, 0x30, 0x21, 0xc0, 0x26,
	0xe5, 0x2c, 0xf6, 0x33, 0x0d, 0x3c, 0x4f, 0x45, 0xdb, 0x4f, 0xd5,
	0x5b, 0x5e, 0x69, 0x99, 0x78, 0xaa, 0x89, 0xb7, 0x9c, 0xdf, 0xb1,
	0x39, 0xc9, 0xe1, 0xe2, 0xfb, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
// Block 3.
	0xff, 0xff, 0x0b, 0x00, 0x19, 0xff, 0xff, 0x18, 0xfa, 0x00, 0xff,
	0xff, 0x1c, 0x88, 0x13, 0xff, 0xff, 0x19, 0x32, 0x00, 0xff, 0xff,
	0x1d, 0x88, 0x13, 0xff, 0xff, 0x1a, 0xfa, 0x00, 0xff, 0xff, 0x1e,
	0x88, 0x13, 0xff, 0xff, 0x1b, 0x32, 0x00, 0xff, 0xff, 0x1f, 0xc4,
	0x09, 0xff, 0xff, 0x08, 0x9a, 0x77, 0xff, 0xff, 0x08, 0xa7, 0x35,
	0xff, 0xff, 0x08, 0x22, 0x22, 0xff, 0xff, 0x08, 0x00, 0x00, 0xff,
	0xff, 0x08, 0xec, 0x11, 0xff, 0xff, 0x0d, 0x00, 0xff, 0xff, 0x0c,
	0x01, 0xff, 0xff, 0x09, 0x00, 0xff, 0xff, 0x16,

	0x00, 0x00, 0x5b, 0x00, 0x64, 0x00, 0x6f, 0x00, 0x7b, 0x00, 0x89,
	0x00, 0x9a, 0x00, 0xad, 0x00, 0xc4, 0x00, 0xdf, 0x00, 0xfe, 0x00,
	0x23, 0x01, 0x4e, 0x01, 0x80, 0x01, 0xbb, 0x01, 0xff, 0x01, 0x4f,
	0x02, 0xad, 0x02, 0x1b, 0x03, 0x9c, 0x03, 0x32, 0x04, 0xe2, 0x04,
	0xb0, 0x05, 0xa1, 0x06, 0xbb, 0x07, 0x05, 0x09, 0x87, 0x0a, 0x4a,
	0x0c, 0x5b, 0x0e, 0xc5, 0x10, 0x97, 0x13, 0xe5, 0x16, 0xc4, 0x1a,
	0x48, 0x1f, 0x92, 0x24, 0xc5, 0x2a, 0x04, 0x32, 0x7e, 0x3a, 0x69,
	0x44, 0x01, 0x50, 0x91, 0x5d, 0x75, 0x6d, 0x08, 0x80, 0xcc, 0x95,
	0x34, 0xaf, 0xfd, 0xcc, 0xc4, 0xef, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
	0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
	0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
// Block 4.
        0xff, 0xff, 0x0b, 0x00, 0x19, 0xff, 0xff, 0x18, 0xfa, 0x00, 0xff,
        0xff, 0x1c, 0x88, 0x13, 0xff, 0xff, 0x19, 0x32, 0x00, 0xff, 0xff,
        0x1d, 0x88, 0x13, 0xff, 0xff, 0x1a, 0xfa, 0x00, 0xff, 0xff, 0x1e,
        0x88, 0x13, 0xff, 0xff, 0x1b, 0x32, 0x00, 0xff, 0xff, 0x1f, 0xc4,
        0x09, 0xff, 0xff, 0x08, 0x9a, 0x77, 0xff, 0xff, 0x08, 0xa7, 0x35,
        0xff, 0xff, 0x08, 0x22, 0x22, 0xff, 0xff, 0x08, 0x00, 0x00, 0xff,
        0xff, 0x08, 0xec, 0x11, 0xff, 0xff, 0x0d, 0x00, 0xff, 0xff, 0x0c,
        0x01, 0xff, 0xff, 0x09, 0x00, 0xff, 0xff, 0x17,


        0x00, 0x00, 0x06, 0x0a, 0xbb, 0x0b, 0xb9, 0x0d, 0x0c, 0x10, 0xc6,
        0x12, 0xf8, 0x15, 0xb6, 0x19, 0x12, 0x1e, 0x30, 0x23, 0x2a, 0x29,
        0x2d, 0x30, 0x5a, 0x38, 0xed, 0x41, 0x2c, 0x4d, 0x45, 0x5a, 0xa0,
        0x69, 0x8e, 0x7b, 0x8b, 0x90, 0x18, 0xa9, 0xe5, 0xc5, 0x8c, 0xe7,
        0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
        0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
        0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
        0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
        0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
        0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
        0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
        0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
        0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
        0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
        0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
        0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
        0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff,
        0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe,
        0xff, 0xfe, 0xff, 0xfe, 0xff, 0xfe
	};

UHSAS_Serial::UHSAS_Serial():
    DSMSerialSensor(),_noutValues(0),_nValidChannels(0),_nHousekeep(0),
    _hkScale(),_binary(true),_sampleRate(0.0),
    _sendInitBlock(false),_nOutBins(100),_sumBins(false),
    _nDataErrors(0),_dtUsec(USECS_PER_SEC),
    _nstitch(0),_largeHistograms(0),_totalHistograms(0),
    _converters()
{
    for (unsigned int i = 0; i < sizeof(_hkScale)/sizeof(_hkScale[0]); i++)
        _hkScale[i] = 0.0;
}

UHSAS_Serial::~UHSAS_Serial()
{
    cerr << "UHSAS_Serial: " << getName() << " #histograms=" << _totalHistograms <<
            ", #large=" << _largeHistograms << ", #stitch=" << _nstitch << ", #errors=" << _nDataErrors << endl;
}


void UHSAS_Serial::open(int flags)
{
    const Parameter *p;
    p = getParameter("sendInit");
    if (p) {
        if (p->getLength() != 1)
        throw n_u::InvalidParameterException(getName(),
              "sendInit","should have length=1");
        setSendInitBlock((int)p->getNumericValue(0));
    }

    SerialSensor::open(flags);
}


void UHSAS_Serial::init()
{
    CharacterSensor::init();

    _nHousekeep = 9;	// 9 of the available 12 are used.
    _nValidChannels = 99;	// 99 valid channels (100 total).

    const std::list<AsciiSscanf*>& sscanfers = getScanfers();
    if (!sscanfers.empty()) {
        _binary = false;
        _nHousekeep = 13;	// 13 given by CU UHSAS.
        return;
    }


    const Parameter *p;

    // Get Housekeeping scale factors.
    p = getParameter("HSKP_SCALE");
    if (!p) throw n_u::InvalidParameterException(getName(),
          "HSKP_SCALE","not found");
    if (p->getLength() != 12)
        throw n_u::InvalidParameterException(getName(),
              "HSKP_SCALE","not 12 long ");
    for (int i = 0; i < p->getLength(); ++i)
        _hkScale[i] = p->getNumericValue(i);

    // Determine number of floats we will recieve (_noutValues)
    list<SampleTag*>& stags = getSampleTags();
    if (stags.size() != 1)
          throw n_u::InvalidParameterException(getName(),"sample",
              "must be one <sample> tag for this sensor");

    _noutValues = 0;
    _nOutBins = 0;
    list<SampleTag*>::const_iterator ti = stags.begin();

    for (; ti != stags.end() ; ++ti)
    {
        SampleTag* stag = *ti;
        _sampleRate = stag->getRate();
        _dtUsec = (int)rint(USECS_PER_SEC / _sampleRate);

//        dsm_sample_id_t sampleId = stag->getId();

        const vector<Variable*>& vars = stag->getVariables();
        vector<Variable*>::const_iterator vi = vars.begin();
        for ( ; vi != vars.end(); ++vi)
        {
            Variable* var = *vi;
            // first variable is the histogram array
            if (!_noutValues) _nOutBins = var->getLength();
            _noutValues += var->getLength();

            if (getApplyVariableConversions())
                _converters.push_back(var->getConverter());
        }
    }

    /*
     * We'll be adding a bogus zeroth bin to the data to match historical
     * behavior. Remove all traces of this after the netCDF file refactor.
     */
    /*
     * This logic should match what is in ::process, so that
     * an output sample of the correct size is created.
     */

    int nextra = _nOutBins - _nValidChannels;
    if (nextra != 0 && nextra != 1) {
        ostringstream ost;
        ost << "length of first (bins) variable=" << _nOutBins << ". Should be " << _nValidChannels << " or " << _nValidChannels + 1;
        throw n_u::InvalidParameterException(getName(),"sample",ost.str());
    }

    // CVI processor wants to have a sum of the bins. If the user asks for
    // one more variable, we'll assume it is the sum, which must be right
    // after the histogram variable.
    _sumBins = false;
    if (_noutValues == _nOutBins + _nHousekeep + 1) _sumBins = true;

    int ncheck = _nOutBins + _nHousekeep + (int)_sumBins;
    if (_noutValues != ncheck) {
        ostringstream ost;
        ost << "total length of variables should be " << ncheck;
        throw n_u::InvalidParameterException(getName(),"sample",ost.str());
    }
}

void UHSAS_Serial::sendInitString()
{
    if (!getSendInitBlock()) return;
    ILOG(("sending Init block"));
    // clear whatever junk may be in the buffer til a timeout
    try {
        for (;;) {
            readBuffer(MSECS_PER_SEC / 100);
            clearBuffer();
        }
    }
    catch (const n_u::IOTimeoutException& e) {}

    n_u::UTime twrite;
    write(setup_pkt, sizeof(setup_pkt));

    // UHSAS starts sending data.  So read first sample
    // to see that things are OK

    // read with a timeout in milliseconds. Throws n_u::IOTimeoutException
    readBuffer(MSECS_PER_SEC * 1);

    Sample* samp = nextSample();
    if (!samp)
        throw n_u::IOException(getName(),"first sample", "not read.");
    samp->freeReference();
}

/* static */
unsigned const char* UHSAS_Serial::findMarker(unsigned const char* ip,unsigned const char* eoi,
    unsigned char* marker, int len)
{
    // scan for marker
    for (; ; ip++) {
        for ( ; ip < eoi && *ip != marker[0]; ip++);
        if (ip + len > eoi) break;
        if (!memcmp(ip, marker, len)) break;
    }
    if (ip + len > eoi) return 0;
    return ip + len;
}

bool UHSAS_Serial::process(const Sample* samp,list<const Sample*>& results)
{
    static unsigned char marker0[] = { 0xff, 0xff, 0x00 };
    static unsigned char marker1[] = { 0xff, 0xff, 0x01 };
    static unsigned char marker2[] = { 0xff, 0xff, 0x02 };  // start of stitch data
    static unsigned char marker3[] = { 0xff, 0xff, 0x03 };  // end of stitch data
    static unsigned char marker4[] = { 0xff, 0xff, 0x04 };  // start of  histogram
    static unsigned char marker5[] = { 0xff, 0xff, 0x05 };  // end of  histogram
    static unsigned char marker6[] = { 0xff, 0xff, 0x06 };  // start of housekeeping
    static unsigned char marker7[] = { 0xff, 0xff, 0x07 };  // end of housekeeping

    const unsigned char * input = (unsigned char *) samp->getConstVoidDataPtr();
    unsigned nbytes = samp->getDataByteLength();

    const unsigned char* ip = input;
    const unsigned char* eoi = input + nbytes;

    const int LOG_MSG_DECIMATE = 10;

    list<Sample*> osamps;

    if (!_binary) {
        SerialSensor::process(samp, results);

        if (results.empty()) return false;

        const Sample* psamp = results.front();

        // now figure out what elements of pdata contain the histogram
        // don't go past nvals though...  You could hard-code things, or
        // In validate() method you could loop over the variables in the sample
        // checking their names to figure out what might be the first and last bin.

        const float* pdata = (const float*) psamp->getConstVoidDataPtr();
        double sum = 0.0;
        int nvals = psamp->getDataLength();
        for (int i = 0; i < _nValidChannels; i++) {
            if (i + _nHousekeep < nvals) {
                float val = pdata[i + _nHousekeep];
                if (! isnan(val)) sum += val;
            }
        }

        SampleT<float> * outs = getSample<float>(1);
        outs->setTimeTag(psamp->getTimeTag());
        outs->setId(psamp->getId() + 1);
        float* dout = outs->getDataPtr();
        *dout  = sum;

        results.push_back(outs);            // TCNT
        return true;
    }

    // If there is more than one sample, then the UHSAS wasn't sending out the
    // ffff00 beginning-of-message separator, and NIDAS has concat'd a bunch
    // of samples together into an 8190 byte glob.  Unpack all the samples
    // we find.
    // It appears that a UHSAS can get in a mode where it sends a ffff01 (TOC)
    // beginning-of-message separator rather than ffff00 (TIC).  A solution
    // appears to be to search for a ffff07 end-of-message separator, which
    // I suggest we do for projects after PREDICT.
    for (; ip < eoi; ) {

        const unsigned char* mk = findMarker(ip,eoi,marker0,sizeof(marker0));
        if (!mk) {
            mk = findMarker(ip,eoi,marker1,sizeof(marker1));
            if (mk && !(_nDataErrors++ % LOG_MSG_DECIMATE))
                WLOG(("UHSAS: ") << getName() << ": " <<
                    n_u::UTime(samp->getTimeTag()).format(true,"%H:%M:%S.%3f") <<
                        " Obsolete TOC marker (ffff01) found.");
        }

        if (!mk) {
            if (ip == input && !(_nDataErrors++ % LOG_MSG_DECIMATE))
                WLOG(("UHSAS: ") << getName() << ": " <<
                        n_u::UTime(samp->getTimeTag()).format(true,"%H:%M:%S.%3f") <<
                        " Start TIC/TOC marker (ffff00/ffff01) not found. #errors=" << _nDataErrors);
            break;
        }
        ip = mk;

        // check for stitch data: ffff02...ffff03
        bool stitch = false;
        if (ip + sizeof(marker2) <= eoi && !memcmp(ip, marker2, sizeof(marker2))) {
            ip += sizeof(marker2);
            mk = findMarker(ip,eoi,marker3,sizeof(marker3));
            if (mk) {
                // check if the stitch section actually has values.
                if (mk - ip > (signed)sizeof(marker3)) {
                    stitch = true;
                    _nstitch++;
                }
                ip = mk;
            }
            else if (!(_nDataErrors++ % LOG_MSG_DECIMATE))
                WLOG(("UHSAS: ") << getName() << ": " <<
                    n_u::UTime(samp->getTimeTag()).format(true,"%H:%M:%S.%3f") <<
                      " Stitch end marker (ffff03) not found. #errors=" << _nDataErrors);
        }

        // start of histogram, ffff04
        mk = findMarker(ip,eoi,marker4,sizeof(marker4));
        if (!mk) {
            // When the UHSAS puts out stitch data (ffff02...ffff03) there may not be
            // histograms. If we find stitch data instead, don't log an error.
            if (stitch) continue;
            if (!(_nDataErrors++ % LOG_MSG_DECIMATE))
                WLOG(("UHSAS: ") << getName() << ": " <<
                    n_u::UTime(samp->getTimeTag()).format(true,"%H:%M:%S.%3f") <<
                      " Histogram start marker (ffff04) not found. #errors=" << _nDataErrors);
            break;
        }
        ip = mk;

        // There are 100 2-byte histogram values (but apparently only 99 valid ones).
        int nbyteBins = (_nValidChannels + 1) * sizeof(short);

        if (eoi - ip < nbyteBins) {
            if (!(_nDataErrors++ % LOG_MSG_DECIMATE))
                WLOG(("UHSAS: ") << getName() << ": Short data block. #errors=" << _nDataErrors);
            break;
        }
        const unsigned char* histoPtr = ip;
        ip += nbyteBins;

        if (ip + sizeof(marker5) > eoi || memcmp(ip, marker5, sizeof(marker5))) {

            // saw data during PREDICT where the histogram end marker was 1 or 2 bytes
            // later than expected, resulting in a 201 or 202 byte histogram, instead
            // of 200 bytes.
            //
            // How should we handle the large histograms?
            //  1. discard them
            //  2. use the leading 200 bytes, ignoring the extra
            //  3. assume the leading 1 or 2 bytes are extraneous, skip those bytes
            //     and use the last 200.
            //
            //  Currently this code does #2.

            mk = findMarker(ip,eoi,marker5,sizeof(marker5));
            if (!mk) {
                if (!(_nDataErrors++ % LOG_MSG_DECIMATE))
                    WLOG(("UHSAS: ") << getName() << ": " <<
                        n_u::UTime(samp->getTimeTag()).format(true,"%H:%M:%S.%3f") <<
                            " Histogram end marker (ffff05) not found. #errors=" << _nDataErrors);
                break;
            }
            else {
                ip = mk;
                if (!(_largeHistograms++ % LOG_MSG_DECIMATE))
                    WLOG(("UHSAS: ") << getName() << ": " <<
                        n_u::UTime(samp->getTimeTag()).format(true,"%H:%M:%S.%3f") <<
                            " Histogram length=" << (long)(ip - histoPtr - sizeof(marker5)) << " bytes, expected " << nbyteBins);
                continue;
            }
        }
        else ip += sizeof(marker5);

        _totalHistograms++;

        ip = findMarker(ip,eoi,marker6,sizeof(marker6));
        if (!ip) {
            if (!(_nDataErrors++ % LOG_MSG_DECIMATE))
                WLOG(("UHSAS: ") << getName() << ": " <<
                    n_u::UTime(samp->getTimeTag()).format(true,"%H:%M:%S.%3f") <<
                        " Housekeeping start marker (ffff06) not found. #errors=" << _nDataErrors);
            break;
        }

        const unsigned char* housePtr = ip;
        ip = findMarker(ip,eoi,marker7,sizeof(marker7));
        if (!ip) {
            if (!(_nDataErrors++ % LOG_MSG_DECIMATE))
                WLOG(("UHSAS: ") << getName() << ": " <<
                    n_u::UTime(samp->getTimeTag()).format(true,"%H:%M:%S.%3f") <<
                        " End marker (ffff07) not found. #errors=" << _nDataErrors);
            break;
        }
        int nhouse = (ip - sizeof(marker7) - housePtr) / sizeof(short);

        SampleT<float> * outs = getSample<float>(_noutValues);
        outs->setTimeTag(samp->getTimeTag() + (results.size() * _dtUsec) - getLagUsecs());
        outs->setId(getId() + 1);
        float * dout = outs->getDataPtr();

        // variable index within the sample, used to get the VariableConverters
        unsigned int ivar = 0;

        // Pull out histogram data.
        // If user asked for 100 values, add a bogus zeroth bin for historical reasons
        if (_nOutBins == _nValidChannels + 1) *dout++ = 0.0;

        int sum = 0;
        // UHSAS puts out largest bins first
        for (int iout = _nValidChannels-1; iout >= 0; --iout) {
           int c = fromLittle->uint16Value(histoPtr);
           histoPtr += sizeof(short);
           sum += c;
           dout[iout] = (float)c;
        }
        dout += _nValidChannels;
        ivar++;

        if (_sumBins) {
            *dout++ = sum * _sampleRate;  // counts/sec
            ivar++;
        }
        // cerr << "sum=" << sum << " _sumBins=" << _sumBins << " _noutBins=" << _nOutBins << " _noutValues=" << _noutValues << " _sampleRate=" << _sampleRate << endl;

        // 12 housekeeping values, of which we unpack 9, skipping #8, #10 and #11.
        // These values must correspond to the sequence of
        // <variable> tags in the <sample> for this sensor.

        // cerr << "house=";
        for (int iout = 0; iout < nhouse; ++iout) {
            if (iout != 8 && iout < 10) {
                int c = fromLittle->uint16Value(housePtr);
                // cerr << setw(6) << c;
                double d = (float)c / _hkScale[iout];

                // apply calibration
                if (!_converters.empty()) {
                    assert(ivar < _converters.size());
                    if (_converters[ivar]) d = _converters[ivar]->convert(outs->getTimeTag(),d);
                }
                *dout++ = d;
                ivar++;
            }
            housePtr += sizeof(short);
        }
        for (; dout < outs->getDataPtr() + _noutValues; ) *dout++ = floatNAN;
        // cerr << endl;

        // check for under/overflow.
        assert(dout - outs->getDataPtr() == _noutValues);
        results.push_back(outs);
        osamps.push_back(outs);
    }

    // if there is more than one sample, try to fix up the timetag.
    if (results.size() > 1) {
        list<Sample*>::iterator ri = osamps.begin();
        for ( ; ri != osamps.end(); ++ri) {
            Sample* osamp = *ri;
            osamp->setTimeTag(osamp->getTimeTag() - (results.size() * _dtUsec) - getLagUsecs());
        }
    }
    return !results.empty();
}
