/* vim: set shiftwidth=4 softtabstop=4 expandtab: */
/* -*- mode: C++; c-basic-offset: 4; -*- */
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

#include "PSQLProcessor.h"
#include "PSQLSampleOutput.h"

#include <nidas/core/Site.h>
#include <nidas/core/Project.h>
#include <nidas/core/DSMConfig.h>
#include <nidas/util/Logger.h>

using namespace nidas::dynld::psql;
using namespace nidas::core;
using namespace std;

using nidas::util::IOException;

NIDAS_CREATOR_FUNCTION_NS(psql,PSQLProcessor)

PSQLProcessor::
PSQLProcessor(): 
    input(0), site(0)
{
    setName("PSQLProcessor");
    averager.setAveragePeriod(MSECS_PER_SEC);
}

PSQLProcessor::PSQLProcessor(const PSQLProcessor& x):
    SampleIOProcessor((const SampleIOProcessor&)x),input(0),
    averager(x.averager),
    site(0)
{
    setName("PSQLProcessor");
}

PSQLProcessor::~PSQLProcessor()
{
}

PSQLProcessor* PSQLProcessor::clone() const {
    return new PSQLProcessor(*this);
}

void PSQLProcessor::connect(SampleInput* newinput) 
    throw(IOException)
{
    input = newinput;

    list<const SampleTag*> itags(input->getSampleTags().begin(),
	input->getSampleTags().end());
    list<const SampleTag*>::const_iterator si = itags.begin();

    for ( ; si != itags.end(); ++si) {
	const SampleTag* stag = *si;

	if (!site)
	{
	    const DSMConfig* dsm =
		    Project::getInstance()->findDSM(stag->getDSMId());
	    site = dsm->getSite();
	}
    }

    if (!site)
	throw IOException ("PSQLProcessor","PSQLProcessor","No sites found!");

    const list<const DSMConfig*>& dsms = site->getDSMConfigs();
    list<const DSMConfig*>::const_iterator di;
    for (di = dsms.begin(); di != dsms.end(); ++di) {
        const DSMConfig* dsm = *di;

	const list<DSMSensor*>& sensors = dsm->getSensors();
	list<DSMSensor*>::const_iterator si;
	for (si = sensors.begin(); si != sensors.end(); ++si) {
	    DSMSensor* sensor = *si;
	    const std::set<const SampleTag*>& tags = sensor->getSampleTags();

	    std::set<const SampleTag*>::const_iterator ti;
	    for (ti = tags.begin(); ti != tags.end(); ++ti) {
	    	const SampleTag* tag = *ti;             

                if (!tag->isProcessed()) continue;

		const vector<const Variable*>& vars = tag->getVariables();
		vector<const Variable*>::const_iterator vi;
		for (vi = vars.begin(); vi != vars.end(); ++vi) {
		    const Variable* var = *vi;
		    averager.addVariable(var);
		}
	    }
	}
    }

    averager.init();
    for (di = dsms.begin(); di != dsms.end(); ++di) {
        const DSMConfig* dsm = *di;

        const list<DSMSensor*>& sensors = dsm->getSensors();
        list<DSMSensor*>::const_iterator si;
        for (si = sensors.begin(); si != sensors.end(); ++si) {
            DSMSensor* sensor = *si;
#ifdef DEBUG
            cerr << "PSQLProcessor::connect, input=" <<
                    input->getName() << " sensor=" <<
                        sensor->getName() << endl;
#endif
            input->addProcessedSampleClient(&averager,sensor);
        }
    }
    SampleIOProcessor::connect(input);

}
 
void PSQLProcessor::disconnect(SampleInput* oldinput) throw(IOException)
{
    if (!input || !site) return;
    assert(input == oldinput);

    const list<const DSMConfig*>& dsms = site->getDSMConfigs();
    list<const DSMConfig*>::const_iterator di;
    for (di = dsms.begin(); di != dsms.end(); ++di) {
        const DSMConfig* dsm = *di;

        const list<DSMSensor*>& sensors = dsm->getSensors();
        list<DSMSensor*>::const_iterator si;
        for (si = sensors.begin(); si != sensors.end(); ++si) {
            DSMSensor* sensor = *si;
            input->removeProcessedSampleClient(&averager,sensor);
        }
    }
    averager.flush();
    SampleIOProcessor::disconnect(input);
    input = 0;
}
 
void
PSQLProcessor::
connected(SampleOutput* orig, SampleOutput* output) throw()
{
    PSQLSampleOutput* psqlOutput =
	    dynamic_cast<PSQLSampleOutput*>(output);
    if (psqlOutput) psqlOutput->addSampleTag(averager.getSampleTag());
    else nidas::util::Logger::getInstance()->log(LOG_ERR,
	"%s is not a PSQLSampleOutput", output->getName().c_str());

    SampleIOProcessor::connected(orig, output);
    averager.addSampleClient(output);
}
 
void PSQLProcessor::disconnected(SampleOutput* output) throw()
{
    averager.removeSampleClient(output);
    SampleIOProcessor::disconnected(output);
}

