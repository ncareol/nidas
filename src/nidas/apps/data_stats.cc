/* -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; -*- */
/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
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

// #define _XOPEN_SOURCE	/* glibc2 needs this */

#include <ctime>

#include <nidas/core/FileSet.h>
#include <nidas/core/Socket.h>
#include <nidas/dynld/RawSampleInputStream.h>
#include <nidas/core/Project.h>
#include <nidas/core/XMLParser.h>
#include <nidas/core/SamplePipeline.h>
#include <nidas/core/DSMConfig.h>
#include <nidas/core/DSMSensor.h>
#include <nidas/core/Variable.h>
#include <nidas/core/NidasApp.h>
#include <nidas/core/BadSampleFilter.h>
#include <nidas/util/EOFException.h>
#include <nidas/util/Process.h>
#include <nidas/util/Logger.h>
#include <nidas/util/auto_ptr.h>
#include <nidas/util/util.h>

#include <set>
#include <map>
#include <deque>
#include <iostream>
#include <iomanip>
#include <sys/stat.h>

#include <unistd.h>
#include <stdio.h>   // rename()

#ifndef NIDAS_JSONCPP_ENABLED
#define NIDAS_JSONCPP_ENABLED 1
#endif
#if NIDAS_JSONCPP_ENABLED
#include <json/json.h>
// Early json versions without StreamWriterBuilder also did not define a
// version symbol.
#ifndef JSONCPP_VERSION_STRING
#define NIDAS_JSONCPP_STREAMWRITER 0
#else
#define NIDAS_JSONCPP_STREAMWRITER 1
#endif
#endif

using namespace nidas::core;
using namespace nidas::dynld;
using namespace std;

namespace n_u = nidas::util;
using nidas::util::UTime;
using nidas::util::Logger;
using nidas::util::LogScheme;

namespace {
    inline std::string
    iso_format(const UTime& ut)
    {
        return ut.format(true, "%Y-%m-%dT%H:%M:%S.%3fZ");
    }

#if NIDAS_JSONCPP_ENABLED
    inline Json::Value
    number_or_null(float value)
    {
        if (std::isnan(value))
        {
            return Json::Value();
        }
        else
        {
            return Json::Value(value);
        }
    }
#endif
}


/**
 * Problem is any issue detected in the data and statistics, distinguished
 * by a kind id and a stream id, and an optionally including any related
 * values.
 *
 * { "problem-class": "sample-rate-mismatch",
 *   "streamid": stream-id }
 */
class Problem
{
public:

    static const std::string SAMPLE_RATE_MISMATCH;
    static const std::string MISSING_VALUES;
    static const std::string NO_SAMPLES;

    Problem(const std::string& kind, const std::string& streamid):
        kind(kind),
        streamid(streamid)
    {}

    Json::Value json_value()
    {
        values["streamid"] = streamid;
        values["kind"] = kind;
        return values;
    }

    /**
     * Return a one-line string enumerating this problem.
     */
    std::string printout()
    {
        std::ostringstream out;
        out << kind << " in " << streamid;
        auto members = values.getMemberNames();
        bool first = true;
        for (auto& member: members)
        {
            out << (first ? ": " : "; ");
            out << member << "=";
            Json::Value& value = values[member];
            if (value.isNumeric())
                out << setprecision(5) << value.asDouble();
            else
                out << value;
            first = false;
        }
        return out.str();
    }

    static Json::Value
    asJsonArray(std::vector<Problem>& problems)
    {
        Json::Value array(Json::arrayValue);
        for (auto& p: problems)
            array.append(p.json_value());
        return array;
    }

    std::string kind;
    std::string streamid;
    Json::Value values{Json::objectValue};
};


const std::string Problem::SAMPLE_RATE_MISMATCH{ "sample-rate-mismatch" };
const std::string Problem::MISSING_VALUES{ "missing-values" };
const std::string Problem::NO_SAMPLES{ "no-samples" };


/**
 * SampleCounter accumulates samples and values for a particular sample stream.
 **/
class SampleCounter
{
public:
    /**
     * Create a SampleCounter for the given sample id @p sid.  If available,
     * pass the Sensor in @p sensor, and it will be used to collect metadata
     * about this sample stream and to set a header name.  If the SampleTag
     * for this sample is passed in @p stag, then it is used to store the
     * variable names for the sample values.
     **/
    SampleCounter(dsm_sample_id_t sid = 0, const DSMSensor* sensor = 0,
                  const SampleTag* stag = 0) :
        streamid(),
        sname(),
        id(sid),
        t1s(0),
        t2s(0),
        nsamps(0),
        minlens(0),
        maxlens(0),
        totalBytes(0),
        minDeltaTs(0),
        maxDeltaTs(0),
        varnames(),
        fullnames(),
        enable_json(false),
        enable_data(false),
        sums(),
        nnans(),
        rawmsg(),
        values(),
        times()
#if NIDAS_JSONCPP_ENABLED
        ,header()
#endif
    {
        streamid = generateStreamId(sensor);
        if (sensor)
        {
            sname = sensor->getDSMConfig()->getName() + ":" +
                sensor->getDeviceName();
        }
        if (stag)
        {
            const std::vector<const Variable*>& variables = stag->getVariables();
            for (unsigned int i = 0; i < variables.size(); ++i)
            {
                // We need to keep track of two variable names, the fully
                // qualified 'name' and the short 'prefix' name.  The prefix
                // is unique within this stream, so it is used as the key,
                // whereas reports generated by callers want the full name.
                fullnames.push_back(variables[i]->getName());
                varnames.push_back(variables[i]->getPrefix());
            }
        }
        collectMetadata(sensor, stag);
    }

    /**
     * Set whether information for JSON output will be accumulated, according to whether @p enable
     * is true or false.  Enabling JSON output automatically enables the collection of data
     * statistics.
     **/
    void
    enableJSON(bool enable)
    {
        enable_json = enable;
        if (enable_json)
            enableData(true);
    }

    /**
     * Set whether data values will be extracted from processed samples and accumulated to
     * compute data statistics.  Disabling data statistics automatically disables JSON
     * information.
     **/
    void
    enableData(bool enable)
    {
        enable_data = enable;
        if (!enable_data)
            enableJSON(false);
    }

    /**
     * Format the sensor name or variable names for this SampleCounter into
     * a comma-separated list, abbreviated to a shortened form unless @p
     * allnames is true.  If this is a raw counter, meaning no variables,
     * then use the sensor name.  The fully-qualified variable names are
     * used in the header line to be compatible with historical behavior,
     * but maybe someday this can change.
     **/
    std::string
    getHeaderLine(bool allnames)
    {
        if (varnames.empty())
            return sname;
        string varname = fullnames[0];
        for (unsigned int i = 1; allnames && i < fullnames.size(); ++i)
        {
            varname += "," + fullnames[i];
        }
        if (!allnames && fullnames.size() > 1)
        {
            varname += ",...";
        }
        // Include device name when allnames enabled.
        if (allnames)
        {
            varname = "[" + sname + "] " + varname;
        }
        return varname;
    }

    /**
     * Reset the accumulated data without changing the sample tag information.
     **/
    void
    reset()
    {
        t1s = 0;
        t2s = 0;
        nsamps = 0;
        minlens = 0;
        maxlens = 0;
        totalBytes = 0;
        minDeltaTs = 0;
        maxDeltaTs = 0;
        sums.clear();
        nnans.clear();
        values.clear();
        rawmsg.clear();
        times.clear();
    }

    bool
    receive(const Sample* samp);

    void
    accumulateData(const Sample* samp);

    // Compute the rate for the current time range of the samples and the
    // number of samples.  Return nan if fewer than 2 samples.  Note that
    // returning a rate of 0 for no samples is problematic, because 0 samples
    // over a shorter time period is different than 0 samples over a longer
    // one.  A sensor with rate 0.5 Hz is close to 0, but that does not mean
    // there isn't a rate mismatch.  Perhaps comparing expected samples to
    // received would be more correct... At the moment it's up to the user to
    // collect samples over a long enough period for the rate to be
    // meaningful, according to the expected rates of the sensors.
    double
    computeRate()
    {
        double rate = floatNAN;
        if (nsamps > 1)
            rate = double(nsamps-1) / (double(t2s - t1s) / USECS_PER_SEC);
        return rate;
    }

    /**
     * Format the data from this SampleCounter and write it to the given
     * output stream @p outs.
     **/
    void
    printData(std::ostream& outs);

    /**
     * Generate a streamid for this SampleCounter.  The streamid is the
     * unique identifer for this stream of samples based on the available
     * metadata.
     **/
    std::string
    generateStreamId(const DSMSensor* sensor=0);

    void
    collectMetadata(const DSMSensor* sensor=0, const SampleTag* stag=0);

    void
    accumulateSample(const Sample* samp);

    string streamid;
    string sname;
    dsm_sample_id_t id;

    dsm_time_t t1s;
    dsm_time_t t2s;
    size_t nsamps;
    size_t minlens;
    size_t maxlens;
    size_t totalBytes;
    int minDeltaTs;
    int maxDeltaTs;

    // Stash the variable names from the sample tag to identify the
    // variables in the accumulated data.  This can be used to map from
    // variable index in the sample to the key for the data maps.
    vector<string> varnames;
    vector<string> fullnames;

    // As samples are accumulated, take care to only consume memory for what
    // is needed.  These settings select what will be accumulated.

    // Enable JSON objects.  Everything is included in the JSON output,
    // including individual data samples and the data statistics.
    bool enable_json;

    // Enable data statistics.
    bool enable_data;

    // ==> Following members are only filled when a data report is needed.

    // Accumulate sums of each variable in the sample and counts of the
    // number of nans seen in each variable.  These are vectors keyed by
    // variable index.
    vector<float> sums;
    vector<int> nnans;

    // For raw samples, build up a vector of the raw messages converted to
    // strings.  Actually, since right now only the last is ever printed,
    // this only ever contains the last raw message.
    vector<std::string> rawmsg;

    // ==> Following members only needed for JSON output.

    // All the processed values.  The outer vector is indexed by variable
    // index, and each element is a vector for all the values of that
    // variable at each time, including nans.
    vector<vector<float> > values;

    // Collect the timestamps of all the samples also.
    vector<dsm_time_t> times;

#if NIDAS_JSONCPP_ENABLED
    /**
     * Return a Json::Value node containing all the data in this SampleCounter.
     **/
    Json::Value
    jsonData();

    /**
     * Return a json object with the statistics calculated for this stream,
     * and append any Problem instances to the vector @p problems.
     **/
    Json::Value
    jsonStats(std::vector<Problem>& problems);

    /**
     * Return a Json::Value containing just the header for this SampleClient.
     **/
    Json::Value
    jsonHeader()
    {
        return header;
    }

    // Store stream header metadata.
    Json::Value header;
#endif
};

namespace {
    // Copied from data_influxdb.cc, should probably be moved where it can
    // be shared.
    std::string
    id_to_string(unsigned int id)
    {
        ostringstream tostring;
        if (id >= 0x8000)
        {
            tostring << "0x" << hex << id;
        }
        else
        {
            tostring << id;
        }
        return tostring.str();
    }
}

std::string
SampleCounter::
generateStreamId(const DSMSensor* sensor)
{
    // From the full sample id we can at least get a DSM id and a SPS id.
    dsm_sample_id_t dsmid = GET_DSM_ID(id);
    dsm_sample_id_t spsid = GET_SPS_ID(id);

    // Use <project>.<dsmid>.<spsid>, for now.
    string projectname = "noproject";
    if (sensor)
    {
        const Project* project = sensor->getDSMConfig()->getProject();
        if (project)
            projectname = project->getName();
        // I would like to force this to lower case, but leave it for now.
        // I suppose technically ISFS project names are case-sensitive.
    }
    std::ostringstream out;
    // Hex for mote sample ids is just more useful for human inspection.
    out << projectname << "." << dsmid << "." << id_to_string(spsid);
    return out.str();
}

#if NIDAS_JSONCPP_ENABLED
/**
 * If the metadata string value is not empty, assign that
 * value to the json field named key.
 **/
std::string
assign_if_set(Json::Value& object, const std::string& key,
              const std::string& value)
{
    if (! value.empty())
        object[key] = value;
    return value;
}
#endif

void
SampleCounter::
collectMetadata(const DSMSensor* sensor, const SampleTag* stag)
{
#if NIDAS_JSONCPP_ENABLED
    header["streamid"] = streamid;
    // The header specifies a version in case the schema changes for either
    // the header itself or the data streams which reference it.
    header["version"] = "v0";
    header["dsmid"] = GET_DSM_ID(id);
    header["spsid"] = GET_SPS_ID(id);

    if (sensor)
    {
        const DSMConfig* dsm = sensor->getDSMConfig();
        const Project* project = dsm->getProject();
        string dsmname = dsm->getName();
        header["dsmname"] = dsmname;
        if (project)
            header["project"] = project->getName();
        const Site* site = sensor->getSite();
        if (site)
            header["site"] = site->getName();
        header["device"] = sensor->getDeviceName();
        // Some fields are not meaningful unless set.
        assign_if_set(header, "classname", sensor->getClassName());
        assign_if_set(header, "sensorcatalogname", sensor->getCatalogName());
        assign_if_set(header, "depth", sensor->getDepthString());
        assign_if_set(header, "height", sensor->getHeightString());
        assign_if_set(header, "location", sensor->getLocation());
    }
    if (stag)
    {
        double rate = stag->getRate();
        if (rate != 0)
            header["rate"] = rate;

        const std::vector<const Variable*>& variables = stag->getVariables();
        Json::Value& vmap = header["variables"];
        for (unsigned int i = 0; i < variables.size(); ++i)
        {
            const Variable& v = *variables[i];
            // The "short name" for a variable is known as the prefix, since
            // it is the very first part of the fully qualified long name.
            string vname = v.getPrefix();
            vmap[vname]["name"] = vname;
            // fullname is the fully qualified name, also known as the name.
            vmap[vname]["fullname"] = v.getName();
            vmap[vname]["longname"] = v.getLongName();
            vmap[vname]["units"] = v.getUnits();
        }
    }
#endif
}


bool
SampleCounter::
receive(const Sample* samp)
{
    NidasApp& app{ *NidasApp::getApplicationInstance() };
    dsm_sample_id_t sampid = samp->getId();
    VLOG(("counting sample ") << nsamps << " for id "
         << app.formatId(sampid));
    if (sampid != id && nsamps == 0)
    {
        ILOG(("assigning received sample ID ")
             << app.formatId(sampid) << " in place of " << app.formatId(id));
        id = sampid;
    }
    else if (sampid != id)
    {
        // Worst case this would cause a message for every sample, but it
        // is rare enough to not be worth improving.
        ELOG(("sample ID ") << app.formatId(sampid)
             << "is being included in statistics for "
             << "samples with different ID: " << app.formatId(id));
    }
    accumulateSample(samp);
    return true;
}


void
SampleCounter::
accumulateSample(const Sample* samp)
{
    dsm_time_t sampt = samp->getTimeTag();
    if (nsamps == 0)
    {
        t1s = sampt;
        minDeltaTs = INT_MAX;
        maxDeltaTs = INT_MIN;
    }
    else
    {
        int deltaT = (sampt - t2s + USECS_PER_MSEC/2) / USECS_PER_MSEC;
        minDeltaTs = std::min(minDeltaTs, deltaT);
        maxDeltaTs = std::max(maxDeltaTs, deltaT);
    }
    t2s = sampt;

    size_t slen = samp->getDataByteLength();
    totalBytes += slen;
    if (nsamps == 0)
    {
        minlens = slen;
        maxlens = slen;
    }
    else
    {
        minlens = std::min(minlens, slen);
        maxlens = std::max(maxlens, slen);
    }
    ++nsamps;

    accumulateData(samp);
}


void
SampleCounter::
accumulateData(const Sample* samp)
{
    dsm_time_t sampt = samp->getTimeTag();

    // Only need sample times to output data.
    if (enable_data)
    {
        times.push_back(sampt);
    }

    if (samp->getType() == CHAR_ST && enable_data)
    {
        const char* cp = (const char*)samp->getConstVoidDataPtr();
        size_t l = samp->getDataByteLength();
        if (l > 0 && cp[l-1] == '\0')
            l--;  // exclude trailing '\0'
        // Only keep the most recent raw message, since so far that is all
        // that is used in the reports.
        rawmsg.resize(1);
        rawmsg[0] = n_u::addBackslashSequences(string(cp,l));
        return;
    }
    if (samp->getType() != FLOAT_ST && samp->getType() != DOUBLE_ST)
    {
        return;
    }
    unsigned int nvalues = samp->getDataLength();
    if (nvalues > sums.size())
    {
        sums.resize(nvalues);
        nnans.resize(nvalues);
        values.resize(nvalues);
    }
    for (unsigned int i = 0; i < nvalues; ++i)
    {
        double value = samp->getDataValue(i);
        // Only need data values for JSON output with data.
        if (enable_data)
        {
            values[i].push_back(value);
        }
        if (std::isnan(value))
        {
            nnans[i] += 1;
        }
        else
        {
            sums[i] += value;
        }
    }
}


namespace
{
    /**
     * Compute the number of digits of space required to display
     * @p value in decimal.
     **/
    inline int
    ndigits(double value)
    {
        return (int)ceil(log10(value));
    }

    struct check_valid
    {
        double _value;
        bool _valid;

        check_valid(double value, bool valid) :
            _value(value),
            _valid(valid)
        {
        }

        inline std::ostream&
        to_stream(std::ostream& outs) const
        {
            if (_valid)
            {
                outs << _value;
            }
            else
            {
                outs << floatNAN;
            }
            return outs;
        }
    };

    inline std::ostream&
    operator<<(std::ostream& outs, const check_valid& cv)
    {
        return cv.to_stream(outs);
    }
}


void
SampleCounter::
printData(std::ostream& outs)
{
    // No samples, no data to print.
    if (nsamps == 0)
    {
        return;
    }
    // Print the last raw data message, if there were any.
    if (rawmsg.size() > 0)
    {
        outs << " " << *rawmsg.rbegin() << endl;
    }
    size_t nwidth = 8;
    outs.unsetf(std::ios::fixed);
    outs << setprecision(3) << fixed;

    // Deliberately choose the short variable name when printing data, to
    // make the output more compact, and since the sensor header should
    // provide the necessary context to differentiate variable names.
    size_t maxname = 0;
    for (unsigned int i = 0; i < varnames.size(); ++i)
    {
        maxname = std::max(maxname, varnames[i].length());
    }
    int nfields = std::max((size_t)2, 80 / (maxname+2+nwidth));

    for (unsigned int i = 0; i < sums.size(); ++i)
    {
        if (i > 0 && i % nfields == 0)
        {
            outs << endl;
        }
        string varname;
        if (i < varnames.size())
        {
            varname = varnames[i];
        }
        outs << " " << setw(maxname) << right << varname << "=" << left;
        outs << setw(nwidth);
        int nvalues = nsamps - nnans[i];
        if (nvalues == 0)
        {
            outs << string(nwidth, '*');
        }
        else
        {
            outs << sums[i]/nvalues;
        }
        if (nvalues && nnans[i] > 0)
        {
            outs << "(*" << nnans[i] << " NaN*)";
        }
    }
    outs << endl;
}

#if NIDAS_JSONCPP_ENABLED
Json::Value
SampleCounter::
jsonData()
{
    Json::Value data;

    data["streamid"] = streamid;

    // If individual values were not enabled, then we're done.
    if (!enable_data)
        return data;

    // Include every variable in the object, even if there are no samples.
    for (unsigned int i = 0; i < varnames.size(); ++i)
    {
        Json::Value jvalues(Json::arrayValue);
        for (unsigned int j = 0; nsamps > 0 && j < values[i].size(); ++j)
        {
            // jsoncpp on jessie does not convert nan to null, and nan is
            // not valid json.
            jvalues.append(number_or_null(values[i][j]));
        }
        data[varnames[i]] = jvalues;
    }
    Json::Value jtimes(Json::arrayValue);
    for (unsigned int i = 0; i < times.size(); ++i)
    {
        UTime ut(times[i]);
        jtimes.append(iso_format(ut));
    }
    data["time"] = jtimes;
    return data;
}

Json::Value
SampleCounter::
jsonStats(std::vector<Problem>& problems)
{
    Json::Value stats;
    stats["streamid"] = streamid;

    // Some stats relate to the whole sample stream.
    stats["nsamps"] = Json::Value::UInt(nsamps);
    if (nsamps > 0)
    {
        Json::Value timerange(Json::arrayValue);
        timerange.append(iso_format(UTime(t1s)));
        timerange.append(iso_format(UTime(t2s)));
        stats["timerange"] = timerange;
    }
    if (nsamps >= 2)
    {
        stats["mindeltat"] = (double)minDeltaTs / MSECS_PER_SEC;
        stats["maxdeltat"] = (double)maxDeltaTs / MSECS_PER_SEC;
        stats["minsamplebytes"] = Json::Value::UInt(minlens);
        stats["maxsamplebytes"] = Json::Value::UInt(maxlens);
        stats["totalbytes"] = Json::Value::UInt(totalBytes);
    }

    if (nsamps == 0 )
    {
        Problem nosamples{Problem::NO_SAMPLES, streamid};
        problems.push_back(std::move(nosamples));
    }

    // Store computed rate no matter what, so it will be null if not enough
    // samples.
    double rate = computeRate();
    stats["averagerate"] = number_or_null(rate);

    // A sensor with no samples is not reporting at the expected rate, so
    // report that as a problem also.  This makes it easier to track sonics
    // which flip back and forth between no data and mismatched rates as a
    // contiguous sample-rate-mismatch problem.
    if (header.isMember("rate"))
    {
        double xrate = header["rate"].asDouble();
        // probably this threshold should be a parameter eventually
        if (xrate != 0 && (std::isnan(rate) || abs(xrate - rate) > 0.5))
        {
            Problem problem{Problem::SAMPLE_RATE_MISMATCH, streamid};
            problem.values["averagerate"] = number_or_null(rate);
            problem.values["expectedrate"] = Json::Value(xrate);
            problems.push_back(std::move(problem));
        }
    }

    // Then include stats for each variable in the stream.
    Json::Value variables;
    for (unsigned int i = 0; i < varnames.size(); ++i)
    {
        Json::Value variable;

        variable["name"] = varnames[i];
        unsigned int nvalues = 0;
        variable["nnans"] = Json::Value::UInt(0);
        if (nsamps > 0)
        {
            variable["nnans"] = nnans[i];
            nvalues = nsamps - nnans[i];
        }
        variable["nvalues"] = nvalues;
        Json::Value average;
        if (nvalues > 0)
        {
            average = sums[i]/nvalues;
        }
        variable["average"] = average;
        variables[varnames[i]] = variable;

        if (nsamps > 0 && nnans[i] > 0)
        {
            Problem nans{Problem::MISSING_VALUES, streamid};
            nans.values["variable"] = varnames[i];
            nans.values["nnans"] = nnans[i];
            problems.push_back(std::move(nans));
        }
    }
    if (varnames.size() > 0)
    {
        stats["variables"] = variables;
    }
    return stats;
}
#endif

class DataStats : public SampleClient
{
public:
    DataStats();

    ~DataStats()
    {
        clearSampleQueue();
    }

    int run();

    void readHeader(SampleInputStream& sis);

    void readSamples(SampleInputStream& sis);

    int parseRunstring(int argc, char** argv);

    static int main(int argc, char** argv);

    int usage(const char* argv0);

    /**
     * Create a SampleCounter for each of the sample tags in the given
     * sensors, if any.  Set @p singlemote to expect only one mote for each
     * sensor type.  When a wisard sensor returns multiple sample tags with
     * different mote IDs, samples will only be expected from one of those
     * tags.
     **/
    void
    createCounters(const list<DSMSensor*>& sensors);

    /**
     * Copy the counter into the samples map and return a reference to it.
     * Also configure the counter according to whether json output or data
     * statistics should be enabled.  Mote sensor IDs for the same sensor
     * type will be hashed to one bucket, as will sensors with different
     * mote IDs if _singlemote is enabled.  This should only be called if
     * getCounter() does not return a counter for the sample ID.
     **/
    SampleCounter*
    addCounter(const SampleCounter& counter);

    /**
     * Return a pointer to the SampleCounter for the given sample id @p sid.
     * If no such counter exists, return null.  Mote sensor sample IDs are
     * hashed into buckets by sensor type and according to the _singlemote
     * setting, so the returned SampleCounter may not have the same ID as
     * the one passed in.
     **/
    SampleCounter*
    getCounter(dsm_sample_id_t sid);

    bool
    reportsExhausted(int nreports=-1)
    {
        // Just to avoid the unused warning, while allowing _nreports to be
        // incremented with a prefix increment operator in the call to this
        // method.
        if (nreports > -1)
            _nreports = nreports;
        return (_count > 0 && _nreports > _count);
    }

    /**
     * Recalculate statistics as needed based on the current collection of
     * data and time period.
     **/
    void
    restartStats(const UTime& start, const UTime& end);

    void
    clearSampleQueue();

    void
    report();

    static void handleSignal(int signum);

    // *** SampleClient interface ***

    void flush() throw() {}

    bool receive(const Sample* samp) throw();

    // ********************************

    void printReport(std::ostream& outs);

    void jsonReport();

    void resetResults();

    void setStart(const UTime& start);

private:
    static const int DEFAULT_PORT = 30000;

    static bool _alarm;

    typedef map<dsm_sample_id_t, SampleCounter> sample_map_t;
    sample_map_t _samples;

    dsm_sample_id_t
    hashId(dsm_sample_id_t sid);

    bool _reportall;
    bool _reportdata;
    bool _singlemote;
    bool _fullnames;

    bool _realtime;
    bool _counters_created{false};
    list<DSMSensor*> allsensors{};

    UTime _period_start;
    UTime _period_end;
    UTime _start_time;
    int _count;
    int _period;
    int _update;
    int _nreports;

    // The buffer of samples from which statistics will be generated.
    typedef std::list<const Sample*> sample_queue;
    sample_queue _sampleq;

    NidasApp _app;
    NidasAppArg Period;
    NidasAppArg Update;
    NidasAppArg Count;
    // Type of report to generate:
    //
    // All - show all samples, received or not
    // Missing - show only missing samples
    // Compact - report only one line for a site with no samples for any sensors
    // Received - show only received samples, the default
    NidasAppArg AllSamples;

    // Show averaged data or raw messages for each report.
    NidasAppArg ShowData;

    NidasAppArg SingleMote;
    NidasAppArg Fullnames;
    BadSampleFilterArg FilterArg;
    NidasAppArg JsonOutput;
    NidasAppArg ShowProblems;
    NidasAppArg RoundStart;

#if NIDAS_JSONCPP_ENABLED
#if !NIDAS_JSONCPP_STREAMWRITER
    n_u::auto_ptr<Json::StyledStreamWriter> streamWriter;
    n_u::auto_ptr<Json::StyledStreamWriter> headerWriter;

    void
    createJsonWriters()
    {
        if (! streamWriter.get())
        {
            Json::StyledStreamWriter* sw = new Json::StyledStreamWriter("");
            // Can't set precision in the old StyledStreamWriter.
            streamWriter.reset(sw);

            Json::StyledStreamWriter* hw = new Json::StyledStreamWriter("  ");
            headerWriter.reset(hw);
        }
    }
#else
    n_u::auto_ptr<Json::StreamWriter> streamWriter;
    n_u::auto_ptr<Json::StreamWriter> headerWriter;

    void
    createJsonWriters()
    {
        if (! streamWriter.get())
        {
            Json::StreamWriterBuilder builder;
            builder.settings_["indentation"] = "";
            // This is just a guess at a reasonable value.  The goal is to make
            // the output a little more human readable and concise.
            builder.settings_["precision"] = 5;
            streamWriter.reset(builder.newStreamWriter());

            builder.settings_["indentation"] = "  ";
            headerWriter.reset(builder.newStreamWriter());
        }
    }
#endif
#endif
};


bool DataStats::_alarm(false);


void
DataStats::handleSignal(int signum)
{
    // The NidasApp handler sets interrupted before calling this handler,
    // so clear that if this is just the interval alarm.
    if (signum == SIGALRM)
    {
        NidasApp::setInterrupted(false);
        _alarm = true;
    }
}


DataStats::DataStats():
    _samples(),
    _reportall(false),
    _reportdata(false),
    _singlemote(false),
    _fullnames(false),
    _realtime(false),
    _period_start(UTime::ZERO),
    _period_end(UTime::ZERO),
    _start_time(UTime::ZERO),
    _count(1), _period(0), _update(0), _nreports(0),
    _sampleq(),
    _app("data_stats"),
    Period("-P,--period", "<seconds>",
           "Compute statistics over the given number of seconds.\n"
           "Defaults to the update period, if specified.", "0"),
    Update("-U,--update", "<seconds>",
           "Update rolling statistics at given interval.\n"
           "Defaults to the sample period, if that is specified.\n"
           "For example, '--period 60 --update 5' computes statistics\n"
           "every 5 seconds over the last 60 seconds of data", "0"),
    Count("-n,--count", "<count>",
          "Generate <count> reports with --period.\n"
          "Use a count of zero to continue reports until interrupted.", "1"),
    AllSamples("-a,--all", "",
               "Show statistics for all sample IDs, including those for which "
               "no samples are received."),
    ShowData("-D,--data", "",
             "Print data for each sample, either the last received message\n"
             "for raw samples, or data values averaged over the recording\n"
             "period for processed samples.\n"
             "When JSON output is enabled, this includes all the data values\n"
             "in the JSON file and also writes the data as JSON to stdout, so\n"
             "usually -D is only useful with --json for short periods of data.\n"
             "JSON currently only includes processed data, not raw, so\n"
             "usually -p is used with -D and JSON output."),
    SingleMote("--onemote", "",
               "Expect each wisard sensor type to come from a single mote,\n"
               "so mote IDs are not differentiated in sample tags for the same\n"
               "type of sensor.  If there are two motes on a DSM, then any\n"
               "sensor duplication will report a warning, including Vmote."),
    Fullnames("-F,--fullnames", "",
              "Report variable and device names for each for each sensor."),
    FilterArg(),
    JsonOutput("--json", "<path>",
R"(Write json stream headers to path.  The output is first
written to a filename with .tmp appended, then renamed to the
given path.  The json file contains an object which maps each
streamid to the header object for that stream.  The header
contains stream metadata and a dictionary of variables.  With
--data, the sample data will be written to stdout as a
newline-separated json stream, where each json line is an object
with fields for each variable set to an array of values.  The
'streamid' in the data object relates to the header with that
streamid.  Use with -p to compute stats for individual variables.
The path argument can include strptime() time specifiers, which
will be interpolated with each stats period start time.)"
               ),
    ShowProblems("-e,--errors", "",
R"(Print errors (aka problems) detected in processed data, such as
missing values and mismatched sample rates.  JSON reports always
include problems.)"),
    RoundStart("--round", "SECONDS",
R"(Round the start time of each period to the nearest interval of
length SECONDS.  The period start time is only ever shown in log
messages and the json output, since the sample statistics always
show the actual sample times.)"),
#if NIDAS_JSONCPP_ENABLED
    streamWriter(),
    headerWriter()
#endif
{
    NidasApp& app = _app;
    app.setApplicationInstance();
    app.setupSignals();
    app.enableArguments(app.XmlHeaderFile | app.loggingArgs() |
                        app.SampleRanges | app.FormatHexId |
                        app.FormatSampleId | app.ProcessData |
                        app.Version | app.InputFiles | FilterArg |
                        app.Help | Period | Update | Count |
                        AllSamples | ShowData | SingleMote | Fullnames |
                        ShowProblems | RoundStart);
#if NIDAS_JSONCPP_ENABLED
    app.enableArguments(JsonOutput);
#endif
    app.InputFiles.allowFiles = true;
    app.InputFiles.allowSockets = true;
    app.InputFiles.setDefaultInput("sock:localhost", DEFAULT_PORT);
}


int DataStats::parseRunstring(int argc, char** argv)
{
    // Setup a default log scheme which will get replaced if any logging is
    // configured on the command line.
    Logger::setScheme(LogScheme("data_stats").addConfig("notice"));

    try {
        ArgVector args = _app.parseArgs(argc, argv);
        if (_app.helpRequested())
        {
            return usage(argv[0]);
        }
        _period = Period.asInt();
        _update = Update.asInt();
        _count = Count.asInt();
        _singlemote = SingleMote.asBool();
        _reportall = AllSamples.asBool();
        _reportdata = ShowData.asBool();
        _fullnames = Fullnames.asBool();
        if (RoundStart.specified())
        {
            int seconds = RoundStart.asInt();
            if (seconds < 0 || seconds > _period)
                throw std::invalid_argument("rounding seconds must be "
                    "less than period and not negative");
        }

        if (_period < 0 || _update < 0)
        {
            throw NidasAppException("period or update cannot be negative.");
        }
        if (_period > 0 && _update == 0)
        {
            _update = _period;
        }
        if (_update > 0 && _period == 0)
        {
            _period = _update;
        }
        _app.parseInputs(args);
    }
    catch (NidasAppException& ex)
    {
        std::cerr << ex.what() << std::endl;
        return 1;
    }
    return 0;
}

int DataStats::usage(const char* argv0)
{
    cerr <<
        "Usage: " << argv0 << " [options] [inputURL] ...\n";
    cerr <<
        "Standard options:\n"
         << _app.usage();
    if (!_app.briefHelp())
    {
        cerr << "Examples:\n" <<
            argv0 << " xxx.dat yyy.dat\n" <<
            argv0 << " file:/tmp/xxx.dat file:/tmp/yyy.dat\n" <<
            argv0 << " -p -x ads3.xml sock:hyper:30000\n" <<
            argv0 << " --period 60 --update 5 -a -p -D -F" << endl;
    }
    return 1;
}

int DataStats::main(int argc, char** argv)
{
    DataStats stats;
    int result;
    if (! (result = stats.parseRunstring(argc, argv)))
    {
        try {
            result = stats.run();
        }
        catch (n_u::Exception& e)
        {
            cerr << e.what() << endl;
            XMLImplementation::terminate(); // ok to terminate() twice
            result = 1;
        }
    }
    return result;
}

class AutoProject
{
public:
    AutoProject() { Project::getInstance(); }
    ~AutoProject() { Project::destroyInstance(); }
};


void
DataStats::
readHeader(SampleInputStream& sis)
{
    // Loop over the header read until it is read or the periods expire.
    // Since the header is not sent until there's a sample to send, if
    // there are no samples we could block in readInputHeader() waiting for
    // the header and never get to the readSamples() loop.
    bool header_read = false;
    _nreports = 0;
    while (!header_read && !_app.interrupted() &&
           !reportsExhausted(++_nreports))
    {
        _alarm = false;
        if (_realtime)
            alarm(_update);
        try {
            sis.readInputHeader();
            header_read = true;
            // Reading the header does not count as a report cycle.
            --_nreports;
        }
        catch (n_u::IOException& e)
        {
            DLOG(("") << e.what() << " (errno=" << e.getErrno() << ")");
            if (e.getErrno() != ERESTART && e.getErrno() != EINTR)
                throw;
        }
        if (_realtime)
            alarm(0);
        if (_app.interrupted())
        {
            throw n_u::Exception("Interrupted while waiting for header.");
        }
        if (_alarm)
        {
            ostringstream outs;
            outs << "Header not received after " << _nreports
                 << " periods of " << _update << " seconds.";
            // Throw an exception if nreports exhausted.
            if (reportsExhausted())
            {
                throw n_u::Exception(outs.str());
            }
            else
            {
                cerr << outs.str() << endl;
            }
        }
    }
}


void
DataStats::
readSamples(SampleInputStream& sis)
{
    // Read samples until an alarm signals the end of a reporting period or
    // an interruption occurs.
    _alarm = false;
    if (_update > 0 && _realtime)
    {
        // Add some slack to the alarm to allow samples to arrive within the
        // period, rounded up.
        int delay = 1; // seconds of lag
        UTime when;
        if (when < _period_end)
        {
            when = _period_end - when.toUsecs();
            delay += when.toSecs();
        }
        DLOG(("alarm(") << delay << ") seconds until period ends at "
                        << iso_format(_period_end));
        alarm(delay);
    }
    while (!_alarm && !_app.interrupted())
    {
        try {
            sis.readSamples();
        }
        catch (n_u::IOException& e)
        {
            DLOG(("") << e.what() << " (errno=" << e.getErrno() << ")");
            if (e.getErrno() != ERESTART && e.getErrno() != EINTR)
                throw;
        }
    }
}


void
DataStats::
resetResults()
{
    sample_map_t::iterator si;
    for (si = _samples.begin(); si != _samples.end(); ++si)
    {
        si->second.reset();
    }
}


dsm_sample_id_t
DataStats::
hashId(dsm_sample_id_t sid)
{
    if (sid & 0x8000)
    {
        // collapse mote sensor types to one id
        dsm_sample_id_t mid = sid ^ (sid & 3);
        if (_singlemote)
        {
            mid = mid ^ (mid & 0xf00);
        }
        if (sid != mid)
        {
            VLOG(("hashed ID ") << _app.formatId(sid) << " to "
             << _app.formatId(mid));
        }
        sid = mid;
    }
    return sid;
}


SampleCounter*
DataStats::
getCounter(dsm_sample_id_t sid)
{
    SampleCounter *stats = 0;
    sample_map_t::iterator it = _samples.find(hashId(sid));
    if (it != _samples.end())
    {
        stats = &it->second;
    }
    return stats;
}


SampleCounter*
DataStats::
addCounter(const SampleCounter& counter)
{
    dsm_sample_id_t sid = hashId(counter.id);
    _samples[sid] = counter;
    SampleCounter& stats = _samples[sid];
    stats.enableData(_reportdata);
    stats.enableJSON(JsonOutput.specified());
    return &stats;
}


void
DataStats::createCounters(const list<DSMSensor*>& sensors)
{
    bool processed = _app.processData();
    SampleMatcher& matcher = _app.sampleMatcher();
    list<DSMSensor*>::const_iterator si;
    for (si = sensors.begin(); si != sensors.end(); ++si)
    {
        // Create a SampleCounter for samples from the given sensors.
        DSMSensor* sensor = *si;
        string sname = sensor->getDSMConfig()->getName() + ":" +
            sensor->getDeviceName();

        // Stop with raw samples if processed not requested.
        if (! processed)
        {
            if (matcher.match(sensor->getId()))
            {
                dsm_sample_id_t sid = sensor->getId();
                DLOG(("adding raw sample: ") << _app.formatId(sid));
                addCounter(SampleCounter(sid, sensor));
            }
            continue;
        }

        SampleTagIterator ti = sensor->getSampleTagIterator();
        for ( ; ti.hasNext(); ) {
            const SampleTag* stag = ti.next();
            const std::vector<const Variable*>& variables = stag->getVariables();
            if (variables.size() > 0)
            {
                // As a special case for wisard sensors, mask the last two
                // bits of the IDs so all "sensor types" aka I2C addresses
                // are treated like the same kind of sample.  We use the
                // first such ID and then map any others to that one, since
                // in most cases only one such ID will ever appear for all
                // four possible "sensor types".  However, there is some
                // risk this could hide multiple sensor types appearing in
                // the stream.  We can warn for that later if it happens.
                // Since the wisard ID mapping is taken care of in
                // getCounter(), here we just add the sensor if the ID
                // does not already have a stats entry.
                //
                // Note this just adds the first of possibly multiple
                // "sensor types" assigned to a sample.  The actual sample
                // IDs are not known until samples are received.  So that's
                // the point at which we can correct the ID so it is
                // accurate in the reports.  Likewise for mote IDs, since
                // samples for the same sensor type may have multiple
                // possible mote IDs to account for different mote IDs at
                // different DSMs.
                //
                // I suppose the other way to avoid redundant tags is to
                // compare the actual variable names, since those at least
                // should be unique.  The sample tags should be complete as
                // long as there is at least one sample tag for each
                // variable name.  That does not help for raw mode when no
                // project config is available, but it still might be a more
                // accurate and cleaner approach when there is a project
                // configuration.  Future implementation perhaps.
                dsm_sample_id_t sid = stag->getId();
                if (! matcher.match(sid))
                {
                    continue;
                }
                SampleCounter* stats = getCounter(sid);
                if (!stats)
                {
                    DLOG(("adding processed sample: ") << _app.formatId(sid));
                    stats = addCounter(SampleCounter(sid, sensor, stag));
                }
            }
        }
    }
}


bool
DataStats::
receive(const Sample* samp) throw()
{
    dsm_sample_id_t sampid = samp->getId();
    if (! _app.sampleMatcher().match(sampid))
    {
        return false;
    }

    if (!_counters_created)
    {
        _counters_created = true;
        createCounters(allsensors);
    }

    VLOG(("received and accepted sample ") << _app.formatId(sampid));
    SampleCounter* stats = getCounter(sampid);
    if (!stats)
    {
        // When there is no header from which to gather samples ahead of
        // time, just add a SampleCounter instance for any new raw sample
        // that arrives.
        DLOG(("creating counter for sample id ") << _app.formatId(sampid));
        stats = addCounter(SampleCounter(sampid));
    }
    // The samples only need to be queued if they will need to be "replayed"
    // through the counters to update statistics, or to cache samples which
    // are received past the current sample period.  In other words, queue
    // this sample if count is not 1 and a period has been set.  If no period
    // is set, meaning period and update are zero, stats will be accumulated
    // and updated in the Samplecounter even if the sample is not queue.  We
    // do nothing with samples which are too old.
    dsm_time_t sampt = samp->getTimeTag();
    if (_start_time.isZero())
    {
        // not realtime since start not set yet, so use first sample time.
        setStart(samp->getTimeTag());
    }
    if (_period > 0 && sampt < _period_start.toUsecs())
        return true;
    if (_count != 1 && _period > 0)
    {
        samp->holdReference();
        _sampleq.push_back(samp);
    }
    // Do not accumlate this sample into the statistics if it is not yet in
    // the current time period.
    if ((_period == 0) || (sampt < _period_end.toUsecs()))
    {
        stats->receive(samp);
    }
    // Otherwise note the end of the period if not realtime.
    if (_period && (sampt >= _period_end.toUsecs()))
    {
        _alarm = true;
    }
    return true;
}


void DataStats::printReport(std::ostream& outs)
{
    size_t maxnamelen = 6;
    int field_width[3] = {5,5,6};
    int dtlog10[2] = {7,7};

    sample_map_t::iterator si;
    for (si = _samples.begin(); si != _samples.end(); ++si)
    {
        SampleCounter &ss = si->second;
        if (ss.nsamps == 0 && !_reportall)
            continue;
        string name = ss.getHeaderLine(_fullnames);
        maxnamelen = std::max(maxnamelen, name.length());
        if (ss.nsamps >= 1)
        {
            field_width[0] = std::max(field_width[0], ndigits(ss.minlens)+1);
            field_width[1] = std::max(field_width[1], ndigits(ss.maxlens)+1);
            field_width[2] = std::max(field_width[2], ndigits(ss.totalBytes)-2);
        }
        // Skip min/max stats which will be printed as missing if there are
        // not at least two samples.
        if (ss.nsamps >= 2)
        {
            int dt = abs(ss.minDeltaTs);
            dtlog10[0] = std::max(dtlog10[0], ndigits(dt+1)+2);
            dt = ss.maxDeltaTs;
            dtlog10[1] = std::max(dtlog10[1], ndigits(dt+1)+2);
        }
    }

    // Truncate maxnamelen when fullnames is in effect.
    if (_fullnames)
    {
        maxnamelen = 0;
    }

    struct tm tm;
    char tstr[64];
    outs << left << setw(maxnamelen) << (maxnamelen > 0 ? "sensor" : "")
         << right
         << "  dsm sampid    nsamps |------- start -------|  |------ end -----|"
         << "    rate"
         << setw(dtlog10[0] + dtlog10[1]) << " minMaxDT(sec)"
         << setw(field_width[0] + field_width[1]) << " minMaxLen totalMB"
         << endl;

#if NIDAS_JSONCPP_ENABLED
    std::vector<Problem> problems;
#endif

    for (si = _samples.begin(); si != _samples.end(); ++si)
    {
        SampleCounter& ss = si->second;
#if NIDAS_JSONCPP_ENABLED
        Json::Value stats = ss.jsonStats(problems);
#endif

        if (ss.nsamps == 0 && !_reportall)
            continue;

        string t1str;
        string t2str;
        if (ss.nsamps > 0)
        {
            time_t ut = ss.t1s / USECS_PER_SEC;
            gmtime_r(&ut,&tm);
            strftime(tstr,sizeof(tstr),"%Y %m %d %H:%M:%S",&tm);
            int msec = (int)(ss.t1s % USECS_PER_SEC) / USECS_PER_MSEC;
            sprintf(tstr + strlen(tstr),".%03d",msec);
            t1str = tstr;
            ut = ss.t2s / USECS_PER_SEC;
            gmtime_r(&ut,&tm);
            strftime(tstr,sizeof(tstr),"%m %d %H:%M:%S",&tm);
            msec = (int)(ss.t2s % USECS_PER_SEC) / USECS_PER_MSEC;
            sprintf(tstr + strlen(tstr),".%03d",msec);
            t2str = tstr;
        }
        else
        {
            t1str = string((size_t)23, '*');
            t2str = string((size_t)18, '*');
        }

        // Put long variable names on a header line before statistics.
        string name = ss.getHeaderLine(_fullnames);
        if (_fullnames)
        {
            outs << left << name << endl;
        }

        outs << left << setw(maxnamelen) << (maxnamelen ? name : "")
             << right << ' ' << setw(4) << GET_DSM_ID(ss.id) << ' ';
        NidasApp* app = NidasApp::getApplicationInstance();
        app->formatSampleId(outs, ss.id);
        outs << " ";

        double rate = ss.computeRate();
        outs << setw(9) << ss.nsamps << ' '
             << t1str << "  " << t2str << ' '
             << fixed << setw(7) << setprecision(2)
             << check_valid(rate, bool(ss.nsamps > 1))
             << setw(dtlog10[0]) << setprecision(3)
             << check_valid((double)ss.minDeltaTs / MSECS_PER_SEC, (ss.nsamps > 1))
             << setw(dtlog10[1]) << setprecision(3)
             << check_valid((float)ss.maxDeltaTs / MSECS_PER_SEC, (ss.nsamps > 1))
             << setprecision(0)
             << setw(field_width[0]) << check_valid(ss.minlens, (ss.nsamps > 0))
             << setw(field_width[1]) << check_valid(ss.maxlens, (ss.nsamps > 0))
             << setprecision(1)
             << setw(field_width[2]) << check_valid((double)ss.totalBytes/1000000, (ss.nsamps > 0))
             << endl;

        if (_reportdata){
            ss.printData(outs);
        }
    }
#if NIDAS_JSONCPP_ENABLED
    if (ShowProblems.asBool())
    {
        for (auto& p: problems)
        {
            outs << "problem: " << p.printout() << std::endl;
        }
    }
#endif
}


void
DataStats::
restartStats(const UTime& start, const UTime& end)
{
    DLOG(("") << "restartStats(" << iso_format(start) << ", "
     << iso_format(end) << ")");

    // Clear out any samples which precede the start time.
    sample_queue::iterator it;
    dsm_time_t tstart(start.toUsecs());
    for (it = _sampleq.begin(); it != _sampleq.end(); )
    {
        if ((*it)->getTimeTag() < tstart)
        {
            (*it)->freeReference();
            it = _sampleq.erase(it);
        }
        else
        {
            ++it;
        }
    }

    // Tell all the counters to reset their statistics.
    resetResults();

    // Now replay all the samples from the queue prior to the new end time.
    for (it = _sampleq.begin(); it != _sampleq.end(); ++it)
    {
        const Sample* samp = *it;
        dsm_time_t sampt = samp->getTimeTag();
        if (sampt < end.toUsecs())
        {
            SampleCounter* stats = getCounter(samp->getId());
            stats->accumulateSample(samp);
        }
    }
}


void
DataStats::
clearSampleQueue()
{
    while (!_sampleq.empty())
    {
        const Sample* samp = _sampleq.front();
        _sampleq.pop_front();
        samp->freeReference();
    }
}


void
DataStats::
report()
{
    // Only report the period start and end when it's been set.
    if (_period > 0)
    {
        ILOG(("") << "Reporting stats from "
                << iso_format(_period_start) << " to "
                << iso_format(_period_end));
    }

    // Print results to stdout unless json output specified.  For json
    // output, write the headers and data to a file, and stream the data to
    // stdout in line-separated json.
    if (!JsonOutput.specified())
    {
        printReport(cout);
    }
    else
    {
        jsonReport();
    }
}

#if NIDAS_JSONCPP_ENABLED
inline
Json::Value&
createObject(Json::Value& value)
{
    value = Json::Value(Json::objectValue);
    return value;
}
#endif

void
DataStats::
jsonReport()
{
#if NIDAS_JSONCPP_ENABLED
    if (!streamWriter.get())
    {
        createJsonWriters();
    }
    std::string jsonname;
    jsonname = _period_start.format(true, JsonOutput.getValue());
    ILOG(("writing json to ") << jsonname);

    // Create a json object which contains all the headers and all the
    // data for all the SampleCounter streams, then write it out.
    Json::Value root;
    Json::Value timeperiod(Json::arrayValue);
    timeperiod.append(iso_format(_period_start));
    timeperiod.append(iso_format(_period_end));
    // Create three top-level objects, according to what has been enabled.
    Json::Value& stats = createObject(root["stats"]);
    Json::Value data;
    Json::Value& streams = createObject(root["stream"]);
    stats["timeperiod"] = timeperiod;
    stats["update"] = _update;
    stats["period"] = _period;
    stats["starttime"] = iso_format(_start_time);
    Json::Value& streamstats = createObject(stats["streams"]);

    std::vector<Problem> problems;
    sample_map_t::iterator si;
    for (si = _samples.begin(); si != _samples.end(); ++si)
    {
        SampleCounter* stream = &si->second;
        // Use a "stream" field in the top level object to provide a
        // "namespace" for stream objects.  Keeping "data" and
        // "stream" namespaces allows writing both into one file or
        // into different files, without modifying the schema
        // expected by consumers.
        streams[stream->streamid] = stream->jsonHeader();
        // Rewriting the data every time seems excessive, but at the
        // moment it's an expedient way to provide both headers and
        // data in one file for web clients.  Later the data could
        // be written into a different file.  Also, it might be
        // possible for the set of headers to change as streams come
        // and go, so writing them all together means they are
        // always in sync and consistent.  Every data object will
        // have a corresponding stream header in the same file.
        if (stream->nsamps > 0 || _reportall)
        {
            if (_reportdata)
                data[stream->streamid] = stream->jsonData();
            streamstats[stream->streamid] = stream->jsonStats(problems);
        }
    }
    if (_reportdata)
        root["data"] = data;
    stats["problems"] = Problem::asJsonArray(problems);

    std::ofstream json;
    // Write to a temporary file first, then move into place.
    std::string tmpname = jsonname + ".tmp";
    json.open(tmpname.c_str());
#if !NIDAS_JSONCPP_STREAMWRITER
    headerWriter->write(json, root);
#else
    headerWriter->write(root, &json);
#endif
    json.close();
    // Now move into place.
    ::rename(tmpname.c_str(), jsonname.c_str());

    if (_reportdata)
    {
        // Now stream the data to stdout.
        for (si = _samples.begin(); si != _samples.end(); ++si)
        {
            SampleCounter* stream = &si->second;
            if (stream->nsamps > 0 || _reportall)
            {
#if !NIDAS_JSONCPP_STREAMWRITER
                streamWriter->write(std::cout, stream->jsonData());
#else
                streamWriter->write(stream->jsonData(), &std::cout);
#endif
                std::cout << std::endl;
            }
        }
    }
#endif
}


void DataStats::setStart(const UTime& start)
{
    _period_start = start;
    if (RoundStart.specified())
    {
        int seconds = RoundStart.asInt();
        _period_start = start.round(seconds*USECS_PER_SEC);
    }
    _period_end = _period_start + _period * USECS_PER_SEC;
    if (_update < _period)
    {
        _period_end = _period_start + _update * USECS_PER_SEC;
    }
    _start_time = _period_start;
}


int DataStats::run()
{
    int result = 0;

    AutoProject aproject;
    IOChannel* iochan = 0;

    if (_app.dataFileNames().size() > 0)
    {
        nidas::core::FileSet* fset =
            nidas::core::FileSet::getFileSet(_app.dataFileNames());
        iochan = fset->connect();
    }
    else
    {
        n_u::Socket* sock = new n_u::Socket(*_app.socketAddress());
        iochan = new nidas::core::Socket(sock);
        _realtime = true;
    }

    SampleInputStream sis(iochan, _app.processData());
    sis.setBadSampleFilter(FilterArg.getFilter());
    DLOG(("filter setting: ") << FilterArg.getFilter());

    if (_update > 0 && _realtime)
    {
        _app.addSignal(SIGALRM, &DataStats::handleSignal, true);
    }
    readHeader(sis);
    const SampleInputHeader& header = sis.getInputHeader();

    string xmlFileName = _app.xmlHeaderFile();
    if (xmlFileName.length() == 0)
        xmlFileName = header.getConfigName();
    xmlFileName = n_u::Process::expandEnvVars(xmlFileName);

    struct stat statbuf;
    if (::stat(xmlFileName.c_str(), &statbuf) == 0 || _app.processData())
    {
        n_u::auto_ptr<xercesc::DOMDocument>
            doc(parseXMLConfigFile(xmlFileName));

        Project::getInstance()->fromDOMElement(doc->getDocumentElement());

        DSMConfigIterator di = Project::getInstance()->getDSMConfigIterator();
        for ( ; di.hasNext(); )
        {
            const DSMConfig* dsm = di.next();
            const list<DSMSensor*>& sensors = dsm->getSensors();
            allsensors.insert(allsensors.end(),sensors.begin(),sensors.end());
        }
    }
    XMLImplementation::terminate();

    SamplePipeline pipeline;
    if (_app.processData())
    {
        pipeline.setRealTime(false);
        pipeline.setRawSorterLength(0);
        pipeline.setProcSorterLength(0);

        list<DSMSensor*>::const_iterator si;
        for (si = allsensors.begin(); si != allsensors.end(); ++si) {
            DSMSensor* sensor = *si;
            sensor->init();
            //  1. inform the SampleInputStream of what SampleTags to expect
            sis.addSampleTag(sensor->getRawSampleTag());
        }
        // 2. connect the pipeline to the SampleInputStream.
        pipeline.connect(&sis);

        // 3. connect the client to the pipeline
        pipeline.getProcessedSampleSource()->addSampleClient(this);
    }
    else
    {
        sis.addSampleClient(this);
    }

    try
    {
        // If realtime, set the period being covered now.  The period starts
        // now, since that is when data starts being collected, and it goes
        // until the next update or next period, whichever is shorter.  If
        // period is shorter, then the first update comes when a whole
        // period has filled, and then every update seconds after that.  If
        // update is shorter, then the period does not fill until enough
        // updates have passed.
        //
        // If not realtime, then the sample times will be used as the "clock".
        if (_realtime)
        {
            setStart(UTime());
        }
        if (_period > 0 && _realtime)
        {
            ILOG(("") << "... Reporting stats over "
                      << _period << " seconds, "
                      << "updating " << _update
                      << " seconds, " << iso_format(_period_start)
                      << " ...");
        }
        while (!_app.interrupted() && !reportsExhausted(++_nreports))
        {
            readSamples(sis);
            report();
            if (_period > 0)
            {
                // Advance the period end by the update time, but do not
                // advance the period start until the period is long enough.
                _period_end += _update * USECS_PER_SEC;
                if (_period_end - _period * USECS_PER_SEC > _period_start)
                {
                    _period_start = _period_end - _period * USECS_PER_SEC;
                }
                // Reset statistics to start accumulating for the new time period.
                restartStats(_period_start, _period_end);
            }
        }
    }
    catch (n_u::EOFException& e)
    {
        cerr << e.what() << endl;
        report();
    }
    catch (n_u::IOException& e)
    {
        if (_app.processData())
        {
            pipeline.getProcessedSampleSource()->removeSampleClient(this);
            pipeline.disconnect(&sis);
            pipeline.interrupt();
            pipeline.join();
        }
        else
        {
            sis.removeSampleClient(this);
        }
        sis.close();
        report();
        throw(e);
    }
    if (_app.processData())
    {
        pipeline.disconnect(&sis);
        pipeline.flush();
        pipeline.getProcessedSampleSource()->removeSampleClient(this);
    }
    else
    {
        sis.removeSampleClient(this);
    }
    sis.close();
    pipeline.interrupt();
    pipeline.join();
    return result;
}

int main(int argc, char** argv)
{
    return DataStats::main(argc, argv);
}
