// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2005, Copyright University Corporation for Atmospheric Research
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
#ifndef NIDAS_CORE_SORTEDSAMPLESET_H
#define NIDAS_CORE_SORTEDSAMPLESET_H

#include "Sample.h"

#include <set>

namespace nidas { namespace core {

/**
 * Sample time tag comparator.
 */
class SampleTimetagComparator {
public:
    /**
     * Return true if x is less than y.
     */
    bool operator() (const Sample* x, const Sample *y) const {
	return x->getTimeTag() < y->getTimeTag();
    }
};

/**
 * Timetag and Id comparator of pointers to Samples: if two timetags
 * are the same, then compare Ids, and if they're equal, compare data
 * length.  This is useful when merging archives, where one expects
 * duplicate samples.  Otherwise, if one doesn't expect duplicates,
 * which is the usual case, this is less efficient than the simple
 * SampleTimetagComparator above.  Warning: if a sensor can generate
 * multiple samples with the same time tag AND the same data length
 * but differing data, then a set using this comparator will discard
 * all but one of the samples whose timetag and length match.
 */
class SampleHeaderComparator {
public:
    /**
     * Return true if x is less than y.
     */
    bool operator() (const Sample* x, const Sample *y) const {
        if (x->getTimeTag() > y->getTimeTag()) return false;
        if (x->getTimeTag() == y->getTimeTag()) {
            if (x->getId() > y->getId()) return false;
            if (x->getId() == y->getId())
                return x->getDataLength() < y->getDataLength();
        }
        return true;
    }
};

/**
 * Comparator of pointers to Samples, does the same checks
 * as SampleHeaderComparator, but in addition, if two samples compare
 * as equal, then compares their data.
 */
class FullSampleComparator {
public:
    /**
     * Return true if x is less than y.
     */
    bool operator() (const Sample* x, const Sample *y) const {
        if (x->getTimeTag() > y->getTimeTag()) return false;
        if (x->getTimeTag() == y->getTimeTag()) {
            if (x->getId() > y->getId()) return false;
            if (x->getId() == y->getId()) {
                if (x->getDataLength() > y->getDataLength()) return false;
                if (x->getDataLength() == y->getDataLength()) {
		    // compare the data
		    return ::memcmp(x->getConstVoidDataPtr(),
		    	y->getConstVoidDataPtr(),x->getDataLength()) < 0;
		}
	    }
        }
        return true;
    }
};

/**
 * A multiset for storing samples sorted by timetag.  It is a multiset
 * rather than a set because it is OK to have samples with the same
 * timetag.
 */
typedef std::multiset<const Sample*,SampleTimetagComparator> SortedSampleSet; 

/**
 * A set for storing samples sorted by the timetag, id and data length.
 * Duplicate samples which compare as equal ( !(x<y) & !(y<x) ) to another
 * are not inserted.  This is used for merging archives containing
 * duplicate samples.  Note that we're not comparing the data in the sample.
 * Also note: you must check the return of insert(const Sample*) to
 * know whether a sample was inserted. If a sample wasn't inserted
 * you likely want to do a Sample::freeReference() on it.
 */
typedef std::set<const Sample*,SampleHeaderComparator> SortedSampleSet2; 

typedef std::set<const Sample*,FullSampleComparator> SortedSampleSet3; 

}}	// namespace nidas namespace core

#endif
