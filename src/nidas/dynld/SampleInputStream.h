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

#ifndef NIDAS_DYNLD_SAMPLEINPUTSTREAM_H
#define NIDAS_DYNLD_SAMPLEINPUTSTREAM_H

#include <nidas/core/SampleInput.h>
#include <nidas/core/SampleInputHeader.h>
#include <nidas/core/SampleSourceSupport.h>
#include <nidas/core/SampleStats.h>
#include <nidas/core/Sample.h>
#include <nidas/core/NidsIterators.h>
#include <nidas/core/NidasApp.h>
#include <nidas/util/UTime.h>
#include <nidas/core/BadSampleFilter.h>

namespace nidas {

namespace core {
class DSMService;
class DSMConfig;
class SampleTag;
class SampleStats;
class SampleClient;
class SampleSource;
class Sample;
class IOChannel;
class IOStream;
}

namespace dynld {

/**
 * Keep track of statistics for a contiguous block of good or bad samples
 * in a stream.
 **/
struct BlockStats {

    typedef nidas::core::dsm_time_t dsm_time_t;

    /**
     * Initialize a block as good or bad and give it an offset.  Blocks
     * default to being good but empty.
     **/
    BlockStats(bool goodblock=true, size_t startblock=0);

    /**
     * On a good sample, reset this block to a good block if not already,
     * and update the last good sample and the size of the block.  Call this
     * method on each good sample so that when a bad header appears, all the
     * stats in this block are already correct.  Return true if this is the
     * start of a new good block.
     **/
    bool
    addGoodSample(nidas::core::Sample* samp, long long offset);

    /**
     * An alternative to calling startBadBlock()/endBadBlock(), it adds
     * nbadbytes to a bad block, resetting the block from good to bad if
     * necessary.  It is analogous to the addGoodSample() method.  Use
     * startBadBlock() and endBadBlock() to avoid calling addGoodSample()
     * on every single bad byte.  When a bad block is started from a good
     * block, it copies the end_time from the good block into the
     * start_time of the new bad block.
     **/
    void
    addBadSample(long long offset, unsigned int nbadbytes);

    /**
     * Start a bad block.  The size (nbytes) will not be correct until
     * endBadBlock() is called.
     **/
    void
    startBadBlock(long long offset);

    /**
     * Mark the end of a bad block by assigning the end_time and setting
     * nbytes according to the current offset.  If the block ends without a
     * good sample, then pass @p samp as NULL, in which case the end_time
     * will remain at the default of LONG_LONG_MAX.
     **/
    void
    endBadBlock(nidas::core::Sample* samp, long long offset);

    inline size_t
    blockEnd() const
    {
        return block_start + nbytes;
    }

    /**
     * Sample times bounding this block.  For good blocks, these are the
     * sample times for the first and last samples in the block.  For bad
     * blocks, they are the sample times of the last good sample before the
     * block and the first good sample after the block.  If unset,
     * start_time is LONG_LONG_MIN and end_time is LONG_LONG_MAX.
     **/
    dsm_time_t start_time;
    dsm_time_t end_time;

    /**
     * True if this is a block of good samples.
     **/
    bool good;

    /**
     * Number of good samples in a row.  Zero in a bad block.
     */
    size_t nsamples;

    /*
     * File position of start of this block.
     */
    size_t block_start;

    /**
     * Size in bytes of this block.  Starts out empty at zero.
     **/
    size_t nbytes;

    /**
     * Size of the last good sample in a good sample block. It is zero in a
     * bad block.
     **/
    unsigned int last_good_sample_size;
};

/**
 * An implementation of a SampleInput.
 *
 * The readSamples method converts raw bytes from the iochannel
 * into Samples.
 *
 * If a SampleClient has requested processed Samples via
 * addProcessedSampleClient, then SampleInputStream will pass
 * Samples to the respective DSMSensor for processing,
 * and then the DSMSensor passes the processed Samples to
 * the SampleClient:
 *
 * iochannel -> readSamples method -> DSMSensor -> SampleClient
 * 
 * If a SampleClient has requested non-processed Samples,
 * via the simple addSampleClient method, then SampleInput
 * stream passes the Samples straight to the SampleClient:
 *
 * iochannel -> readSamples method -> SampleClient
 *
 */
class SampleInputStream: public nidas::core::SampleInput
{
public:
    typedef nidas::core::dsm_time_t dsm_time_t;

    /**
     * Constructor.
     * @param raw Whether the input samples are raw.
     */
    SampleInputStream(bool raw=false);

    /**
     * Constructor.
     * @param iochannel The IOChannel that we use for data input.
     *   SampleInputStream will own the pointer to the IOChannel,
     *   and will delete it in ~SampleInputStream(). If 
     *   it is a null pointer, then it must be set within
     *   the fromDOMElement method.
     */
    SampleInputStream(nidas::core::IOChannel* iochannel,bool raw=false);

    /**
     * Create a clone, with a new, connected IOChannel.
     */
    virtual SampleInputStream* clone(nidas::core::IOChannel* iochannel);

    virtual ~SampleInputStream();

    std::string getName() const;

    int getFd() const;

    /**
     * Set the IOChannel for this SampleInputStream.h
     */
    virtual void setIOChannel(nidas::core::IOChannel* val);

    /**
     * Read archive information at beginning of input stream or file.
     *
     * @throws nidas::util::IOException
     **/
    void readInputHeader();

    /**
     * @throws nidas::util::IOException
     **/
    bool parseInputHeader();

    const nidas::core::SampleInputHeader& getInputHeader() const
    {
        return _inputHeader;
    }

    /**
     * @throws nidas::util::IOException
     **/
    void requestConnection(nidas::core::DSMService*);

    virtual nidas::core::SampleInput* getOriginal() const
    {
        return _original;
    }

    /**
     * Implementation of IOChannelRequester::connected.
     * One can use this method to notify SampleInputStream that
     * the IOChannel is connected, which will cause SampleInputStream
     * to open the IOStream.
     */
    nidas::core::SampleInput* connected(nidas::core::IOChannel* iochan) throw();

    /**
     * @throws nidas::util::IOException
     **/
    void setNonBlocking(bool val);

    /**
     * @throws nidas::util::IOException
     **/
    bool isNonBlocking() const;

    /**
     * What DSM am I connnected to? May be NULL if it cannot be determined.
     */
    const nidas::core::DSMConfig* getDSMConfig() const;

    // void init() throw();

    void setKeepStats(bool val)
    {
        _source.setKeepStats(val);
    }

    /**
     * Implementation of SampleInput::addSampleTag().
     */
    void addSampleTag(const nidas::core::SampleTag* tag) throw()
    {
        return _source.addSampleTag(tag);
    }

    void removeSampleTag(const nidas::core::SampleTag* tag) throw()
    {
        _source.removeSampleTag(tag);
    }

    nidas::core::SampleSource* getRawSampleSource()
    {
        return _source.getRawSampleSource();
    }

    nidas::core::SampleSource* getProcessedSampleSource()
    {
        return _source.getProcessedSampleSource();
    }

    /**
     * Get the output SampleTags.
     */
    std::list<const nidas::core::SampleTag*> getSampleTags() const
    {
        return _source.getSampleTags();
    }

    /**
     * Implementation of SampleSource::getSampleTagIterator().
     */
    nidas::core::SampleTagIterator getSampleTagIterator() const
    {
        return _source.getSampleTagIterator();
    }

    /**
     * Implementation of SampleSource::addSampleClient().
     */
    void addSampleClient(nidas::core::SampleClient* client) throw()
    {
        _source.addSampleClient(client);
    }

    void removeSampleClient(nidas::core::SampleClient* client) throw()
    {
        _source.removeSampleClient(client);
    }

    /**
     * Add a Client for a given SampleTag.
     * Implementation of SampleSource::addSampleClient().
     */
    void addSampleClientForTag(nidas::core::SampleClient* client,const nidas::core::SampleTag* tag) throw()
    {
        _source.addSampleClientForTag(client,tag);
    }

    void removeSampleClientForTag(nidas::core::SampleClient* client,const nidas::core::SampleTag* tag) throw()
    {
        _source.removeSampleClientForTag(client,tag);
    }

    int getClientCount() const throw()
    {
        return _source.getClientCount();
    }

    /**
     * Implementation of SampleSource::flush(), unpacks and distributes
     * any samples currently in the read buffer.
     */
    void flush() throw();

    const nidas::core::SampleStats& getSampleStats() const
    {
        return _source.getSampleStats();
    }

    /**
     * Read a buffer of data, serialize the data into samples,
     * and distribute() samples to the receive() method of my
     * SampleClients and DSMSensors.
     * This will perform only one physical read of the underlying
     * IOChannel and so is appropriate to use when a select() or poll()
     * has determined that there is data available on the file
     * descriptor, or when the physical device is configured
     * for non-blocking reads.
     * @return false: no data available for physical read, likely the result of
     *  an EAGAIN from a non-blocking read on a file descriptor.
     *  true: physical read did not necessarily consume all available data.
     *
     * @throws nidas::util::IOException
     **/
    bool readSamples();

    /**
     * Search forward until a sample header is read whose time is 
     * greater than or equal to tt.  Leaves the InputStream
     * positioned so that the next call to readSample() or
     * readSamples() will read the rest of the sample.
     *
     * @throws nidas::util::IOException
     **/
    void search(const nidas::util::UTime& tt);

    /**
     * Read the next sample from the InputStream. The caller must
     * call freeReference on the sample when they're done with it.
     * This method may perform zero or more reads of the IOChannel.
     * @return pointer to a sample, never NULL.
     *
     * @throws nidas::util::IOException
     **/
    nidas::core::Sample* readSample();


    /**
     * Distribute a sample to my clients. One could use this
     * to insert a sample into the stream.
     */
    void distribute(const nidas::core::Sample* s) throw()
    {
        return _source.distribute(s);
    }

    size_t getBadSamples() const { return _badSamples; }

    /**
     * @throws nidas::util::IOException
     **/
    void close();

    /**
     * Replace the bad sample filter rules for this stream with @p bsf.
     **/
    void setBadSampleFilter(const nidas::core::BadSampleFilter& bsf)
    {
        _bsf = bsf;
    }

    /**
     * See BadSampleFilter.
     **/
    void setFilterBadSamples(bool val)
    {
        _bsf.setFilterBadSamples(val);
    }

    /**
     * See BadSampleFilter.
     **/
    void setMinDsmId(int val)
    {
        _bsf.setMinDsmId(val);
    }

    /**
     * See BadSampleFilter.
     **/
    void setMaxDsmId(int val)
    {
        _bsf.setMaxDsmId(val);
    }

    /**
     * See BadSampleFilter.
     **/
    void setMinSampleLength(unsigned int val)
    {
        _bsf.setMinSampleLength(val);
    }

    /**
     * See BadSampleFilter.
     **/
    void setMaxSampleLength(unsigned int val)
    {
        _bsf.setMaxSampleLength(val);
    }

    /**
     * See BadSampleFilter.
     **/
    void setMinSampleTime(const nidas::util::UTime& val)
    {
        _bsf.setMinSampleTime(val);
    }

    /**
     * See BadSampleFilter.
     **/
    void setMaxSampleTime(const nidas::util::UTime& val)
    {
        _bsf.setMaxSampleTime(val);
    }

    /**
     * @throws nidas::util::InvalidParameterException
     **/
    void fromDOMElement(const xercesc::DOMElement* node);

    void setExpectHeader(bool val) { _expectHeader = val; }

    bool getExpectHeader() const { return _expectHeader; }

protected:

    /**
     * Copy constructor, with a new, connected IOChannel.
     */
    SampleInputStream(SampleInputStream& x,nidas::core::IOChannel* iochannel);

    nidas::core::IOChannel* _iochan;

    nidas::core::SampleSourceSupport _source;

private:

    /**
     * Unpack the next sample from the InputStream. This method does not perform
     * any physical reads, so it should not throw EOFException or IOException.
     * @return pointer to a sample if there is one available in the buffer, else
     * NULL.
     */
    nidas::core::Sample* nextSample();

    /**
     * Unpack the next sample from the InputStream buffer or by reading
     * more data if @p keepreading is true.  If @p searching is set, then
     * reading stops after the first sample header found whose time tag is
     * after @p search_time, as described in the search() method.
     * 
     * @throws nidas::util::IOException
     **/
    nidas::core::Sample* nextSample(bool keepreading,
                                    bool searching=false,
                                    dsm_time_t search_time=LONG_LONG_MIN);

    void checkUnexpectedEOF();

    /**
     * Tuple for all the possible results of iostream reads.  Keep it
     * private to SampleInputStream class to avoid polluting the nidas::core
     * namespace.
     **/
    struct ReadResult
    {
    public:
        ReadResult(size_t ilen=0, bool inewinput=false, bool ieof=false):
            len(ilen), newinput(inewinput), eof(ieof)
        {}
        size_t len;
        bool newinput;
        bool eof;
    };

    ReadResult
    readBlock(bool keepreading, char* &ptr, size_t& lentoread);

    ReadResult
    read(bool keepreading, char* ptr, size_t lentoread);

    void
    handleNewInput();

    nidas::core::Sample*
    handleEOF(bool keepreading);

    void closeBlocks();

    /**
     * Check the current header for validity and generate a sample for it.
     **/
    nidas::core::Sample* sampleFromHeader() throw();

    /**
     * Service that has requested my input.
     */
    nidas::core::DSMService* _service;

    nidas::core::IOStream* _iostream;

    /* mutable */ const nidas::core::DSMConfig* _dsm;

    bool _expectHeader;

    bool _inputHeaderParsed;

    nidas::core::SampleHeader _sheader;

    size_t _headerToRead;

    char* _hptr;

    /**
     * Will be non-null if we have previously read part of a sample
     * from the stream.
     */
    nidas::core::Sample* _samp;

    /**
     * The currently pending sample.  When filtering is active, the pending
     * sample is held until the succeeding sample header is confirmed to be
     * good.
     **/
    nidas::core::Sample* _sampPending;

    /**
     * How many bytes left to read from the stream into the data
     * portion of samp.
     */
    size_t _dataToRead;

    /**
     * Pointer into the data portion of samp where we will read next.
     */
    char* _dptr;

    /**
     * This is set if the data for the current sample header should
     * be read but skipped, because the sample is the first after
     * a block of bad samples and thus the time at the front of the
     * sample might still be corrupt.
     **/
    bool _skipSample;

    /**
     * Information about the current block of samples, good or bad.
     **/
    BlockStats _block;

    /**
     * Number of bad samples in the stream so far, which is to say number
     * of bytes checked which did not contain a reasonable sample header.
     **/
    size_t _badSamples;

    /**
     * Number of good samples in the stream so far.
     **/
    size_t _goodSamples;

    nidas::core::SampleInputHeader _inputHeader;

    nidas::core::BadSampleFilter _bsf;

    SampleInputStream* _original;

    // XXX This is no longer used and is deprecated.  Someday it will be
    // removed from here and from the constructor signatures. XXX
    bool _raw;

    /**
     * Keep the current name of the input stream so it can be  
     * referenced even after the input stream has advanced to
     * a new input name.
     **/
    std::string _last_name;

    nidas::util::EOFException _eofx;
    bool _ateof;

    /**
     * No regular copy.
     */
    SampleInputStream(const SampleInputStream& x);

    /**
     * No assignment.
     */
    SampleInputStream& operator =(const SampleInputStream& x);

};

}}	// namespace nidas namespace dynld

#endif
