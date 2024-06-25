/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
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

#include <nidas/core/PhysConstants.h>
#include <nidas/core/AsciiSscanf.h>
#include <nidas/core/CalFile.h>
#include <nidas/core/Parameter.h>
#include <nidas/core/Project.h>
#include <nidas/core/Variable.h>
#include <nidas/util/Logger.h>

#include "Wind3D.h"
#include "metek.h"

#ifdef HAVE_LIBGSL
#include <gsl/gsl_linalg.h>
#endif


#include <sstream>

using namespace nidas::core;
using namespace nidas::dynld::isff;
using std::vector;
using std::string;
using std::list;
using std::ostringstream;

namespace n_u = nidas::util;

NIDAS_CREATOR_FUNCTION_NS(isff,Wind3D)


namespace nidas {
namespace dynld {
namespace isff {


class Wind3D_impl
{
public:
#ifdef HAVE_LIBGSL
    Wind3D_impl():
        _atMatrixGSL(gsl_matrix_alloc(3,3)),
        _atPermutationGSL(gsl_permutation_alloc(3))
    {}

    ~Wind3D_impl()
    {
        gsl_matrix_free(_atMatrixGSL);
        gsl_permutation_free(_atPermutationGSL);
    }

    gsl_matrix* _atMatrixGSL;
    gsl_permutation* _atPermutationGSL;
    bool _enabled{true};
#else
    bool _enabled{false};
#endif

    Wind3D_impl(const Wind3D_impl&) = delete;
    Wind3D_impl& operator=(const Wind3D_impl&) = delete;
};



Wind3D::Wind3D():
    _allBiasesNaN(false),
    _despike(false),
    _metek(false),
    _rotator(), _tilter(), _orienter(),
    _tcOffset(0.0),_tcSlope(1.0),
    _horizontalRotation(true),_tiltCorrection(true),
    _sampleId(0),
    _diagIndex(-1), _ldiagIndex(-1),
    _spdIndex(-1), _dirIndex(-1),
    _noutVals(0),
    _numParsed(0),
    _oaCalFile(0),
    _atCalFile(0),
    _atMatrix(),
    _atInverse(),
    _shadowFactor(0.0),
    _impl{new Wind3D_impl{}}
{
    for (int i = 0; i < 3; i++) {
        _bias[i] = 0.0;
    }
    for (int i = 0; i < 4; i++) {
        _ttlast[i] = 0;
    }
}

// This must be defined here where the impl type is known completely, as
// opposed to defaulting the destructor in the header where it is incomplete.
Wind3D::~Wind3D()
{
}

void Wind3D::despike(dsm_time_t tt,
	float* uvwt,int n,bool* spikeOrMissing) throw()
{
    vector<double> dvec(n);	// despiked data
    double* duvwt = &dvec.front();
    /*
     * Despike data
     */
    for (int i=0; i < n; i++) {
        /* Restart statistics after data gap. */
        if (tt - _ttlast[i] > DATA_GAP_USEC) _despiker[i].reset();

        /* Despike status, 1=despiked, 0=not despiked */
        spikeOrMissing[i] = std::isnan(uvwt[i]);
        duvwt[i] = _despiker[i].despike(uvwt[i],spikeOrMissing+i);
        if (!spikeOrMissing[i]) _ttlast[i] = tt;
    }

    // If user wants despiked data, copy results back to passed array.
    if (getDespike()) {
        for (int i=0; i < n; i++) {
            uvwt[i] = duvwt[i];
        }
    }
}


void Wind3D::setBias(int b, double val)
{
    int nnan = 0;
    for (int i = 0; i < 3; i++)
    {
        if (b == i) _bias[i] = val;
        if (std::isnan(_bias[i])) nnan++;
    }
    _allBiasesNaN = (nnan == 3);
}


void Wind3D::readOffsetsAnglesCalFile(dsm_time_t tt) throw()
{
    // Read CalFile of bias, rotation angles, and orientation.
    // u.off   v.off   w.off    theta  phi    Vazimuth  t.off  t.slope  orientation
    if (_oaCalFile) {
        while(tt >= _oaCalFile->nextTime().toUsecs()) {
            float d[8];
            try {
                n_u::UTime calTime;
                std::vector<std::string> cfields;
                int nd = sizeof d/sizeof(d[0]);
                int n = _oaCalFile->readCF(calTime, d, nd, &cfields);
                for (int i = 0; i < 3 && i < n; i++)
                    setBias(i, d[i]);

                if (n > 3) setLeanDegrees(d[3]);
                if (n > 4) setLeanAzimuthDegrees(d[4]);
                if (n > 5) setVazimuth(d[5]);
                /* Old versions of CSAT calibration files contained
                 * a byteShift value in column 7 - which this version
                 * of the software does not support.  This software
                 * instead will expect column 7 and 8 to contain
                 * the offset and slope for the correction to virtual
                 * temperature.
                 */
                if (n > 7) {
                    setTcOffset(d[6]);
                    setTcSlope(d[7]);
                }
                /*
                 * Allow the orientation parameter to be specified as a
                 * string in the 9th column.
                 */
                if (cfields.size() > 8)
                {
                    _orienter.setOrientation(cfields[8], getName());
                }
            }
            catch(const n_u::EOFException& e)
            {
            }
            catch(const n_u::IOException& e)
            {
                n_u::Logger::getInstance()->log(LOG_WARNING,"%s: %s",
                    _oaCalFile->getCurrentFileName().c_str(),e.what());
                for (int i = 0; i < 3; i++)
                    setBias(i, floatNAN);
                setLeanDegrees(floatNAN);
                setLeanAzimuthDegrees(floatNAN);
                setVazimuth(floatNAN);
                _oaCalFile = 0;
                break;
            }
            catch(const n_u::ParseException& e)
            {
                n_u::Logger::getInstance()->log(LOG_WARNING,"%s: %s",
                    _oaCalFile->getCurrentFileName().c_str(),e.what());
                for (int i = 0; i < 3; i++)
                    setBias(i, floatNAN);
                setLeanDegrees(floatNAN);
                setLeanAzimuthDegrees(floatNAN);
                setVazimuth(floatNAN);
                _oaCalFile = 0;
                break;
            }
        }
    }
}

void Wind3D::applyOrientation(dsm_time_t tt, float* uvwt) throw()
{
    readOffsetsAnglesCalFile(tt);
    _orienter.applyOrientation(uvwt);
}

void Wind3D::offsetsTiltAndRotate(dsm_time_t tt, float* uvwt) throw()
{
    readOffsetsAnglesCalFile(tt);

    // bias removal is part of the tilt correction.
    if (_tiltCorrection) {
        for (int i=0; i<3; i++) uvwt[i] -= _bias[i];
        if (!_tilter.isIdentity()) _tilter.rotate(uvwt,uvwt+1,uvwt+2);
    }
    // Apply correction to Tc
    uvwt[3] = uvwt[3] * _tcSlope + _tcOffset;
    if (_horizontalRotation) _rotator.rotate(uvwt,uvwt+1);
}

void Wind3D::validate()
{
    SerialSensor::validate();

    parseParameters();

    checkSampleTags();
}

void Wind3D::parseParameters()
{
    const Parameter* parameter =
        Project::getInstance()->getParameter("wind3d_horiz_rotation");
    if (parameter) {
        if ((parameter->getType() != Parameter::BOOL_PARAM &&
            parameter->getType() != Parameter::INT_PARAM &&
            parameter->getType() != Parameter::FLOAT_PARAM) ||
            parameter->getLength() != 1)
            throw n_u::InvalidParameterException(getName(),parameter->getName(),
                "should be a boolean or integer (FALSE=0,TRUE=1) of length 1");
        bool val = (bool) parameter->getNumericValue(0);
        setDoHorizontalRotation(val);
    }

    parameter = Project::getInstance()->getParameter("wind3d_tilt_correction");
    if (parameter) {
        if ((parameter->getType() != Parameter::BOOL_PARAM &&
            parameter->getType() != Parameter::INT_PARAM &&
            parameter->getType() != Parameter::FLOAT_PARAM) ||
            parameter->getLength() != 1)
            throw n_u::InvalidParameterException(getName(),parameter->getName(),
                "should be a boolean or integer (FALSE=0,TRUE=1) of length 1");
        bool val = (bool) parameter->getNumericValue(0);
        setDoTiltCorrection(val);
    }

    const list<const Parameter*>& params = getParameters();
    list<const Parameter*>::const_iterator pi = params.begin();

    for ( ; pi != params.end(); ++pi) {
        parameter = *pi;

        if (parameter->getName() == "biases")
        {
            for (int i = 0; i < 3 && i < parameter->getLength(); i++)
            {
                setBias(i, parameter->getNumericValue(i));
            }
        }
        else if (parameter->getName() == "Vazimuth") {
            setVazimuth(parameter->getNumericValue(0));
        }
        else if (parameter->getName() == "despike") {
            setDespike(parameter->getNumericValue(0) != 0.0);
        }
        else if (parameter->getName() == "outlierProbability") {
            setOutlierProbability(parameter->getNumericValue(0));
        }
        else if (parameter->getName() == "discLevelMultiplier") {
            setDiscLevelMultiplier(parameter->getNumericValue(0));
        }
        else if (parameter->getName() == "lean") {
            setLeanDegrees(parameter->getNumericValue(0));
        }
        else if (parameter->getName() == "leanAzimuth") {
            setLeanAzimuthDegrees(parameter->getNumericValue(0));
        }
        else if (parameter->getName() == "wind3d_horiz_rotation") {
            if ((parameter->getType() != Parameter::BOOL_PARAM &&
                parameter->getType() != Parameter::INT_PARAM &&
                parameter->getType() != Parameter::FLOAT_PARAM) ||
                parameter->getLength() != 1)
                throw n_u::InvalidParameterException(getName(),parameter->getName(),
                    "should be a boolean or integer (FALSE=0,TRUE=1) of length 1");
            setDoHorizontalRotation((bool)parameter->getNumericValue(0));
        }
        else if (parameter->getName() == "wind3d_tilt_correction") {
            if ((parameter->getType() != Parameter::BOOL_PARAM &&
                parameter->getType() != Parameter::INT_PARAM &&
                parameter->getType() != Parameter::FLOAT_PARAM) ||
                parameter->getLength() != 1)
                throw n_u::InvalidParameterException(getName(),parameter->getName(),
                    "should be a boolean or integer (FALSE=0,TRUE=1) of length 1");
            setDoTiltCorrection((bool)parameter->getNumericValue(0));
        }
        else if (_orienter.handleParameter(parameter, getName())) {
            // pass
        }
        else if (parameter->getName() == "shadowFactor") {
            if (parameter->getType() != Parameter::FLOAT_PARAM ||
                    parameter->getLength() != 1)
                    throw n_u::InvalidParameterException(getName(),
                            "shadowFactor","must be one float");
            _shadowFactor = parameter->getNumericValue(0);
            if (!_impl->_enabled && _shadowFactor != 0.0)
                    throw n_u::InvalidParameterException(getName(),
                        "shadowFactor",
                        "must be zero since there is no GSL support");
        }
        else if (parameter->getName() == "metek") {
            if ((parameter->getType() != Parameter::BOOL_PARAM &&
                parameter->getType() != Parameter::INT_PARAM &&
                parameter->getType() != Parameter::FLOAT_PARAM) ||
                parameter->getLength() != 1)
                throw n_u::InvalidParameterException(getName(),parameter->getName(), "'metek' should be a boolean or integer (FALSE=0,TRUE=1) of length 1");
            setMetek(parameter->getNumericValue(0)); 
        }
        else if (parameter->getName() == "oversample");
        else if (parameter->getName() == "soniclog");
        else if (parameter->getName() == "maxShadowAngle");
        else if (parameter->getName() == "expectedCounts");
        else if (parameter->getName() == "maxMissingFraction");
        else if (parameter->getName() == "bandwidth");
        else if (parameter->getName() == "configure");
        else if (parameter->getName() == "checkCounter");
        else throw n_u::InvalidParameterException(
             getName(),"parameter",parameter->getName());
    }

    _oaCalFile =  getCalFile("offsets_angles");
    if (!_oaCalFile) _oaCalFile =  getCalFile("");

    // transformation matrix from non-orthogonal axes to UVW
    _atCalFile = getCalFile("abc2uvw");
}

void Wind3D::checkSampleTags()
{
    list<SampleTag*>& tags= getSampleTags();
    list<SampleTag*>::const_iterator si = tags.begin();

    _noutVals = 4;  // u,v,w,tc
    _numParsed = 4;  // u,v,w,tc

    for ( ; si != tags.end(); ++si) {
        const SampleTag* stag = *si;
        _sampleId = stag->getId();

        VariableIterator vi = stag->getVariableIterator();
        for (unsigned int i = 0; vi.hasNext(); i++) {
            const Variable* var = vi.next();
            const string& vname = var->getName();
            if (vname.length() > 2 && vname.substr(0,3) == "spd") {
                _spdIndex = i;
                if (i >= _noutVals) _noutVals = i + 1;
            }
            else if (vname.length() > 2 && vname.substr(0,3) == "dir") {
                _dirIndex = i;
                if (i >= _noutVals) _noutVals = i + 1;
            }
            else if (vname == "diag" || (vname.length() > 4 && vname.substr(0,5) == "diag.")) {
                _diagIndex = i;
                if (i >= _noutVals) _noutVals = i + 1;
                if (i >= _numParsed) _numParsed = i + 1;
            }
            else if (vname == "status" || (vname.length() > 5 && vname.substr(0,6) == "status.")) {
                _diagIndex = i;
                if (i >= _noutVals) _noutVals = i + 1;
            }
            else if (vname == "ldiag" || (vname.length() > 5 && vname.substr(0,6) == "ldiag.")) {
                _ldiagIndex = i;
                if (i >= _noutVals) _noutVals = i + 1;
            }
        }
    }
}

void Wind3D::validateSscanfs()
{
    const std::list<AsciiSscanf*>& sscanfers = getScanfers();

    // binary sensor
    if (sscanfers.empty()) return;

    std::list<AsciiSscanf*>::const_iterator si = sscanfers.begin();

    for ( ; si != sscanfers.end(); ++si) {
        AsciiSscanf* sscanf = *si;
        unsigned int nf = sscanf->getNumberOfFields();

        if (nf < _numParsed) {
            ostringstream ost;
            ost << "number of scanf fields (" << nf <<
                ") is less than the number expected (" << _numParsed << ")";
            throw n_u::InvalidParameterException(getName(),"scanfFormat",ost.str());
        }
    }
}

void Wind3D::transducerShadowCorrection(dsm_time_t tt,float* uvw)
{
#ifdef HAVE_LIBGSL
    if (!_atCalFile || _shadowFactor == 0.0 || std::isnan(_atMatrix[0][0]))
        return;

    double spd2 = uvw[0] * uvw[0] + uvw[1] * uvw[1] + uvw[2] * uvw[2];

    /* If one component is missing, do we mark all as missing?
     * This should not be a common occurance, but since this data
     * is commonly averaged, it wouldn't be obvious in the averages
     * whether some values were not being shadow corrected. So we'll
     * let one NAN "spoil the barrel".
     */
    if (std::isnan(spd2)) {
        for (int i = 0; i < 3; i++) uvw[i] = floatNAN;
        return;
    }

    getTransducerRotation(tt);

    double abc[3];

    // rotate from UVW to non-orthogonal transducer coordinates, ABC
    for (int i = 0; i < 3; i++) {
        abc[i] = 0.0;
        for (int j = 0; j < 3; j++)
            abc[i] += uvw[j] * _atInverse[i][j];
    }

    // apply shadow correction to winds in transducer coordinates
    for (int i = 0; i < 3; i++) {
        double x = abc[i];
        double sintheta = ::sqrt(1.0 - x * x / spd2);
        abc[i] = x / (1.0 - _shadowFactor + _shadowFactor * sintheta);
    }

    // cerr << "uvw=" << uvw[0] << ' ' << uvw[1] << ' ' << uvw[2] << endl;

    // rotate back to uvw coordinates
    for (int i = 0; i < 3; i++) {
        uvw[i] = 0.0;
        for (int j = 0; j < 3; j++)
            uvw[i] += abc[j] * _atMatrix[i][j];
    }

    // cerr << "uvw=" << uvw[0] << ' ' << uvw[1] << ' ' << uvw[2] << endl;
#endif
}

void Wind3D::getTransducerRotation(dsm_time_t tt)
{
#ifdef HAVE_LIBGSL
    if (_atCalFile) {
        while(tt >= _atCalFile->nextTime().toUsecs()) {

            try {
                n_u::UTime calTime;
                float data[3*3];
                int n = _atCalFile->readCF(calTime, data,sizeof(data)/sizeof(data[0]));
                if (n != 9) {
                    if (n != 0)
                        WLOG(("%s: short record of less than 9 values at line %d",
                            _atCalFile->getCurrentFileName().c_str(),
                            _atCalFile->getLineNumber()));
                    continue;
                }
                const float* dp = data;
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        gsl_matrix_set(_impl->_atMatrixGSL, i, j, *dp);
                        _atMatrix[i][j] = *dp++;
                    }
                    // cerr << _atMatrix[i][0] << ' ' << _atMatrix[i][1] << ' ' << _atMatrix[i][2] << endl;
                }
                int sign;
                gsl_linalg_LU_decomp(_impl->_atMatrixGSL, _impl->_atPermutationGSL, &sign);
                gsl_matrix* inverseGSL = gsl_matrix_alloc(3,3);
                gsl_linalg_LU_invert(_impl->_atMatrixGSL, _impl->_atPermutationGSL, inverseGSL);
                for (int i = 0; i < 3; i++) {
                    for (int j = 0; j < 3; j++) {
                        _atInverse[i][j] = gsl_matrix_get(inverseGSL,i,j);
                    }
                }
                gsl_matrix_free(inverseGSL);
            }
            catch(const n_u::EOFException& e)
            {
            }
            catch(const n_u::IOException& e)
            {
                WLOG(("%s: %s", _atCalFile->getCurrentFileName().c_str(),e.what()));
                _atMatrix[0][0] = floatNAN;
                _atCalFile = 0;
                break;
            }
            catch(const n_u::ParseException& e)
            {
                WLOG(("%s: %s", _atCalFile->getCurrentFileName().c_str(),e.what()));
                _atMatrix[0][0] = floatNAN;
                _atCalFile = 0;
                break;
            }
        }
    }
#endif
}

bool Wind3D::process(const Sample* samp,
	std::list<const Sample*>& results)
{
    std::list<const Sample*> parseResults;

    SerialSensor::process(samp,parseResults);

    if (parseResults.empty()) return false;

    // result from base class parsing of ASCII
    const Sample* psamp = parseResults.front();

    unsigned int nParsedVals = psamp->getDataLength();
    const float* pdata = (const float*) psamp->getConstVoidDataPtr();

    const float* pend = pdata + nParsedVals;

    // u,v,w,tc,diag
    float uvwtd[5];

    for (unsigned int i = 0; i < sizeof(uvwtd) / sizeof(uvwtd[0]); i++) {
        if (pdata < pend) uvwtd[i] = *pdata++;
        else uvwtd[i] = floatNAN;
    }
    
    //metek has a correction before we start applying other corrections
    if (_metek) {
        nidas::dynld::isff::metek::Apply3DCorrect(uvwtd);
    }

    // get diagnostic value from parsed sample
    float diagval = floatNAN;
    bool diagOK = false;

    if (_diagIndex >= 0 && (unsigned) _diagIndex < sizeof(uvwtd)/sizeof(uvwtd[0])) {
        diagval = uvwtd[_diagIndex];
        diagOK = !std::isnan(diagval) && diagval == 0.0;
    }

    if (getDespike()) {
        bool spikes[4] = {false,false,false,false};
        despike(samp->getTimeTag(),uvwtd,4,spikes);
    }

    // apply shadow correction before correcting for unusual orientation
    transducerShadowCorrection(samp->getTimeTag(), uvwtd);

    applyOrientation(samp->getTimeTag(), uvwtd);

    offsetsTiltAndRotate(samp->getTimeTag(), uvwtd);

    // new sample
    SampleT<float>* wsamp = getSample<float>(_noutVals);

    // First fill the output sample with the parsed values.  The
    // CharacterSensor::process() method takes care of filling any unparsed
    // values in the output sample with nan.  This way any extra variables
    // beyond the first 5 standard sonic variables (u,v,w,tc,diag) will get
    // passed on to the output.  Any derived sonic variables
    // (ldiag,dir,spd) will overwrite the value at their respective index.
    memcpy(wsamp->getVoidDataPtr(), psamp->getConstVoidDataPtr(),
           std::min(psamp->getDataByteLength(),
                    wsamp->getDataByteLength()));

    // any defined time lag has been applied by SerialSensor
    wsamp->setTimeTag(psamp->getTimeTag());
    wsamp->setId(_sampleId);

    // finished with parsed sample
    psamp->freeReference();

    float* dout = wsamp->getDataPtr();

    float *dptr = dout;

    // Do not copy more than will fit into the output sample nor more than
    // exists in the uvwtd array.
    int nvals = std::min(_noutVals, (unsigned int)(sizeof(uvwtd)/sizeof(uvwtd[0])));

    memcpy(dptr, uvwtd, sizeof(float) * nvals);

    // If user asks for ldiag, use it to flag data values
    if (_ldiagIndex >= 0) {
        dout[_ldiagIndex] = (float)!diagOK;
        if (!diagOK) {
            for (unsigned int i = 0; i < 4 && i < _noutVals; i++) {
                dout[i] = floatNAN;
            }
        }
    }

    if (_spdIndex >= 0) {
        dout[_spdIndex] = sqrt(dout[0] * dout[0] + dout[1] * dout[1]);
    }
    if (_dirIndex >= 0) {
        dout[_dirIndex] = n_u::dirFromUV(dout[0], dout[1]);
    }

    results.push_back(wsamp);
    return true;
}


} // isff
} // dynld
} // nidas
