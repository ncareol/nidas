// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
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

#include "WisardMote.h"
#include <nidas/util/Logger.h>
#include <nidas/core/DSMSensor.h>
#include <nidas/core/Project.h>
#include <nidas/core/DSMConfig.h>
#include <nidas/core/SampleTag.h>
#include <nidas/core/Variable.h>
#include <nidas/util/UTime.h>
#include <nidas/util/InvalidParameterException.h>

#include <cmath>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>

using namespace nidas::dynld;
using namespace nidas::dynld::isff;
using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

#define MSECS_PER_HALF_DAY 43200000

/* static */
bool WisardMote::_functionsMapped = false;

/* static */
map<int, pair<WisardMote::unpack_t,unsigned int> > WisardMote::_unpackMap;

/* static */
map<int, string> WisardMote::_typeNames;

/* static */
const n_u::EndianConverter * WisardMote::fromLittle =
    n_u::EndianConverter::getConverter(
            n_u::EndianConverter::EC_LITTLE_ENDIAN);

/* static */
map<dsm_sample_id_t,WisardMote*> WisardMote::_processorSensors;

NIDAS_CREATOR_FUNCTION_NS(isff, WisardMote)

WisardMote::WisardMote() :
    _sampleTagsById(),
    _processorSensor(0),
    _sensorSerialNumbersByMoteIdAndType(),
    _sequenceNumbersByMoteId(),
    _badCRCsByMoteId(),
    _tdiffByMoteId(),
    _numBadSensorTypes(),
    _unconfiguredMotes(),
    _noSampleTags(),
    _ignoredSensorTypes(),
    _nowarnSensorTypes(),
    _tsoilData()
{
    setDuplicateIdOK(true);
    initFuncMap();
}
WisardMote::~WisardMote()
{
}

void WisardMote::validate()
{
    // Since WisardMote data has internal identifiers (mote ids and
    // sensor types), multiple WisardMote sensors can be instantiated on
    // a DSM with the same sensor id (typically 0x8000), but on different
    // serial ports. When the internal indentifiers are parsed from
    // the raw sample, the processed sample ids are generated from the
    // mote ids and sensor type values in the raw sample.

    // When samples are processed, the process methods of all DSMSensor
    // objects matching a given raw sample id are called. For WisardMotes
    // we want to do the processing in only one sensor instance for the
    // sensor id on each DSM, primarily so that multiple sensor objects
    // are not complaining about unrecognized mote ids or sample types.
    // So WisardMotes share a static map of sampleTagsById so that one
    // WisardMote object can do the processing for a DSM.
    //

    _processorSensor = _processorSensors[getId()];

    if (!_processorSensor) _processorSensors[getId()] = _processorSensor = this;
    else if (_processorSensor == this) return;  // already validated

    vector<int> motev;

    const Parameter* motes = getParameter("motes");
    if (motes) {
        /*
         * If a "motes" parameter exists for the sensor, then for
         * all sample tags with a zero mote field (bits 15-8), generate
         * tags for the list of motes in the parameter.
         */
        if (motes->getType() != Parameter::INT_PARAM)
            throw n_u::InvalidParameterException(getName(),"motes","should be integer type");
        for (int i = 0; i < motes->getLength(); i++) {
            int mote = (unsigned int) motes->getNumericValue(i);
            motev.push_back(mote);
        }
    }

    // make a copy, since removeSampleTag() will change the reference
    list<SampleTag*> configTags = getSampleTags();
    list<SampleTag*>::const_iterator ti = configTags.begin();

    // loop over the configured sample tags, creating the ones we want
    // using the "motes" and "stypes" parameters. Delete the original
    // configured tags.
    list<SampleTag*> newtags;
    for ( ; ti != configTags.end(); ++ti ) {
        SampleTag* stag = *ti;
        createSampleTags(stag,motev,newtags);
        removeSampleTag(stag);
    }

    ti = newtags.begin();
    for ( ; ti != newtags.end(); ++ti )
        _processorSensor->addMoteSampleTag(*ti);

    _processorSensor->addImpliedSampleTags(motev);

    if (_processorSensor == this) 
        checkLessUsedSensors();

    VLOG(("final getSampleTags().size()=") << getSampleTags().size());
    VLOG(("final _sampleTagsByIdTags.size()=") << _sampleTagsById.size());
    SerialSensor::validate();
}

void WisardMote::createSampleTags(const SampleTag* stag,const vector<int>& sensorMotes,list<SampleTag*>& newtags)
{

    // The stag must contain a Parameter, called "stypes", specifing one or
    // more sensor types that the stag is applied to.

    // stag can also contain a Parameter, "motes" specifying the motes that the
    // sample is for, overriding sensorMotes.  Otherwise the motes must be
    // passed in the sensorMotes vector which came from the "motes" parameter of
    // the sensor.

    // Sample tags are created for every mote and sensor type.
    // The resultant sample ids for processed samples will be the sum of
    //      sensor_id + (mote id << 8) + stype
    // where sensor_id is typically 0x8000

    string idstr;
    {
        ostringstream ost;
        ost << stag->getDSMId() << ",0x" << hex << stag->getSensorId() << "+" <<
            dec << stag->getSampleId();
        idstr = ost.str();
    }

    vector<int> motes;

    const Parameter* motep = stag->getParameter("motes");
    if (motep) {
        if (motep->getType() != Parameter::INT_PARAM)
            throw n_u::InvalidParameterException(getName() + ": id=" + idstr,
                    "parameter \"motes\"","should be integer type");
        for (int i = 0; i < motep->getLength(); i++)
            motes.push_back((int) motep->getNumericValue(i));
    }
    else motes = sensorMotes;

    if (motes.empty())
        throw n_u::InvalidParameterException(getName(),string("id=") + idstr,"no motes specified");

#ifdef DEBUG_DSM
    if (stag->getDSMId() == DEBUG_DSM) cerr << "mote=" << mote << endl;
#endif

    // This sample applies to all sensor type ids in the "stypes" parameter.
    // inid is a full sample id (dsm,sensor,mote,sensor type), except
    // that mote may be 0 indicating it applies to all motes.
    // If there is no "stypes" parameter
    const Parameter* stypep = stag->getParameter("stypes");
    if (!stypep)
            throw n_u::InvalidParameterException(getName(), string("id=") + idstr,
                    "no \"stypes\" parameter");
    if (stypep->getType() != Parameter::INT_PARAM || stypep->getLength() < 1)
        throw n_u::InvalidParameterException(getName()+": id=" + idstr,
                "stypes","should be hex or integer type, of length > 0");

    vector<int> stypes;
    for (int i = 0; i < stypep->getLength(); i++) {
        stypes.push_back((int) stypep->getNumericValue(i));
    }

    for (unsigned im = 0; im < motes.size(); im++) {
        int mote = motes[im];

        ostringstream moteost;
        moteost << mote;
        string motestr = moteost.str();

        for (unsigned int is = 0; is < stypes.size(); is++) {
            int stype = stypes[is];

            SampleTag *newtag = new SampleTag(*stag);
            newtag->setSampleId((mote << 8) + stype);
            for (unsigned int iv = 0; iv < newtag->getVariables().size(); iv++) {
                Variable& var = newtag->getVariable(iv);
                var.setPrefix(n_u::replaceChars(var.getPrefix(),"%m",motestr));
                var.setPrefix(n_u::replaceChars(var.getPrefix(),"%c",string(1,(char)('a' + is))));
                var.setSuffix(n_u::replaceChars(var.getSuffix(),"%m",motestr));
                var.setSuffix(n_u::replaceChars(var.getSuffix(),"%c",string(1,(char)('a' + is))));
            }
            newtags.push_back(newtag);
        }
    }
}

void WisardMote::addMoteSampleTag(SampleTag* tag)
{
    VLOG(("addSampleTag, id=") << tag->getDSMId() << ','
         << hex << tag->getSpSId() << dec
         << ", ntags=" << getSampleTags().size());
    
    if (_sampleTagsById[tag->getId()]) {
        WLOG(("%s: duplicate processed sample tag for id %d,%#x",
                    getName().c_str(), tag->getDSMId(),tag->getSpSId()));
        delete tag;
    }
    else {
        _sampleTagsById[tag->getId()] = tag;
        addSampleTag(tag);
    }
}

void WisardMote::addImpliedSampleTags(const vector<int>& sensorMotes)
{
    // add samples for WST_IMPLIED types. These don't have
    // to be configured in the XML for a mote, but
    // the sensor should have a "motes" parameter.
    for (unsigned int im = 0; im < sensorMotes.size(); im++) {
        int mote = sensorMotes[im];
        for (unsigned int itype = 0;; itype++) {
            int stype1 = _samps[itype].firstst;
            if (stype1 == 0) break;
            if (_samps[itype].type == WST_IMPLIED) {
                int stype2 = _samps[itype].lastst;
                for (int stype = stype1; stype <= stype2; stype++) {
                    SampleTag* newtag = createSampleTag(_samps[itype],mote,stype);
                    _processorSensor->addMoteSampleTag(newtag);
                }
            }
        }
    }
}
void WisardMote::checkLessUsedSensors()
{
    // accumulate sets of WST_IGNORED and WST_NOWARN sensor types.
    for (unsigned int itype = 0;; itype++) {
        int stype1 = _samps[itype].firstst;
        if (stype1 == 0) break;
        WISARD_SAMPLE_TYPE type = _samps[itype].type;
        int stype2 = _samps[itype].lastst;
        for (int stype = stype1; stype <= stype2; stype++) {
            switch (type) {
                case WST_IGNORED:
                    _ignoredSensorTypes.insert(stype);
                    break;
                case WST_NOWARN:
                    _nowarnSensorTypes.insert(stype);
                    break;
                default:
                    break;
            }
        }
    }
}

// create a SampleTag from contents of a SampInfo object
SampleTag* WisardMote::createSampleTag(SampInfo& sinfo,int mote, int stype)
{

    ostringstream moteost;
    moteost << mote;
    string motestr = moteost.str();

    mote <<= 8;

    SampleTag* newtag = new SampleTag(this);
    newtag->setSampleId(mote + stype);

    int nv = sizeof(sinfo.variables) / sizeof(sinfo.variables[0]);

    for (int iv = 0; iv < nv; iv++) {
        VarInfo vinf = sinfo.variables[iv];
        if (vinf.name == NULL)
            break;

        Variable *var = new Variable();
        var->setName(n_u::replaceChars(vinf.name,"%m",motestr));
        var->setName(n_u::replaceChars(var->getName(),"%c",string(1,(char)('a' + stype - sinfo.firstst))));

        var->setUnits(vinf.units);
        var->setLongName(vinf.longname);
        var->setDynamic(true);
        var->setLength(1);

        string aval = Project::getInstance()->expandString(vinf.plotrange);
        std::istringstream ist(aval);
        float prange[2] = { -10.0, 10.0 };
        // if plotrange value starts with '$' ignore error.
        if (aval.length() < 1 || aval[0] != '$') {
            int k;
            for (k = 0; k < 2; k++) {
                if (ist.eof())
                    break;
                ist >> prange[k];
                if (ist.fail())
                    break;
            }
            // Don't throw exception on poorly formatted plotranges
            if (k < 2) {
                n_u::InvalidParameterException e(string("variable ")
                        + vinf.name, "plot range", aval);
                WLOG(("%s", e.what()));
            }
        }
        var->setPlotRange(prange[0], prange[1]);

        newtag->addVariable(var);
    }
    return newtag;
}

bool WisardMote::process(const Sample * samp, list<const Sample *>&results)
{
    if (_processorSensor != this) return false;

    /* unpack a WisardMote packet, consisting of binary integer data from a variety
     * of sensor types. */
    const char *sos =
        (const char *) samp->getConstVoidDataPtr();
    const char *eos = sos + samp->getDataByteLength();
    const char *cp = sos;

    dsm_time_t ttag = samp->getTimeTag();

    /*  check for good EOM  */
    if (!(eos = checkEOM(cp, eos,ttag)))
        return false;

    /*  verify crc for data  */
    if (!(eos = checkCRC(cp, eos, ttag)))
        return false;

    /*  read message header */
    struct MessageHeader header;

    if (!readHead(cp, eos, ttag, &header)) return false;

    if (header.messageType != 1)
        return false; // other than a data message

    while (cp < eos) {

        /* get Wisard sensor type */
        unsigned int sensorType = (unsigned char)*cp++;

        VLOG(("%s: %s, moteId=%d, sensorid=%#x, sensorType=%#x",
              getName().c_str(),
              n_u::UTime(ttag).format(true, "%Y %m %d %H:%M:%S.%3f").c_str(),
              header.moteId, getSensorId(),sensorType));

        /* find the appropriate member function to unpack the data for this sensorType */
        const pair<unpack_t,int>& upair = _unpackMap[sensorType];
        unpack_t unpack = upair.first;

        if (unpack == NULL) {
            if (!( _numBadSensorTypes[header.moteId][sensorType]++ % 1000))
                WLOG(("%s: %s, moteId=%d: unknown sensorType=%#.2x, at byte %u, #times=%u",
                        getName().c_str(),
                        n_u::UTime(ttag).format(true, "%Y %m %d %H:%M:%S.%3f").c_str(),
                        header.moteId, sensorType,
                        (unsigned int)(cp-sos-1),_numBadSensorTypes[header.moteId][sensorType]));
            break;
        }

        unsigned int nfields = upair.second;

        // sample id of processed sample
        dsm_sample_id_t sid = getId() + (header.moteId << 8) + sensorType;
        SampleTag* stag = _sampleTagsById[sid];
        SampleT<float>* osamp = 0;

        /* create an output floating point sample */
        if (stag) {
            const vector<Variable*>& vars = stag->getVariables();
            unsigned int slen = vars.size();
            osamp = getSample<float> (std::max(slen,nfields));
            osamp->setId(sid);
            osamp->setTimeTag(ttag);
        }
        else {
            // if a sample is ignored, we don't create a sample, but still
            // need to keep parsing this raw sample.
            bool ignore = _ignoredSensorTypes.find(sensorType) != _ignoredSensorTypes.end();
            if (!ignore) {
                bool warn = _nowarnSensorTypes.find(sensorType) == _nowarnSensorTypes.end();
                if (warn) {
                    if (!( _noSampleTags[header.moteId][sensorType]++ % 1000))
                        WLOG(("%s: %s, no sample tag for %d,%#x, mote=%d, sensorType=%#.2x, #times=%u",
                                getName().c_str(),
                                n_u::UTime(ttag).format(true, "%Y %m %d %H:%M:%S.%3f").c_str(),
                                GET_DSM_ID(sid),GET_SPS_ID(sid),header.moteId,sensorType,_noSampleTags[header.moteId][sensorType]));
                }
                osamp = getSample<float> (nfields);
                osamp->setId(sid);
                osamp->setTimeTag(ttag);
            }
        }

        /* unpack the sample for this sensorType.
         * If osamp is NULL, the character pointer will be
         * moved past the field for this sensorType, but (obviously) no
         * data stored in the sample. */
        cp = (this->*unpack)(cp, eos, nfields, &header, stag, osamp);

        /* push out */
        if (osamp) results.push_back(osamp);
    }
    return true;
}

void WisardMote::convert(SampleTag* stag, SampleT<float>* osamp, float* results)
{
    applyConversions(stag, osamp, results);
}

/*
 * read mote id: return -1 invalid.  >0 valid mote id.
 */
int WisardMote::readMoteId(const char* &cp, const char*eos)
{
    if (eos - cp < 4) return -1;    // IDn:

    if (memcmp(cp,"ID",2)) return -1;
    cp += 2;

    int l = strspn(cp,"0123456789");
    if (l == 0) return -1;
    l = std::min((int)(eos-cp)-1,l);

    if (*(cp + l) != ':') return -1;

    int moteId = atoi(cp);
    cp += l + 1;

    return moteId;
}

/*
 * Read initial portion of a Wisard message, filling in a struct MessageHeader.
 */
bool WisardMote::readHead(const char *&cp, const char *eos,
        dsm_time_t ttag, struct MessageHeader* hdr)
{
    hdr->moteId = readMoteId(cp,eos);
    if (hdr->moteId < 0) return false;

    // version number
    if (cp == eos) return false;
    hdr->version = *cp++;

    // message type
    if (cp == eos) return false;
    hdr->messageType = *cp++;

    switch (hdr->messageType) {
    case 0:
        /* unpack 1 bytesId + 2 byte s/n */
        while (cp + 3 <= eos) {
            int sensorType = *cp++;
            int serialNumber = WisardMote::fromLittle->uint16Value(cp);
            cp += sizeof(short);
            // log serial number if it changes.
            if (_sensorSerialNumbersByMoteIdAndType[hdr->moteId][sensorType]
                                                             != serialNumber) {
                _sensorSerialNumbersByMoteIdAndType[hdr->moteId][sensorType]
                                                             = serialNumber;
                ILOG(("%s: %s, mote=%d, sensorType=%#.2x SN=%d, typeName=%s",
                        getName().c_str(),
                        n_u::UTime(ttag).format(true,"%Y %m %d %H:%M:%S.%3f").c_str(),
                        hdr->moteId, sensorType,
                        serialNumber,_typeNames[sensorType].c_str()));
            }
        }
        break;
    case 1:
        /* unpack 1byte sequence */
        if (cp == eos)
            return false;
        _sequenceNumbersByMoteId[hdr->moteId] = *cp++;
        VLOG(("mote=%d, Ver=%d MsgType=%d seq=%d",
              hdr->moteId, hdr->version, hdr->messageType,
              _sequenceNumbersByMoteId[hdr->moteId]));
        break;
    case 2:
        VLOG(("mote=%d, Ver=%d MsgType=%d ErrMsg=\"",
              hdr->moteId, hdr->version,
              hdr->messageType) << string((const char *) cp, eos - cp) << "\"");
        break;
    default:
        WLOG(("%s: %s, unknown msgType, mote=%d, Ver=%d MsgType=%d, len=%d",
                getName().c_str(),
                n_u::UTime(ttag).format(true,"%Y %m %d %H:%M:%S.%3f").c_str(),
                hdr->moteId, hdr->version, hdr->messageType, (int)(eos - cp)+3));
        return false;
    }
    return true;
}

/*
 * Check EOM (0x03 0x04 0xd). Return pointer to start of EOM.
 */
const char *WisardMote::checkEOM(const char *sos, const char *eos, dsm_time_t ttag)
{
    if (eos - 4 < sos) {
        WLOG(("%s: %s, message length is too short, len=%d",
                getName().c_str(),
                n_u::UTime(ttag).format(true,"%Y %m %d %H:%M:%S.%3f").c_str(),
                (int)(eos - sos)));
        return 0;
    }
    // NIDAS will likely add a NULL to the end of the message. Check for that.
    if (*(eos - 1) == 0)
        eos--;
    eos -= 3;

    if (memcmp(eos, "\x03\x04\r", 3) != 0) {
        NLOG(("%s: %s, bad EOM, last 3 chars= %x %x %x",
                getName().c_str(),
                n_u::UTime(ttag).format(true,"%Y %m %d %H:%M:%S.%3f").c_str(),
                    *(unsigned char*)(eos), *(unsigned char*)(eos + 1),*(unsigned char*)(eos + 2)));
        return 0;
    }
    return eos;
}

/*
 * Check CRC.
 * Return pointer to CRC, which is one past the end of the data portion,
 * or if the CRC is bad, return 0.
 */
const char *WisardMote::checkCRC(const char *cp, const char *eos, dsm_time_t ttag)
{
    // eos points to one past the CRC.
    const char* crcp = eos - 1;

    int origlen = (int)(eos - cp) + 3; // include the EOM (0x03 0x04 0xd)

    if (crcp <= cp) {
        WLOG(("%s: %s, message length is too short, len=%d",
                getName().c_str(),
                n_u::UTime(ttag).format(true,"%Y %m %d %H:%M:%S.%3f").c_str(),
                origlen));
        return 0;
    }

    // read value of CRC at end of message.
    unsigned char crc = *crcp;

    // Calculate checksum. Start with length of message, not including checksum.
    unsigned char cksum = (int)(crcp - cp);
    for (const char *cp2 = cp; cp2 < crcp;)
        cksum ^= *cp2++;


    if (cksum != crc) {

        int moteId = -1;

        const char* idstr = strstr(cp,"ID");
        if (!idstr) {
            if (!(_badCRCsByMoteId[moteId]++ % 100)) {
                NLOG(("%s: %s, bad checksum and no ID in message, len=%d, #bad=%u",
                            getName().c_str(),
                            n_u::UTime(ttag).format(true, "%Y %m %d %H:%M:%S.%3f").c_str(),
                            origlen,_badCRCsByMoteId[moteId]));
            }
            return 0;
        }
        else if (idstr > cp) {
            if (!(_badCRCsByMoteId[moteId]++ % 100)) {
                NLOG(("%s: %s, bad checksum and %d bad characters before ID, message len=%d, #bad=%u",
                            getName().c_str(),
                            n_u::UTime(ttag).format(true, "%Y %m %d %H:%M:%S.%3f").c_str(),
                            (int)(idstr-cp),origlen,_badCRCsByMoteId[moteId]));
            }
            cp = idstr;
            return checkCRC(cp, eos, ttag);
        }
        // Print out mote id
        moteId = readMoteId(cp, eos);
        if (moteId > 0) {
            if (!(_badCRCsByMoteId[moteId]++ % 10)) {
                NLOG(("%s: %s, bad checksum for mote id %d, length=%d, tx crc=%#.2x, calc crc=%#.2x, #bad=%u",
                            getName().c_str(),
                            n_u::UTime(ttag).format(true, "%Y %m %d %H:%M:%S.%3f").c_str(),
                            moteId, origlen,
                            crc,cksum,_badCRCsByMoteId[moteId]));
            }
        } else {
            if (!(_badCRCsByMoteId[moteId]++ % 100)) {
                NLOG(("%s: %s, bad checksum for unknown mote, length=%d, tx crc=%#.2x, calc crc=%#.2x, #bad=%u",
                            getName().c_str(),
                            n_u::UTime(ttag).format(true, "%Y %m %d %H:%M:%S.%3f").c_str(),
                             origlen, crc, cksum,_badCRCsByMoteId[moteId]));
            }
        }
        return 0;
    }
    return eos-1;
}

/* unnamed namespace */
namespace {

    const unsigned char _missValueUint8 = 0x80;

    const short _missValueInt16 = (signed) 0x8000;

    const unsigned short _missValueUint16 = (unsigned) 0x8000;

    const unsigned int _missValueUint32 = 0x80000000;

    const int _missValueInt32 = 0x80000000;

    const char *readUint16(const char *cp, const char *eos,
            unsigned int nfields,float scale, float *fp)
    {
        /* unpack 16 bit unsigned integers */
        unsigned int i;
        for (i = 0; i < nfields; i++) {
            if (cp + sizeof(uint16_t) > eos) break;
            unsigned short val = WisardMote::fromLittle->uint16Value(cp);
            cp += sizeof(uint16_t);
            if (fp) {
                if (val != _missValueUint16)
                    *fp++ = val * scale;
                else
                    *fp++ = floatNAN;
            }
        }
        if (fp) for ( ; i < nfields; i++) *fp++ = floatNAN;
        return cp;
    }

    const char *readInt16(const char *cp, const char *eos,
            unsigned int nfields,float scale, float *fp)
    {
        /* unpack 16 bit signed integers */
        unsigned int i;
        for (i = 0; i < nfields; i++) {
            if (cp + sizeof(int16_t) > eos) break;
            signed short val = WisardMote::fromLittle->int16Value(cp);
            cp += sizeof(int16_t);
            if (fp) {
                if (val != _missValueInt16)
                    *fp++ = val * scale;
                else
                    *fp++ = floatNAN;
            }
        }
        if (fp) for ( ; i < nfields; i++) *fp++ = floatNAN;
        return cp;
    }

    const char *readUint32(const char *cp, const char *eos,
            unsigned int nfields,float scale, float* fp)
    {
        /* unpack 32 bit unsigned ints */
        unsigned int i;
        for (i = 0; i < nfields; i++) {
            if (cp + sizeof(uint32_t) > eos) break;
            unsigned int val = WisardMote::fromLittle->uint32Value(cp);
            cp += sizeof(uint32_t);
            if (fp) {
                if (val != _missValueUint32)
                    *fp++ = val * scale;
                else
                    *fp++ = floatNAN;
            }
        }
        if (fp) for ( ; i < nfields; i++) *fp++ = floatNAN;
        return cp;
    }

    const char *readInt32(const char *cp, const char *eos,
            unsigned int nfields,float scale, float* fp)
    {
        /* unpack 32 bit unsigned ints */
        unsigned int i;
        for (i = 0; i < nfields; i++) {
            if (cp + sizeof(uint32_t) > eos) break;
            int val = WisardMote::fromLittle->int32Value(cp);
            cp += sizeof(int32_t);
            if (val != _missValueInt32)
                *fp++ = val * scale;
            else
                *fp++ = floatNAN;
        }
        for ( ; i < nfields; i++) *fp++ = floatNAN;
        return cp;
    }

}   // unnamed namespace

const char* WisardMote::unpackPicTime(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 1);

    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    // PIC time, convert from tenths of sec to sec
    cp =  readUint16(cp,eos,nfields,0.1,fp);
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackUint16(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{

    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }
    
    cp = readUint16(cp,eos,nfields,1.0,fp);
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackInt16(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{

    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,1.0,fp);
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackUint32(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readUint32(cp,eos,nfields,1.0,fp);

    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackInt32(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt32(cp,eos,nfields,1.0,fp);

    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackAccumSec(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 1);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
        *fp = floatNAN;
    }

    if (cp + sizeof(uint32_t) <= eos) {
        unsigned int val = WisardMote::fromLittle->uint32Value(cp);    // accumulated seconds
        if (fp && val != _missValueUint32 && val != 0) {
            struct tm tm;
            n_u::UTime ut(osamp->getTimeTag());
            ut.toTm(true,&tm);

            // compute time on Jan 1, 00:00 UTC of year from sample time tag
            tm.tm_sec = tm.tm_min = tm.tm_hour = tm.tm_mon = 0;
            tm.tm_mday = 1;
            tm.tm_yday = -1;
            ut = n_u::UTime::fromTm(true,&tm);

            VLOG(("") << "ttag="
                 << n_u::UTime(osamp->getTimeTag()).format(true,"%Y %m %d %H:%M:%S.%6f")
                 << ",ut=" << ut.format(true,"%Y %m %d %H:%M:%S.%6f"));
            VLOG(("") << "ttag=" << osamp->getTimeTag() << ", ut=" << ut.toUsecs()
                 << ", val=" << val);
            // will have a rollover issue on Dec 31 23:59:59, but we'll ignore it
            long long diff = (osamp->getTimeTag() - (ut.toUsecs() + (long long)val * USECS_PER_SEC));

            // bug in the mote timekeeping: the 0x0b values are 1 day too large
            if (::llabs(diff+USECS_PER_DAY) < 60 * USECS_PER_SEC) diff += USECS_PER_DAY;
            *fp = (float)diff / USECS_PER_SEC;
        }
        cp += sizeof(uint32_t);
    }
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpack100thSec(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 1);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readUint32(cp,eos,nfields,0.01,fp);
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpack10thSec(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader* hdr,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 2);

    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    /* unpack  32 bit  t-tm-ticks in 10th sec */
    unsigned int val = 0;
    if (cp + sizeof(uint32_t) > eos) {
        if (fp) for (unsigned int n = 0; n < osamp->getDataLength(); n++)
            fp[n] = floatNAN;
        return cp;
    }
    val = WisardMote::fromLittle->uint32Value(cp);
    // convert mote time to 1/10th secs since 00:00 UTC
    val %= (SECS_PER_DAY * 10);
    // convert to milliseconds
    val *= 100;

    cp += sizeof(uint32_t);

    if (fp) {

        //convert sample time tag to milliseconds since 00:00 UTC
        int mSOfDay = (osamp->getTimeTag() / USECS_PER_MSEC) % MSECS_PER_DAY;

        int diff = mSOfDay - val; //mSec

        if (abs(diff) > MSECS_PER_HALF_DAY) {
            if (diff < -MSECS_PER_HALF_DAY)
                diff += MSECS_PER_DAY;
            else if (diff > MSECS_PER_HALF_DAY)
                diff -= MSECS_PER_DAY;
        }
        float fval = (float) diff / MSECS_PER_SEC; // seconds

        // keep track of the first time difference.
        if (_tdiffByMoteId[hdr->moteId] == 0)
            _tdiffByMoteId[hdr->moteId] = diff;

        // subtract the first difference from each succeeding difference.
        // This way we can check the mote clock drift relative to the adam
        // when the mote is not initialized with an absolute time.
        diff -= _tdiffByMoteId[hdr->moteId];
        if (abs(diff) > MSECS_PER_HALF_DAY) {
            if (diff < -MSECS_PER_HALF_DAY)
                diff += MSECS_PER_DAY;
            else if (diff > MSECS_PER_HALF_DAY)
                diff -= MSECS_PER_DAY;
        }

        float fval2 = (float) diff / MSECS_PER_SEC;

        fp[0] = fval;
        fp[1] = fval2;
        for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
            fp[n] = floatNAN;
        convert(stag, osamp);
    }
    return cp;
}

const char* WisardMote::unpackPicTimeFields(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 4);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    if (fp) for (unsigned int n = 0; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;

    /*  16 bit jday */
    if (cp + sizeof(uint16_t) > eos) return cp;
    unsigned short jday = WisardMote::fromLittle->uint16Value(cp);
    cp += sizeof(uint16_t);
    if (fp) {
        if (jday != _missValueUint16)
            fp[0] = jday;
        else
            fp[0] = floatNAN;
    }

    /*  8 bit hour+ 8 bit min+ 8 bit sec  */
    if (cp + sizeof(uint8_t) > eos) return cp;
    unsigned char hh = *cp;
    cp += sizeof(uint8_t);
    if (fp) {
        if (hh != _missValueUint8)
            fp[1] = hh;
        else
            fp[1] = floatNAN;
    }

    if (cp + sizeof(uint8_t) > eos) return cp;
    unsigned char mm = *cp;
    cp += sizeof(uint8_t);
    if (fp) {
        if (mm != _missValueUint8)
            fp[2] = mm;
        else
            fp[2] = floatNAN;
    }

    if (cp + sizeof(uint8_t) > eos) return cp;
    unsigned char ss = *cp;
    cp += sizeof(uint8_t);
    if (fp) {
        if (ss != _missValueUint8)
            fp[3] = ss;
        else
            fp[3] = floatNAN;
        for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
            fp[n] = floatNAN;
        convert(stag, osamp);
    }
    return cp;
}

const char* WisardMote::unpackTRH(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 3);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    // T * 100, RH * 100, fan current.
    cp = readInt16(cp,eos,nfields,0.01,fp);

    if (fp) {
        for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        {
            fp[n] = floatNAN;
        }
        if (stag) {
            float results[3];
            convert(stag, osamp, results);
            // If ifan is bad or got filtered, then don't overwrite it's
            // value but filter T and RH.  Otherwise take all the results
            // as converted.
            if (::isnan(results[2]))
            {
                fp[0] = floatNAN;
                fp[1] = floatNAN;
            }
            else
            {
                memcpy(fp, results, sizeof(results));
            }
        }
    }
    return cp;
}

const char* WisardMote::unpackTsoil(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        assert(nfields >= 4);    // at least 4 temperatures
        fp = osamp->getDataPtr();
    }

    // This is not very efficient, but as a hack, we need to check if
    // TsoilUnsigned16 exists, and if it does, we want to treat it as an
    // uint16.
    bool tsoilIsUint16 = false;
    if (stag)
    {
        const Parameter* uint16samps = stag->getParameter("TsoilUnsigned16");
        if (uint16samps) {
            if (uint16samps->getType() != Parameter::BOOL_PARAM) throw n_u::InvalidParameterException(getName(),"TsoilUnsigned16","should be boolean type");
            tsoilIsUint16 = (bool) uint16samps->getNumericValue(0);
        }
    }
    cp = tsoilIsUint16 ? readUint16(cp,eos,NTSOILS,0.01,fp): readInt16(cp,eos,NTSOILS,0.01,fp);


    if (fp) {

        for (unsigned int it = NTSOILS; it < osamp->getDataLength(); it++)
        {
            fp[it] = floatNAN;
        }

        if (stag) {
            const vector<Variable*>& vars = stag->getVariables();
            unsigned int slen = vars.size();
            TsoilData& td = _tsoilData[stag->getId()];

            // sample should have variables for soil temps and their derivatives
            unsigned int ntsoils = slen / 2;
            unsigned int id = ntsoils;   // index of derivative
            for (unsigned int it = 0; it < ntsoils; it++,id++) {
                // DLOG(("f[%d]= %f", it, *fp));
                if (it < slen) {
                    vars[it]->convert(osamp->getTimeTag(), &fp[it], 1);
                }

                if (id < osamp->getDataLength()) {
                    float f = fp[it];
                    // time derivative
                    float fd = floatNAN;
                    if (!::isnan(f)) {
                        fd = (f - td.tempLast[it]) / double((osamp->getTimeTag() - td.timeLast[it])) * USECS_PER_SEC;
                        td.tempLast[it] = f;
                        td.timeLast[it] = osamp->getTimeTag();

                        // pass time derivative through limit checks and converters
                        if (id < slen) {
                            vars[id]->convert(osamp->getTimeTag(), &fd, 1);
                        }
                    }
                    fp[id] = fd;
                }
            }

        }
    }
    return cp;
}

const char* WisardMote::unpackGsoil(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,0.1,fp);
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackQsoil(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,0.01,fp);
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackTP01(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 5);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    unsigned int i;

    cp = readUint16(cp,eos,2,1.0,fp);   // Vheat, Vpile.on
    cp = readInt16(cp,eos,1,1.0,(fp ? fp+2 : fp));  // Vpile.off, signed
    cp = readUint16(cp,eos,2,1.0,(fp ? fp+3 : fp)); // Tau63, lambdasoil

    if (fp) {
        /* fields:
         * 0    Vheat, volts
         * 1    Vpile.on, microvolts
         * 2    Vpile.off, microvolts
         * 3    Tau63, seconds
         * 4    lambdasoil, derived on the mote, from Vheat, Vpile.on, Vpile.off
         */
        fp[0] /= 10000.0;   // Vheat, volts
        fp[3] /= 100.0;     // Tau63, seconds
        fp[4] /= 1000.0;    // lambdasoil, heat conductivity in W/(m * K)

        for (i = nfields ; i < osamp->getDataLength(); i++) fp[i] = floatNAN;

        convert(stag, osamp);

        // set derived lambdasoil to NAN if any of Vheat, Vpile.on,
        // Vpile.off are NAN
        for (i = 0; i < 3; i++) if (::isnan(fp[i])) fp[4] = floatNAN;
    }

    return cp;
}

const char* WisardMote::unpackStatus(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 1);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    if (cp + 1 > eos) return cp;
    unsigned char val = *cp++;
    if (fp) {
        if (val != _missValueUint8)
            *fp = val;
        else
            *fp = floatNAN;

        for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
            fp[n] = floatNAN;
        convert(stag, osamp);
    }
    return cp;
}

const char* WisardMote::unpackXbee(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readUint16(cp,eos,nfields,1.0,fp);
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackPower(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields == 6);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    unsigned int n = 3;
    // voltage, currents are unsigned according to the documentation.
    // However, in CABL, counts for Icharge would flip to
    // ~65400K after passing through 0 at sundown. Treated
    // as signed this would be -0.136 Amps, which is more believable
    // than 65.4 Amps. We'll treat them as signed which gives
    // enough range (+-32.7) for battery voltags and currents.
    cp = readInt16(cp,eos,n,0.001,fp);

    // signed temperature
    cp = readInt16(cp,eos,1,0.01,(fp ? fp+n : 0));
    n++;

    // remaining fields
    cp = readInt16(cp,eos,nfields-n,1.0,(fp ? fp+n : 0));
    n = nfields;

    if (fp) {
        for (  ; n < osamp->getDataLength(); n++) fp[n] = floatNAN;
        convert(stag, osamp);
    }
    return cp;
}

const char* WisardMote::unpackRnet(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,0.1,fp);
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackRsw(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,0.1,fp);  // multiplies by 0.1
    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackRlw(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields > 0);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,1.0,fp);
    if (fp) {
        fp[0] /= 10.0; // Rpile
        for (unsigned int n = 1; n < nfields; n++) {
            fp[n] /= 100.0; // Tcase and Tdome1-3
        }
        for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
            fp[n] = floatNAN;
        convert(stag, osamp);
    }
    return cp;
}

const char* WisardMote::unpackRlwKZ(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    assert(nfields > 1);
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,1.0,fp);
    if (fp) {
        fp[0] /= 10.0; // Rpile
        fp[1] /= 100.0; // Tcase

        for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
            fp[n] = floatNAN;
        convert(stag, osamp);
    }
    return cp;
}

const char* WisardMote::unpackCNR2(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,0.1,fp);

    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackRsw2(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,0.1,fp);  // multiplies by 0.1

    if (fp) for (unsigned int n = nfields; n < osamp->getDataLength(); n++)
        fp[n] = floatNAN;
    convert(stag, osamp);
    return cp;
}

const char* WisardMote::unpackNR01(const char *cp, const char *eos,
        unsigned int nfields, const struct MessageHeader*,
        SampleTag* stag, SampleT<float>* osamp)
{
    float *fp = 0;
    if (osamp) {
        assert(osamp->getDataLength() >= nfields);
        fp = osamp->getDataPtr();
    }

    cp = readInt16(cp,eos,nfields,1.0,fp);
    if (fp) {
        unsigned int n;
        for (n = 0; n < 4 && n < nfields; n++) {
            fp[n] *= 0.1;   // 2xRsw, 2xRpile
        }
        if (n < nfields) fp[n++] *= 0.01;   // Tcase
        if (n < nfields) fp[n++] *= 0.001;  // Wetness
        for (int i = 0; i < 2 && n < nfields; n++)
            fp[n] *= 0.01;                  // possible extra 2xTcase
        for ( ; n < osamp->getDataLength(); n++)
            fp[n] = floatNAN;
        convert(stag, osamp);
    }
    return cp;
}

void WisardMote::initFuncMap()
{
    if (_functionsMapped) return;
    _unpackMap[0x01] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackPicTime,1);
    _typeNames[0x01] = "PicTime";

    _unpackMap[0x04] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackInt16,1);
    _typeNames[0x04] = "Int16";

    _unpackMap[0x05] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackInt32,1);
    _typeNames[0x05] = "Int32";

    _unpackMap[0x0b] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackAccumSec,1);
    _typeNames[0x0b] = "SecOfYear";

    _unpackMap[0x0c] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackUint32,1);
    _typeNames[0x0c] = "TimerCounter";

    _unpackMap[0x0d] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpack100thSec,1);
    _typeNames[0x0d] = "Time100thSec";

    _unpackMap[0x0e] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpack10thSec,2);
    _typeNames[0x0e] = "Time10thSec";

    _unpackMap[0x0f] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackPicTimeFields,4);
    _typeNames[0x0f] = "PicTimeFields";

    for (int i = 0x10; i < 0x14; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackTRH,3);
        _typeNames[i] = "TRH";
    }

    for (int i = 0x1c; i < 0x20; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackUint16,2);
        _typeNames[i] = "Rain";
    }

    for (int i = 0x20; i < 0x24; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackTsoil,NTSOILS*2);
        _typeNames[i] = "Tsoil";
    }

    for (int i = 0x24; i < 0x28; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackGsoil,1);
        _typeNames[i] = "Gsoil";
    }

    for (int i = 0x28; i < 0x2c; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackQsoil,1);
        _typeNames[i] = "Qsoil";
    }

    for (int i = 0x2c; i < 0x30; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackTP01,5);
        _typeNames[i] = "TP01";
    }

    for (int i = 0x30; i < 0x34; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackInt16,5);
        _typeNames[i] = "5 fields of Int16";
    }

    for (int i = 0x34; i < 0x38; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackInt16,4);
        _typeNames[i] = "4 fields of Int16";
    }

    for (int i = 0x38; i < 0x3c; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackInt16,1);
        _typeNames[i] = "1 field of Int16, often Wetness";
    }

    for (int i = 0x3c; i < 0x40; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackInt16,1);
        _typeNames[i] = "Infra-red surface temperature";
    }

    _unpackMap[0x40] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackStatus,1);
    _typeNames[0x40] = "Status";

    _unpackMap[0x41] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackXbee,7);
    _typeNames[0x41] = "Xbee Status";

    _unpackMap[0x49] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackPower,6);
    _typeNames[0x49] = "Power Monitor";

    for (int i = 0x4c; i < 0x50; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackNR01,8);
        _typeNames[i] = "Hukseflux NR01";
    }

    for (int i = 0x50; i < 0x54; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRnet,1);
        _typeNames[i] = "Q7 Net Radiometer";
    }

    for (int i = 0x54; i < 0x58; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRsw,1);
        _typeNames[i] = "Uplooking Pyranometer (Rsw.in)";
    }

    for (int i = 0x58; i < 0x5c; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRsw,1);
        _typeNames[i] = "Downlooking Pyranometer (Rsw.out)";
    }

    for (int i = 0x5c; i < 0x60; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRlw,5);
        _typeNames[i] = "Uplooking Epply Pyrgeometer (Rlw.in)";
    }

    for (int i = 0x60; i < 0x64; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRlw,5);
        _typeNames[i] = "Downlooking Epply Pyrgeometer (Rlw.out)";
    }

    for (int i = 0x64; i < 0x68; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRlwKZ,2);
        _typeNames[i] = "Uplooking K&Z Pyrgeometer (Rlw.in)";
    }

    for (int i = 0x68; i < 0x6c; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRlwKZ,2);
        _typeNames[i] = "Downlooking K&Z Pyrgeometer (Rlw.out)";
    }

    for (int i = 0x6c; i < 0x70; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackCNR2,2);
        _typeNames[i] = "CNR2 Net Radiometer";
    }

    for (int i = 0x70; i < 0x74; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRsw2,2);
        _typeNames[i] = "Diffuse shortwave";
    }

    for (int i = 0x74; i < 0x78; i++) {
        _unpackMap[i] = pair<WisardMote::unpack_t,unsigned int>(&WisardMote::unpackRsw,1);
        _typeNames[i] = "Photosynthetically active radiation";
    }

    _functionsMapped = true;
}

//  %c will be replaced by 'a','b','c', or 'd' for the range of sensor types
//  %m in the variable names below will be replaced by the decimal mote number
SampInfo WisardMote::_samps[] = {
    { 0x01, 0x01, {
                      { "PicTime.m%m", "secs","PIC Time", "$ALL_DEFAULT" },
                      { 0, 0, 0, 0 }
                  }, WST_IGNORED
    },
    { 0x0b, 0x0b, {
                      { "Clockdiff.m%m", "secs","Time difference: sampleTimeTag - moteTime", "$ALL_DEFAULT" },
                      { 0, 0, 0, 0 }
                  }, WST_IGNORED
    },
    { 0x0c, 0x0c, {
                      { "Timer.m%m", "","Some sort of timer counter", "$ALL_DEFAULT" },
                      { 0, 0, 0, 0 }
                  }, WST_IGNORED
    },
    { 0x0d, 0x0d, {
                      { "Clock100.m%m", "","Wizard 100th sec", "$ALL_DEFAULT" },
                      { 0, 0, 0, 0 }
                  }, WST_IGNORED
    },
    { 0x0e, 0x0e, {
                      { "Tdiff.m%m", "secs","Time difference, adam-mote", "$ALL_DEFAULT" },
                      { "Tdiff2.m%m", "secs", "Time difference, adam-mote-first_diff", "$ALL_DEFAULT" },
                      { 0, 0, 0, 0 }
                  }, WST_IGNORED
    },
    { 0x1c, 0x1f, {
                      {"Raintip", "#", "Rain tip", "$RAIN_RANGE" },
                      {"Rainaccum", "#", "Accumulated rain tips", "$RAIN_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x20, 0x23, {
                      {"Tsoil.0.6cm.%c_m%m", "degC", "Soil Temperature", "$TSOIL_RANGE" },
                      {"Tsoil.1.9cm.%c_m%m", "degC", "Soil Temperature", "$TSOIL_RANGE" },
                      {"Tsoil.3.1cm.%c_m%m", "degC", "Soil Temperature", "$TSOIL_RANGE" },
                      {"Tsoil.4.4cm.%c_m%m", "degC", "Soil Temperature", "$TSOIL_RANGE" },
                      {"dTsoil_dt.0.6cm.%c_m%m", "degC/s", "Time derivative of soil temp", "$DTSOIL_RANGE" },
                      {"dTsoil_dt.1.9cm.%c_m%m", "degC/s", "Time derivative of soil temp", "$DTSOIL_RANGE" },
                      {"dTsoil_dt.3.1cm.%c_m%m", "degC/s", "Time derivative of soil temp", "$DTSOIL_RANGE" },
                      {"dTsoil_dt.4.4cm.%c_m%m", "degC/s", "Time derivative of soil temp", "$DTSOIL_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x24, 0x27, {
                      { "Gsoil.%c_m%m", "W/m^2", "Soil Heat Flux", "$GSOIL_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x28, 0x2b, {
                      { "Qsoil.%c_m%m", "vol%", "Soil Moisture", "$QSOIL_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x2c, 0x2f, {
                      { "Vheat.%c_m%m", "V", "TP01 heater voltage", "$VHEAT_RANGE" },
                      { "Vpile.on.%c_m%m", "microV", "TP01 thermopile after heating", "$VPILE_RANGE" },
                      { "Vpile.off.%c_m%m", "microV", "TP01 thermopile before heating", "$VPILE_RANGE" },
                      { "Tau63.%c_m%m", "secs", "TP01 time to decay to 37% of Vpile.on-Vpile.off", "$TAU63_RANGE" },
                      { "lambdasoil.%c_m%m", "W/mDegk", "TP01 derived thermal conductivity", "$LAMBDA_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x30, 0x33, {
                      { "G5_c1.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { "G5_c2.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { "G5_c3.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { "G5_c4.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { "G5_c5.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x34, 0x37, {
                      { "G4_c1.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { "G4_c2.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { "G4_c3.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { "G4_c4.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x38, 0x3b, {
                      { "G1_c1.%c_m%m", "",	"", "$ALL_DEFAULT" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x3c, 0x3f, {
                      { "Tsfc.%c_m%m", "W/m^2", "Infra-red surface temperature", "$T_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x41, 0x41, {
                      { "XbeeStatus.%m", "", "Xbee status", "-10 10" },
                      {0, 0, 0, 0 }
                  }, WST_IGNORED
    },
    { 0x49, 0x49, {
                      { "Vdsm.m%m", "V", "System voltage", "$VIN_RANGE" },
                      { "Idsm.m%m", "A", "Load current", "$IIN_RANGE" },
                      { "Icharge.m%m", "A", "Charging current", "$IIN_RANGE" },
                      { "Tcharge.m%m", "degC", "Charging system temperature", "$T_RANGE" },
                      {0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x4c, 0x4f, {
                      { "Rsw.in.%c_%m", "W/m^2", "Incoming Short Wave, Hukseflux NR01", "$RSWIN_RANGE" },
                      { "Rsw.out.%c_%m", "W/m^2", "Outgoing Short Wave, Hukseflux NR01", "$RSWOUT_RANGE" },
                      { "Rpile.in.%c_%m", "W/m^2", "Incoming Thermopile, Hukseflux NR01", "$RPILE_RANGE" },
                      { "Rpile.out.%c_%m", "W/m^2", "Outgoing Thermopile, Hukseflux NR01", "$RPILE_RANGE" },
                      { "Tcase.%c_%m", "degC", "Average case temperature, Hukseflux NR01", "$T_RANGE" },
                      { "Wetness.%c_%m", "V", "Leaf wetness", "$WETNESS_RANGE" },
                      { "Tcase.in.%c_%m", "degC", "Incoming case temperature, Hukseflux NR01", "$T_RANGE" },
                      { "Tcase.out.%c_%m", "degC", "Outgoing case temperature, Hukseflux NR01", "$T_RANGE" },
                      {0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x50, 0x53, {
                      { "Rnet.%c_m%m", "W/m^2", "Net Radiation", "$RNET_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x54, 0x57, {
                      { "Rsw.in.%c_m%m", "W/m^2", "Incoming Short Wave", "$RSWIN_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x58, 0x5b, {
                      { "Rsw.out.%c_m%m", "W/m^2", "Outgoing Short Wave", "$RSWOUT_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x5c, 0x5f, {
                      { "Rpile.in.%c_m%m", "W/m^2", "Epply pyrgeometer thermopile, incoming", "$RPILE_RANGE" },
                      { "Tcase.in.a_m%m", "degC", "Epply case temperature, incoming", "$TCASE_RANGE" },
                      { "Tdome1.in.a_m%m", "degC", "Epply dome temperature #1, incoming", "$TDOME_RANGE" },
                      { "Tdome2.in.a_m%m", "degC", "Epply dome temperature #2, incoming", "$TDOME_RANGE" },
                      { "Tdome3.in.a_m%m", "degC", "Epply dome temperature #3, incoming", "$TDOME_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x60, 0x63, {
                      { "Rpile.out.%c_m%m", "W/m^2", "Epply pyrgeometer thermopile, outgoing", "$RPILE_RANGE" },
                      { "Tcase.out.%c_m%m", "degC", "Epply case temperature, outgoing", "$TCASE_RANGE" },
                      { "Tdome1.out.%c_m%m", "degC", "Epply dome temperature #1, outgoing", "$TDOME_RANGE" },
                      { "Tdome2.out.%c_m%m", "degC", "Epply dome temperature #2, outgoing", "$TDOME_RANGE" },
                      { "Tdome3.out.%c_m%m", "degC", "Epply dome temperature #3, outgoing", "$TDOME_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x64, 0x67, {
                      { "Rpile.in.%ckz_m%m", "W/m^2", "K&Z pyrgeometer thermopile, incoming", "$RPILE_RANGE" },
                      { "Tcase.in.%ckz_m%m", "degC", "K&Z case temperature, incoming", "$TCASE_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x68, 0x6b, {
                      { "Rpile.out.%ckz_m%m", "W/m^2", "K&Z pyrgeometer thermopile, outgoing", "$RPILE_RANGE" },
                      { "Tcase.out.%ckz_m%m", "degC", "K&Z case temperature, outgoing", "$TCASE_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x6c, 0x6f, {
                      { "Rsw.net.%c_m%m", "W/m^2", "CNR2 net short-wave radiation", "$RSWNET_RANGE" },
                      { "Rlw.net.%c_m%m", "W/m^2", "CNR2 net long-wave radiation", "$RLWNET_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x70, 0x73, {
                      { "Rsw.dfs.%c_m%m", "W/m^2", "Diffuse short wave", "$RSWIN_RANGE" },
                      { "Rsw.direct.%c_m%m", "W/m^2", "Direct short wave", "$RSWIN_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0x74, 0x77, {
                      { "Rpar.%c_m%m", "W/m^2", "Photosynthetically active radiation", "$RSWIN_RANGE" },
                      { 0, 0, 0, 0 }
                  }, WST_NORMAL
    },
    { 0, 0, { {0,0,0,0} }, WST_NORMAL },
};
