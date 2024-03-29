// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
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

#ifndef NIDAS_CORE_SAMPLEPOOL_H
#define NIDAS_CORE_SAMPLEPOOL_H

#include <nidas/util/ThreadSupport.h>
#include "SampleLengthException.h"
#include <nidas/util/Logger.h>

#include <cassert>
#include <cstring> // memcpy()
#include <vector>
#include <list>
#include <iostream>

namespace nidas { namespace core {

class SamplePoolInterface
{
public:
    virtual int getNSamplesAlloc() const = 0;
    virtual int getNSamplesOut() const = 0;
    virtual int getNSmallSamplesIn() const = 0;
    virtual int getNMediumSamplesIn() const = 0;
    virtual int getNLargeSamplesIn() const = 0;

    /**
     * SamplePool singletons for various types and sizes are created and added
     * to the SamplePools class through their getInstance() method.  Those
     * instances must be deleted through the corresponding deleteInstance(),
     * otherwise the global instance pointer is not reset when a pool
     * singleton is deleted.
     */
    virtual void deletePoolInstance() = 0;

protected:
    /**
     * Define a virtual but protected desctructor, so a SamplePoolInterface
     * subclass cannot be deleted through a SamplePoolInterface pointer.  The
     * subclasses are all singleton classes and thus their destructors are
     * private.
     */
    virtual ~SamplePoolInterface() {}

};

class SamplePools
{
public:
    static SamplePools* getInstance();

    static void deleteInstance();

    /**
      * Get a copy of the current list of SamplePools.
      * In the general case a SamplePool could be deleted
      * after this list was copied, and the pointer to that SamplePool
      * would be invalid. However, in practice, these SamplePools
      * stay around until the process is finished, so there
      * shouldn't be a problem.
      */
    std::list<SamplePoolInterface*> getPools() const;

    void addPool(SamplePoolInterface* pool);

    void removePool(SamplePoolInterface* pool);

private:
    SamplePools(): _poolsLock(),_pools() {}

    ~SamplePools();

    static SamplePools* _instance;

    static nidas::util::Mutex _instanceLock;

    mutable nidas::util::Mutex _poolsLock;

    std::list<SamplePoolInterface*> _pools;
};

/**
 * A pool of Samples.  Actually three pools, containing
 * samples segregated by size.  A SamplePool can used
 * as a singleton, and accessed from anywhere, via the
 * getInstance() static member function.
 */
template <typename SampleType>
class SamplePool : public SamplePoolInterface
{
public:

    /**
     * Get a pointer to the singleton instance.
     */
    static SamplePool *getInstance();

    /**
     * Singleton cleanup on program exit.
     */
    static void deleteInstance();

    /**
     * Implementation from SamplePoolInterface which deletes the instance for
     * this particular SamplePoolInterface subclass.
     */
    void deletePoolInstance()
    {
        SamplePool::deleteInstance();
    }

    /**
     * Get a sample of at least len elements from the pool.
     */
    SampleType *getSample(unsigned int len);

    /**
     * Return a sample to the pool.
     */
    void putSample(const SampleType *);

    int getNSamplesAlloc() const { return _nsamplesAlloc; }

    int getNSamplesOut() const { return _nsamplesOut; }

    int getNSmallSamplesIn() const { return _nsmall; }

    int getNMediumSamplesIn() const { return _nmedium; }

    int getNLargeSamplesIn() const { return _nlarge; }

private:

    SamplePool();

    ~SamplePool();

    static SamplePool* _instance;

    static nidas::util::Mutex _instanceLock;

    SampleType *getSample(SampleType** vec,int *veclen, unsigned int len);
    void putSample(const SampleType *,SampleType*** vecp,int *veclen, int* nalloc);

    SampleType** _smallSamples;
    SampleType** _mediumSamples;
    SampleType** _largeSamples;

    int _smallSize;
    int _mediumSize;
    int _largeSize;

    nidas::util::Mutex _poolLock;

    /**
     * maximum number of elements in a small sample
     */
    const static unsigned int SMALL_SAMPLE_MAXSIZE = 64;

    /**
     * maximum number of elements in a medium sized sample
     */
    const static unsigned int MEDIUM_SAMPLE_MAXSIZE = 512;

    /**
     * No copying.
     */
    SamplePool(const SamplePool&);

    /**
     * No assignment.
     */
    SamplePool& operator=(const SamplePool&);

public:
    int _nsmall;
    int _nmedium;
    int _nlarge;

    int _nsamplesOut;

    int _nsamplesAlloc;

};

/* static */
template<class SampleType>
    SamplePool<SampleType>* SamplePool<SampleType>::_instance = 0;

/* static */
template<class SampleType>
    nidas::util::Mutex SamplePool<SampleType>::_instanceLock = nidas::util::Mutex();

/* static */
template<class SampleType>
SamplePool<SampleType> *SamplePool<SampleType>::getInstance()
{
    if (!_instance) {
        nidas::util::Synchronized pooler(_instanceLock);
        if (!_instance) {
            _instance = new SamplePool<SampleType>();
            SamplePools::getInstance()->addPool(_instance);
        }
    }
    return _instance;
}

/* static */
template<class SampleType>
void SamplePool<SampleType>::deleteInstance()
{
    nidas::util::Synchronized pooler(_instanceLock);
    delete _instance;
    _instance = 0;
}

template<class SampleType>
    SamplePool<SampleType>::SamplePool():
        _smallSamples(0), _mediumSamples(0), _largeSamples(0),
        _smallSize(0), _mediumSize(0), _largeSize(0),
        _poolLock(),
        _nsmall(0),_nmedium(0),_nlarge(0),_nsamplesOut(0),_nsamplesAlloc(0)
{
    // Initial size of pool of small samples around 16K bytes
    _smallSize = 16384 / (sizeof(SampleType) + SMALL_SAMPLE_MAXSIZE * SampleType::sizeofDataType());
    // When we expand the size of the pool, we expand by 50%
    // so minimum size should be at least 2.
    if (_smallSize < 2) _smallSize = 2;

    _mediumSize = _smallSize / (MEDIUM_SAMPLE_MAXSIZE / SMALL_SAMPLE_MAXSIZE);
    if (_mediumSize < 2) _mediumSize = 2;

    _largeSize = _mediumSize / 2;
    if (_largeSize < 2) _largeSize = 2;

    _smallSamples = new SampleType*[_smallSize];
    _mediumSamples = new SampleType*[_mediumSize];
    _largeSamples = new SampleType*[_largeSize];
#ifdef DEBUG
    DLOG(("nsmall=%d, nmedium=%d, nlarge=%d",_smallSize,_mediumSize, _largeSize));
#endif
}

template<class SampleType>
SamplePool<SampleType>::~SamplePool() {
    int i;
    for (i = 0; i < _nsmall; i++) delete _smallSamples[i];
    delete [] _smallSamples;
    for (i = 0; i < _nmedium; i++) delete _mediumSamples[i];
    delete [] _mediumSamples;
    for (i = 0; i < _nlarge; i++) delete _largeSamples[i];
    delete [] _largeSamples;
    SamplePools::getInstance()->removePool(this);
}

template<class SampleType>
SampleType* SamplePool<SampleType>::getSample(unsigned int len)
{

    nidas::util::Synchronized pooler(_poolLock);

    // Shouldn't get back more than I've dealt out
    // If we do, that's an indication that reference counting
    // is screwed up.

    assert(_nsamplesOut >= 0);

    // conservation of sample numbers:
    // total number allocated must equal:
    //		number in the pool (_nsmall + _nmedium + _nlarge), plus
    //		number held by others.
    assert(_nsamplesAlloc == _nsmall + _nmedium + _nlarge + _nsamplesOut);

    // get a sample from the appropriate pool, unless is is empty
    // and there are 2 or more available from the next larger pool.
    if (len < SMALL_SAMPLE_MAXSIZE && (_nsmall > 0 ||
                (_nmedium + _nlarge) < 4))
        return getSample((SampleType**)_smallSamples,&_nsmall,len);
    else if (len < MEDIUM_SAMPLE_MAXSIZE && (_nmedium > 0 || _nlarge < 2))
        return getSample((SampleType**)_mediumSamples,&_nmedium,len);
    else return getSample((SampleType**)_largeSamples,&_nlarge,len);
}

template<class SampleType>
SampleType* SamplePool<SampleType>::getSample(SampleType** vec,
        int *n, unsigned int len)
{

    SampleType *sample;
#ifdef DEBUG
    std::cerr << "getSample, this=" << std::hex << this <<
        " pool=" << vec << std::dec <<
        " *n=" << *n << std::endl;
#endif
    int i = *n - 1;

    if (i >= 0) {
        sample = vec[i];
        if (sample->getAllocLength() < len) sample->allocateData(len);
#ifndef NDEBUG
        else if (sample->getAllocLength() > len) {
            // If the sample has been previously allocated, and its length
            // is at least one more than we need, set the one-past-the-end
            // data value to a noticable value. Then if a buggy process method
            // reads past the end of a sample, they'll get a value that should
            // raise questions about the results, rather than something
            // that might go unnoticed.

            // valgrind won't complain in these situations unless one reads
            // past the allocated size.

            // For character data (sizeof(T) == 1), we'll use up to
            // 4 '\x80's as the weird value.
            // For larger sizes, we'll use floatNAN. This will convert to
            // 0 for integer samples.
            
            extern const float floatNAN;

            if (sample->sizeofDataType() == 1) {
                static const char weird[4] = { '\x80','\x80','\x80','\x80' };
                int nb = std::min(4U,sample->getAllocLength()-len);
                memcpy((char*)sample->getVoidDataPtr()+len,weird,nb);
            }
            else sample->setDataValue(len,floatNAN);  // NAN converted to the data type.
        }
#endif
        sample->setDataLength(len);
        *n = i;
        sample->holdReference();
        _nsamplesOut++;
        return sample;
    }

    sample = new SampleType();
    sample->allocateData(len);
    sample->setDataLength(len);
    _nsamplesAlloc++;
    _nsamplesOut++;
    return sample;

}

template<class SampleType>
void SamplePool<SampleType>::putSample(const SampleType *sample) {

    nidas::util::Synchronized pooler(_poolLock);

    assert(_nsamplesOut >= 0);
    assert(_nsamplesAlloc == _nsmall + _nmedium + _nlarge + _nsamplesOut);

    unsigned int len = sample->getAllocLength();
    if (len < SMALL_SAMPLE_MAXSIZE) {
#ifdef DEBUG
        DLOG(("put small sample, len=%d,bytelen=%d,n=%d,size=%d",len,sample->getAllocByteLength(),_nsmall,_smallSize));
#endif
        putSample(sample,(SampleType***)&_smallSamples,&_nsmall,&_smallSize);
    }
    else if (len < MEDIUM_SAMPLE_MAXSIZE) {
#ifdef DEBUG
        DLOG(("put medium sample, len=%d,bytelen=%d,n=%d,size=%d",len,sample->getAllocByteLength(),_nmedium,_mediumSize));
#endif
        putSample(sample,(SampleType***)&_mediumSamples,&_nmedium,&_mediumSize);
    }
    else {
#ifdef DEBUG
        DLOG(("put large sample, len=%d,bytelen=%d,n=%d,size=%d",len,sample->getAllocByteLength(),_nlarge,_largeSize));
#endif
        putSample(sample,(SampleType***)&_largeSamples,&_nlarge,&_largeSize);
    }
}

template<class SampleType>
void SamplePool<SampleType>::putSample(const SampleType *sample,
        SampleType ***vec,int *n, int *nalloc)
{

    // increase by 50%
    if (*n == *nalloc) {
        // cerr << "reallocing, n=" << *n << " nalloc=" << *nalloc << endl;
        // increase by 50%
        int newalloc = *nalloc + (*nalloc >> 1);
#ifdef DEBUG
        DLOG(("*nalloc=%d, newalloc=%d",*nalloc,newalloc));
#endif
        SampleType **newvec = new SampleType*[newalloc];
        ::memcpy(newvec,*vec,*nalloc * sizeof(SampleType*));
        delete [] *vec;
        *vec = newvec;
        *nalloc = newalloc;
    }

    (*vec)[(*n)++] = (SampleType*) sample;
    _nsamplesOut--;
}

}}	// namespace nidas namespace core

#endif
