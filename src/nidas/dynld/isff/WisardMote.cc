/*
    Copyright 2009 UCAR, NCAR, All Rights Reserved

    $LastChangedDate:  $

    $LastChangedRevision:  $

    $LastChangedBy: dongl $

    $HeadURL: http://svn.eol.ucar.edu/svn/nidas/trunk/src/nidas/dynld/isff/WisardMote.h $

 */

#include "WisardMote.h"
#include <nidas/util/Logger.h>
#include <nidas/core/DSMSensor.h>
#include <nidas/core/DSMConfig.h>
#include <cmath>
#include <iostream>
#include <memory> // auto_ptr<>

using namespace nidas::dynld;
using namespace nidas::dynld::isff;
using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

/* static */
bool WisardMote::_functionsMapped = false;

/* static */
std::map<unsigned char, WisardMote::readFunc> WisardMote::_nnMap;

/* static */
const n_u::EndianConverter* WisardMote::_fromLittle =
	n_u::EndianConverter::getConverter(n_u::EndianConverter::EC_LITTLE_ENDIAN);


NIDAS_CREATOR_FUNCTION_NS(isff,WisardMote)


WisardMote::WisardMote():
	_moteId(-1),_version(-1),_badCRCs(0)
	{
	initFuncMap();
	}

bool WisardMote::process(const Sample* samp,list<const Sample*>& results) throw()
{
	/* unpack a WisardMote packet, consisting of binary integer data from a variety
	 * of sensor types. */
	const unsigned char* cp= (const unsigned char*) samp->getConstVoidDataPtr();
	const unsigned char* eos = cp + samp->getDataByteLength();

	/*  check for good EOM  */
	if (!(eos = checkEOM(cp,eos))) return false;

	/*  verify crc for data  */
	if (!(eos = checkCRC(cp,eos))) {
		if (!(_badCRCs++ % 100)) WLOG(("%s: %d bad CRCs",getName().c_str(),_badCRCs));
		return false;
	}

	/*  read header */
	int mtype = readHead(cp, eos);
	if (mtype == -1) return false;  // invalid

	if (mtype != 1) return false;   // other than a data message

	while (cp < eos) {

		/* get sensor type id    */
		unsigned char sensorTypeId = *cp++;

		DLOG(("%s: moteId=%d, sensorid=%x, sensorTypeId=%x, time=",
				getName().c_str(),_moteId, getSensorId(), sensorTypeId) <<
				n_u::UTime(samp->getTimeTag()).format(true,"%c"));

		_data.clear();

		/* find the appropriate member function to unpack the data for this sensorTypeId */
		readFunc func = _nnMap[sensorTypeId];
		if (func == NULL) {
			WLOG(("%s: moteId=%d: no read data function for sensorTypeId=%x",
					getName().c_str(),_moteId, sensorTypeId));
			continue;
		}

		/* unpack the data for this sensorTypeId */
		cp = (this->*func)(cp,eos);

		/* create an output floating point sample */
		if (_data.size() == 0) 	continue;

		SampleT<float>* osamp = getSample<float>(_data.size());
		osamp->setTimeTag(samp->getTimeTag());
		osamp->setId(getId()+(_moteId << 8) + sensorTypeId);
		float* dout = osamp->getDataPtr();

		std::copy(_data.begin(),_data.end(),dout);
#ifdef DEBUG
		for (unsigned int i=0; i<_data.size(); i++) {
			DLOG(("data[%d]=%f",i, _data[i]));
		}
#endif
		/* push out */
		results.push_back(osamp);
	}
	return true;
}

void WisardMote::addSampleTag(SampleTag* stag) throw(n_u::InvalidParameterException) {
	for (int i = 0; ; i++)
	{
		unsigned int id = _samps[i].id;
		if (id == 0) break;

		SampleTag* newtag = new SampleTag(*stag);
		newtag->setSampleId(newtag->getSampleId()+id);
		int nv = sizeof(_samps[i].variables)/sizeof(_samps[i].variables[0]);

		//vars
		int len=1;
		for (int j = 0; j < nv; j++) {
			VarInfo vinf = _samps[i].variables[j];
			if (!vinf.name || sizeof(vinf.name)<=0 ) break;
			Variable* var = new Variable();
			var->setName(vinf.name);
			var->setUnits(vinf.units);
			var->setLongName(vinf.longname);
			var->setDynamic(vinf.dynamic);
			var->setLength(len);
			var->setSuffix(newtag->getSuffix());

			newtag->addVariable(var);
		}
		//add this new sample tag
		DSMSerialSensor::addSampleTag(newtag);
	}
	//delete old tag
	delete stag;
}

/**
 * read mote id, version.
 * return msgType: -1=invalid header, 0 = sensortype+SN, 1=seq+time+data,  2=err msg
 */
int WisardMote::readHead(const unsigned char* &cp, const unsigned char* eos)
{
	_moteId=0;

	/* look for mote id. First skip non-digits. */
	for ( ; cp < eos; cp++) if (::isdigit(*cp)) break;
	if (cp == eos) return -1;

	const unsigned char* colon = (const unsigned char*)::memchr(cp,':',eos-cp);
	if (!colon) return -1;

	// read the moteId
	string idstr((const char*)cp,colon-cp);
	{
		stringstream ssid(idstr);
		ssid >> std::dec >> _moteId;
		if (ssid.fail()) return -1;
	}

	DLOG(("idstr=%s moteId=$i", idstr.c_str(), _moteId));

	cp = colon + 1;

	// version number
	if (cp == eos) return -1;
	_version = *cp++;

	// message type
	if (cp == eos) return -1;
	int mtype = *cp++;

	switch(mtype) {
	case 0:
		/* unpack 1bytesId + 16 bit s/n */
		if (cp + 1 + sizeof(uint16_t) > eos) return false;
		{
			int sensorTypeId = *cp++;
			int serialNumber = *cp++;
			_sensorSerialNumbersByType[sensorTypeId] = serialNumber;
			DLOG(("mote=%s, id=%d, ver=%d MsgType=%d sensorTypeId=%d SN=%d",
					idstr.c_str(),_moteId,_version, mtype, sensorTypeId, serialNumber));
		}
		break;
	case 1:
		/* unpack 1byte sequence */
		if (cp + 1 > eos) return false;
		_sequence = *cp++;
		DLOG(("mote=%s, id=%d, Ver=%d MsgType=%d seq=%d",
				idstr.c_str(), _moteId, _version, mtype, _sequence));
		break;
	case 2:
		DLOG(("mote=%s, id=%d, Ver=%d MsgType=%d ErrMsg=\"",
				idstr.c_str(), _moteId, _version, mtype) << string((const char*)cp,eos-cp) << "\"");
		break;
	default:
		DLOG(("Unknown msgType --- mote=%s, id=%d, Ver=%d MsgType=%d, msglen=",
				idstr.c_str(),_moteId, _version, mtype, eos-cp));
		break;
	}
	return mtype;
}

/*
 * Check EOM (0x03 0x04 0xd). Return pointer to start of EOM.
 */
const unsigned char* WisardMote::checkEOM(const unsigned char* sos, const unsigned char* eos)
{

	if (eos - 4 < sos) {
		n_u::Logger::getInstance()->log(LOG_ERR,"Message length is too short --- len= %d", eos-sos );
		return 0;
	}

	// NIDAS will likely add a NULL to the end of the message. Check for that.
	if (*(eos - 1) == 0) eos--;
	eos -= 3;

	if (memcmp(eos,"\x03\x04\r",3) != 0) {
		WLOG(("Bad EOM --- last 3 chars= %x %x %x", *(eos), *(eos+1), *(eos+2)));
		return 0;
	}
	return eos;
}

/*
 * Check CRC. Return pointer to CRC.
 */
const unsigned char* WisardMote::checkCRC (const unsigned char* cp, const unsigned char* eos)
{
	// retrieve CRC at end of message.
	if (eos - 1 < cp) {
		WLOG(("Message length is too short --- len= %d", eos-cp ));
		return 0;
	}
	unsigned char crc= *(eos-1);

	//calculate Cksum
	unsigned char cksum = (eos - cp) - 1;  //skip CRC+EOM+0x0
	for( ; cp < eos - 1; ) {
		unsigned char c =*cp++;
		cksum ^= c ;
	}

	if (cksum != crc ) {
		n_u::Logger::getInstance()->log(LOG_ERR,"Bad CKSUM --- %x vs  %x ", crc, cksum );
		return 0;
	}
	return eos - 1;
}

/* type id 0x01 */
const unsigned char* WisardMote::readPicTm(const unsigned char* cp, const unsigned char* eos)
{
	/* unpack  16 bit pic-time */
	int	val = missValue;
	if (cp + sizeof(uint16_t) <= eos) val = _fromLittle->uint16Value(cp);
	if (val!= missValue)
		_data.push_back(val/10.0);
	else
		_data.push_back(floatNAN);
	cp += sizeof(uint16_t);
	return cp;

}

/* type id 0x04 */
const unsigned char* WisardMote::readGenShort(const unsigned char* cp, const unsigned char* eos)
{
	/* unpack  16 bit gen-short */
	int	val = missValue;
	if (cp + sizeof(uint16_t) <= eos) val = _fromLittle->uint16Value(cp);
	if (val!= missValue)
		_data.push_back(val/1.0);
	else
		_data.push_back(floatNAN);
	cp += sizeof(uint16_t);
	return cp;

}

/* type id 0x05 */
const unsigned char* WisardMote::readGenLong(const unsigned char* cp, const unsigned char* eos)
{
	/* unpack  32 bit gen-long */
	int	val = missLongValue;
	if (cp + sizeof(uint32_t) <= eos) val = _fromLittle->uint32Value(cp);
	if (val!= missLongValue)
		_data.push_back(val/1.0);
	else
		_data.push_back(floatNAN);
	cp += sizeof(uint16_t);
	return cp;

}


/* type id 0x0B */
const unsigned char* WisardMote::readTmSec(const unsigned char* cp, const unsigned char* eos)
{
	/* unpack  32 bit  t-tm ticks in sec */
	int	val = missValue;
	if (cp + sizeof(uint32_t) <= eos) val = _fromLittle->uint32Value(cp);
	cp += sizeof(uint32_t);
	if (val!= missValue)
		_data.push_back(val);
	else
		_data.push_back(floatNAN);
	return cp;
}

/* type id 0x0C */
const unsigned char* WisardMote::readTmCnt(const unsigned char* cp, const unsigned char* eos)
{
	/* unpack  32 bit  tm-count in  */
	int	val = missValue;
	if (cp + sizeof(uint32_t) <= eos) val = _fromLittle->uint32Value(cp);
	cp += sizeof(uint32_t);
	if (val!= missValue)
		_data.push_back(val);
	else
		_data.push_back(floatNAN);
	return cp;
}


/* type id 0x0E */
const unsigned char* WisardMote::readTm10thSec(const unsigned char* cp, const unsigned char* eos)
{
	/* unpack  32 bit  t-tm-ticks in 10th sec */
	int	val = missValue;
	if (cp + sizeof(uint32_t) <= eos) val = _fromLittle->uint32Value(cp);
	cp += sizeof(uint32_t);
	if (val!= missValue)
		//TODO convert to diff of currenttime-val. Users want to see the diff, not the raw count
		_data.push_back(val/10.);
	else {
		_data.push_back(floatNAN);
	}
	return cp;
}


/* type id 0x0D */
const unsigned char* WisardMote::readTm100thSec(const unsigned char* cp, const unsigned char* eos)
{
	/* unpack  32 bit  t-tm-100th in sec */
	int	val = missValue;
	if (cp + sizeof(uint32_t) <= eos) val = _fromLittle->uint32Value(cp);
	cp += sizeof(uint32_t);
	if (val!= missValue)
		_data.push_back(val/100.0);
	else
		_data.push_back(floatNAN);
	return cp;
}

/* type id 0x0F */
const unsigned char* WisardMote::readPicDT(const unsigned char* cp, const unsigned char* eos)
{
	/*  16 bit jday */
	int jday = missValue;
	if (cp + sizeof(uint16_t) <= eos) jday = _fromLittle->uint16Value(cp);
	cp += sizeof(uint16_t);
	if (jday!= missValue)
		_data.push_back(jday);
	else
		_data.push_back(floatNAN);

	/*  8 bit hour+ 8 bit min+ 8 bit sec  */
	int hh = missByteValue;
	if (cp + sizeof(uint8_t) <= eos) hh = *cp;
	cp += sizeof(uint8_t);
	if (hh != missByteValue)
		_data.push_back(hh);
	else
		_data.push_back(floatNAN);

	int mm = missByteValue;
	if (cp + sizeof(uint8_t) <= eos) mm = *cp;
	cp += sizeof(uint8_t);
	if (mm != missByteValue)
		_data.push_back(mm);
	else
		_data.push_back(floatNAN);

	int ss = missByteValue;
	if (cp + sizeof(uint8_t) <= eos) ss = *cp;
	cp += sizeof(uint8_t);
	if (ss != missByteValue)
		_data.push_back(ss);
	else
		_data.push_back(floatNAN);

	return cp;
}

/* type id 0x20-0x23 */
const unsigned char* WisardMote::readTsoilData(const unsigned char* cp, const unsigned char* eos)
{
	/* unpack 16 bit  */
	for (int i=0; i<4; i++) {
		int val = (signed)0xFFFF8000;
		if (cp + sizeof(int16_t) <= eos) val = _fromLittle->int16Value(cp);
		cp += sizeof(int16_t);
		if (val!= (signed)0xFFFF8000)
			_data.push_back(val/100.0);
		else
			_data.push_back(floatNAN);
	}
	return cp;
}

/* type id 0x24-0x27 */
const unsigned char* WisardMote::readGsoilData(const unsigned char* cp, const unsigned char* eos)
{
	int val = (signed)0xFFFF8000;
	if (cp + sizeof(int16_t) <= eos) val = _fromLittle->int16Value(cp);
	cp += sizeof(int16_t);
	if (val!= (signed)0xFFFF8000)
		_data.push_back(val/1.0);
	else
		_data.push_back(floatNAN);
	return cp;
}

/* type id 0x28-0x2B */
const unsigned char* WisardMote::readQsoilData(const unsigned char* cp, const unsigned char* eos)
{
	int val = missValue;
	if (cp + sizeof(uint16_t) <= eos) val = _fromLittle->uint16Value(cp);
	cp += sizeof(uint16_t);
	if (val!= missValue)
		_data.push_back(val/1.0);
	else
		_data.push_back(floatNAN);
	return cp;
}

/* type id 0x2C-0x2F */
const unsigned char* WisardMote::readTP01Data(const unsigned char* cp, const unsigned char* eos)
{
	// 5 signed
	for (int i=0; i<5; i++) {
		int val = (signed)0xFFFF8000;   // signed
		if (cp + sizeof(int16_t) <= eos) val = _fromLittle->int16Value(cp);
		cp += sizeof(int16_t);
		if (val!= (signed)0xFFFF8000){
			switch (i) {
			case 0: _data.push_back(val/10000.0);
			case 1: _data.push_back(val/1.0);
			case 2: _data.push_back(val/1.0);
			case 3: _data.push_back(val/100.0);
			case 4: _data.push_back(val/1000.0);
			}
		}
		else
			_data.push_back(floatNAN);
	}
	return cp;
}

/* type id 0x40 status-id */
const unsigned char* WisardMote::readStatusData(const unsigned char* cp, const unsigned char* eos)
{
	int val = missByteValue;
	if (cp + 1 <= eos) val = *cp++;
	if (val!= missByteValue)
		_data.push_back(val);
	else
		_data.push_back(floatNAN);
	return cp;

}

/* type id 0x49 pwr */
const unsigned char* WisardMote::readPwrData(const unsigned char* cp, const unsigned char* eos)
{
	for (int i=0; i<6; i++){
		int val = missValue;
		if (cp + sizeof(uint16_t) <= eos) val = _fromLittle->uint16Value(cp);
		cp += sizeof(uint16_t);
		if (val!= missValue) {
			if (i==0 || i==3) 	_data.push_back(val/10.0);  //voltage 10th
			else _data.push_back(val/1.0);					//miliamp
		} else
			_data.push_back(floatNAN);
	}
	return cp;
}



/* type id 0x50-0x53 */
const unsigned char* WisardMote::readRnetData(const unsigned char* cp, const unsigned char* eos)
{
	int val = (signed)0xFFFF8000;   // signed
	if (cp + sizeof(int16_t) <= eos) val = _fromLittle->int16Value(cp);
	cp += sizeof(int16_t);
	if (val!= (signed)0xFFFF8000)
		_data.push_back(val/10.0);
	else
		_data.push_back(floatNAN);
	return cp;
}

/* type id 0x54-0x5B */
const unsigned char* WisardMote::readRswData(const unsigned char* cp, const unsigned char* eos)
{
	int val = missValue;
	if (cp + sizeof(uint16_t) <= eos) val = _fromLittle->uint16Value(cp);
	cp += sizeof(uint16_t);
	if (val!= missValue)
		_data.push_back(val/10.0);
	else
		_data.push_back(floatNAN);
	return cp;
}

/* type id 0x5C-0x63 */
const unsigned char* WisardMote::readRlwData(const unsigned char* cp, const unsigned char* eos)
{
	for (int i=0; i<5; i++) {
		int val = (signed)0xFFFF8000;   // signed
		if (cp + sizeof(int16_t) <= eos) val = _fromLittle->int16Value(cp);
		cp += sizeof(int16_t);
		if (val != (signed)0xFFFF8000 ) {
			if (i>0)
				_data.push_back(val/100.0);          // tcase and tdome1-3
			else
				_data.push_back(val/10.0);           // tpile
		} else                                  //null
			_data.push_back(floatNAN);
	}
	return cp;
}


/* type id 0x64-0x6B */
const unsigned char* WisardMote::readRlwKZData(const unsigned char* cp, const unsigned char* eos)
{
	for (int i=0; i<2; i++) {
		int val = (signed)0xFFFF8000;   // signed
		if (cp + sizeof(int16_t) <= eos) val = _fromLittle->int16Value(cp);
		cp += sizeof(int16_t);
		if (val != (signed)0xFFFF8000 ) {
			if (i==0) _data.push_back(val/10.0);          // RPile
			else _data.push_back(val/100.0);          	// tcase
		} else
			_data.push_back(floatNAN);
	}
	return cp;
}


void WisardMote::initFuncMap() {
	if (! _functionsMapped) {
		_nnMap[0x01] = &WisardMote::readPicTm;
		_nnMap[0x04] = &WisardMote::readGenShort;
		_nnMap[0x05] = &WisardMote::readGenLong;

		_nnMap[0x0B] = &WisardMote::readTmSec;
		_nnMap[0x0C] = &WisardMote::readTmCnt;
		_nnMap[0x0D] = &WisardMote::readTm100thSec;
		_nnMap[0x0E] = &WisardMote::readTm10thSec;
		_nnMap[0x0F] = &WisardMote::readPicDT;

		_nnMap[0x20] = &WisardMote::readTsoilData;
		_nnMap[0x21] = &WisardMote::readTsoilData;
		_nnMap[0x22] = &WisardMote::readTsoilData;
		_nnMap[0x23] = &WisardMote::readTsoilData;

		_nnMap[0x24] = &WisardMote::readGsoilData;
		_nnMap[0x25] = &WisardMote::readGsoilData;
		_nnMap[0x26] = &WisardMote::readGsoilData;
		_nnMap[0x27] = &WisardMote::readGsoilData;

		_nnMap[0x28] = &WisardMote::readQsoilData;
		_nnMap[0x29] = &WisardMote::readQsoilData;
		_nnMap[0x2A] = &WisardMote::readQsoilData;
		_nnMap[0x2B] = &WisardMote::readQsoilData;

		_nnMap[0x2C] = &WisardMote::readTP01Data;
		_nnMap[0x2D] = &WisardMote::readTP01Data;
		_nnMap[0x2E] = &WisardMote::readTP01Data;
		_nnMap[0x2F] = &WisardMote::readTP01Data;

		_nnMap[0x40] = &WisardMote::readStatusData;
		_nnMap[0x49] = &WisardMote::readPwrData;

		_nnMap[0x50] = &WisardMote::readRnetData;
		_nnMap[0x51] = &WisardMote::readRnetData;
		_nnMap[0x52] = &WisardMote::readRnetData;
		_nnMap[0x53] = &WisardMote::readRnetData;

		_nnMap[0x54] = &WisardMote::readRswData;
		_nnMap[0x55] = &WisardMote::readRswData;
		_nnMap[0x56] = &WisardMote::readRswData;
		_nnMap[0x57] = &WisardMote::readRswData;

		_nnMap[0x58] = &WisardMote::readRswData;
		_nnMap[0x59] = &WisardMote::readRswData;
		_nnMap[0x5A] = &WisardMote::readRswData;
		_nnMap[0x5B] = &WisardMote::readRswData;

		_nnMap[0x5C] = &WisardMote::readRlwData;
		_nnMap[0x5D] = &WisardMote::readRlwData;
		_nnMap[0x5E] = &WisardMote::readRlwData;
		_nnMap[0x5F] = &WisardMote::readRlwData;

		_nnMap[0x60] = &WisardMote::readRlwData;
		_nnMap[0x61] = &WisardMote::readRlwData;
		_nnMap[0x62] = &WisardMote::readRlwData;
		_nnMap[0x63] = &WisardMote::readRlwData;

		_nnMap[0x64] = &WisardMote::readRlwKZData;
		_nnMap[0x65] = &WisardMote::readRlwKZData;
		_nnMap[0x66] = &WisardMote::readRlwKZData;
		_nnMap[0x67] = &WisardMote::readRlwKZData;

		_nnMap[0x68] = &WisardMote::readRlwKZData;
		_nnMap[0x69] = &WisardMote::readRlwKZData;
		_nnMap[0x6A] = &WisardMote::readRlwKZData;
		_nnMap[0x6B] = &WisardMote::readRlwKZData;
		_functionsMapped = true;
	}
}

SampInfo WisardMote::_samps[] = {
		{0x0E, {{"TTime-kicks","secs","Total Time kick", true},}},

		{0x20,{
				{"Tsoil.a.1","degC","Soil Temperature", true},
				{"Tsoil.a.2","degC","Soil Temperature", true},
				{"Tsoil.a.3","degC","Soil Temperature", true},
				{"Tsoil.a.4","degC","Soil Temperature", true}, }
		},
		{0x21,{
				{"Tsoil.b.1","degC","Soil Temperature", true},
				{"Tsoil.b.2","degC","Soil Temperature", true},
				{"Tsoil.b.3","degC","Soil Temperature", true},
				{"Tsoil.b.4","degC","Soil Temperature", true}, }
		},
		{0x22,{
				{"Tsoil.c.1","degC","Soil Temperature", true},
				{"Tsoil.c.2","degC","Soil Temperature", true},
				{"Tsoil.c.3","degC","Soil Temperature", true},
				{"Tsoil.c.4","degC","Soil Temperature", true}, }
		},
		{0x23,{
				{"Tsoil.d.1","degC","Soil Temperature", true},
				{"Tsoil.d.2","degC","Soil Temperature", true},
				{"Tsoil.d.3","degC","Soil Temperature", true},
				{"Tsoil.d.4","degC","Soil Temperature", true}, }
		},

		{0x24, {{"Gsoil.a", "W/m^2", "Soil Heat Flux", true},}},
		{0x25, {{"Gsoil.b", "W/m^2", "Soil Heat Flux", true},}},
		{0x26, {{"Gsoil.c", "W/m^2", "Soil Heat Flux", true},}},
		{0x27, {{"Gsoil.d", "W/m^2", "Soil Heat Flux", true},}},

		{0x28,{{"QSoil.a", "vol%", "Soil Moisture", true},}},
		{0x29,{{"QSoil.b", "vol%", "Soil Moisture", true},}},
		{0x2A,{{"QSoil.c", "vol%", "Soil Moisture", true},}},
		{0x2B,{{"QSoil.d", "vol%", "Soil Moisture", true},}},

		{0x2C,{
				{"Vheat.a","V","Soil Thermal, heat volt", true},
				{"Vpile-on.a","microV","Soil Thermal, transducer volt", true},
				{"Vpile-off.a","microV","Soil Thermal, heat volt", true},
				{"Tau63.a","secs","Soil Thermal, time diff", true},
				{"L.a","W/mDegk","Thermal property", true}, }
		},
		{0x2D,{
				{"Vheat.b","V","Soil Thermal, heat volt", true},
				{"Vpile-on.b","microV","Soil Thermal, transducer volt", true},
				{"Vpile-off.b","microV","Soil Thermal, heat volt", true},
				{"Tau63.b","secs","Soil Thermal, time diff", true},
				{"L.b","W/mDegk","Thermal property", true}, }
		},
		{0x2E,{
				{"Vheat.c","V","Soil Thermal, heat volt", true},
				{"Vpile-on.c","microV","Soil Thermal, transducer volt", true},
				{"Vpile-off.c","microV","Soil Thermal, heat volt", true},
				{"Tau63.c","secs","Soil Thermal, time diff", true},
				{"L.c","W/mDegk","Thermal property", true}, }
		},
		{0x2F,{
				{"Vheat.d","V","Soil Thermal, heat volt", true},
				{"Vpile-on.d","microV","Soil Thermal, transducer volt", true},
				{"Vpile-off.d","microV","Soil Thermal, heat volt", true},
				{"Tau63.d","secs","Soil Thermal, time diff", true},
				{"L.d","W/mDegk","Thermal property", true}, }
		},

		{0x40, {{"StatusId","Count","Sampling mode", true},}},

		{0x50, {{"Rnet.a","W/m^2","Net Radiation", true},}},
		{0x51, {{"Rnet.b","W/m^2","Net Radiation", true},}},
		{0x52, {{"Rnet.c","W/m^2","Net Radiation", true},}},
		{0x53, {{"Rnet.d","W/m^2","Net Radiation", true},}},

		{0x54, {{"Rsw.i.a","W/m^2","Incoming Short Wave", true},}},
		{0x55, {{"Rsw.i.b","W/m^2","Incoming Short Wave", true},}},
		{0x56, {{"Rsw.i.c","W/m^2","Incoming Short Wave", true},}},
		{0x57, {{"Rsw.i.d","W/m^2","Incoming Short Wave", true},}},

		{0x58, {{"Rsw.o.a","W/m^2","Outgoing Short Wave", true},}},
		{0x59, {{"Rsw.o.b","W/m^2","Outgoing Short Wave", true},}},
		{0x5A, {{"Rsw.o.c","W/m^2","Outgoing Short Wave", true},}},
		{0x5B, {{"Rsw.o.d","W/m^2","Outgoing Short Wave", true},}},

		{0x5C,{
				{"Rlw.i.tpile.a","W/m^2","Incoming Long Wave", true},
				{"Rlw.i.tcase.a","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome1.a","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome2.a","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome3.a","degC","Incoming Long Wave", true},}
		},
		{0x5D,{
				{"Rlw.i.tpile.b","W/m^2","Incoming Long Wave", true},
				{"Rlw.i.tcase.b","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome1.b","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome2.b","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome3.b","degC","Incoming Long Wave", true},}
		},
		{0x5E,{
				{"Rlw.i.tpile.c","W/m^2","Incoming Long Wave", true},
				{"Rlw.i.tcase.c","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome1.c","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome2.c","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome3.c","degC","Incoming Long Wave", true},}
		},
		{0x5F,{
				{"Rlw.i.tpile.d","W/m^2","Incoming Long Wave", true},
				{"Rlw.i.tcase.d","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome1.d","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome2.d","degC","Incoming Long Wave", true},
				{"Rlw.i.tdome3.d","degC","Incoming Long Wave", true},}
		},

		{0x60,{
				{"Rlw-o.tpile.a","W/m^2","Outgoing Long Wave", true},
				{"Rlw-o.tcase.a","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome1.a","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome2.a","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome3.a","degC","Outgoing Long Wave", true},}
		},
		{0x61,{
				{"Rlw-o.tpile.b","W/m^2","Outgoing Long Wave", true},
				{"Rlw-o.tcase.b","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome1.b","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome2.b","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome3.b","degC","Outgoing Long Wave", true},}
		},
		{0x62,{
				{"Rlw-o.tpile.c","W/m^2","Outgoing Long Wave", true},
				{"Rlw-o.tcase.c","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome1.c","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome2.c","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome3.c","degC","Outgoing Long Wave", true},}
		},
		{0x63,{
				{"Rlw-o.tpile.d","W/m^2","Outgoing Long Wave", true},
				{"Rlw-o.tcase.d","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome1.d","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome2.d","degC","Outgoing Long Wave", true},
				{"Rlw-o.tdome3.d","degC","Outgoing Long Wave", true},}
		},

		{0x64,{
				{"Rlwk.i.Rpile.a","W/m^2","Incoming Long Wave K&Z", true},
				{"Rlwk.i.tcase.a","degC","Incoming Long Wave K&Z", true},}
		},
		{0x65,{
				{"Rlwk.i.Rpile.b","W/m^2","Incoming Long Wave K&Z", true},
				{"Rlwk.i.tcase.b","degC","Incoming Long Wave K&Z", true},}
		},
		{0x66,{
				{"Rlwk.i.Rpile.c","W/m^2","Incoming Long Wave K&Z", true},
				{"Rlwk.i.tcase.c","degC","Incoming Long Wave K&Z", true},}
		},
		{0x67,{
				{"Rlwk.i.Rpile.d","W/m^2","Incoming Long Wave K&Z", true},
				{"Rlwk.i.tcase.d","degC","Incoming Long Wave K&Z", true},}
		},
		{0x68,{
				{"Rlwk.o.Rpile.a","W/m^2","Outgoing  Long Wave K&Z", true},
				{"Rlwk.o.tcase.a","degC","Outgoing  Long Wave K&Z", true},}
		},
		{0x69,{
				{"Rlwk.o.Rpile.b","W/m^2","Outgoing  Long Wave K&Z", true},
				{"Rlwk.o.tcase.b","degC","Outgoing  Long Wave K&Z", true},}
		},
		{0x6A,{
				{"Rlwk.o.Rpile.c","W/m^2","Outgoing  Long Wave K&Z", true},
				{"Rlwk.o.tcase.c","degC","Outgoing  Long Wave K&Z", true},}
		},
		{0x6B,{
				{"Rlwk.o.Rpile.d","W/m^2","Outgoing  Long Wave K&Z", true},
				{"Rlwk.o.tcase.d","degC","Outgoing  Long Wave K&Z", true},}
		},

		{0,{{},}}

};

