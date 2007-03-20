/*
 ******************************************************************
    Copyright 2005 UCAR, NCAR, All Rights Reserved

    $LastChangedDate: 2007-03-16 23:40:37 -0600 (Fri, 16 Mar 2007) $

    $LastChangedRevision: 3736 $

    $LastChangedBy: maclean $

    $HeadURL: http://svn/svn/nids/trunk/src/nidas/dynld/DSC_AnalogOut.cc $

 ******************************************************************
*/

#include <nidas/dynld/DSC_AnalogOut.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <iostream>
#include <sstream>
#include <cmath>

using namespace nidas::dynld;
using namespace nidas::core;
using namespace std;

namespace n_u = nidas::util;

NIDAS_CREATOR_FUNCTION(DSC_AnalogOut)

DSC_AnalogOut::DSC_AnalogOut(): fd(-1),noutputs(0)
{
    memset(&conv,0,sizeof(conv));
}

DSC_AnalogOut::~DSC_AnalogOut()
{
    try {
        if (fd >= 0) close();
    }
    catch(const n_u::IOException& e) {}
}

void DSC_AnalogOut::open() throw(n_u::IOException)
{

    if ((fd = ::open(devName.c_str(),O_RDWR)) < 0)
        throw n_u::IOException(devName,"open",errno);

    if ((noutputs = ::ioctl(fd,DMMAT_D2A_GET_NOUTPUTS,0)) < 0)
        throw n_u::IOException(devName,"ioctl GET_NOUTPUTS",errno);

    if (::ioctl(fd,DMMAT_D2A_GET_CONVERSION,&conv) < 0)
        throw n_u::IOException(devName,"ioctl GET_CONVERSION",errno);

}


void DSC_AnalogOut::close() throw(n_u::IOException)
{
    // don't bother checking for error.
    if (fd >= 0) ::close(fd);
    fd = -1;
}


float DSC_AnalogOut::getMinVoltage() const
{
    return conv.vmin;
}

float DSC_AnalogOut::getMaxVoltage() const
{
    return conv.vmax;
}

void DSC_AnalogOut::setVoltages(const vector<int>& which,
    const vector<float>& val)
            throw(nidas::util::IOException,
                nidas::util::InvalidParameterException)
{
    if (which.size() != val.size()) {
        ostringstream ost;
        ost << "length of which=" << which.size() <<
            " is not equal to " << val.size();
        throw n_u::InvalidParameterException(getName(),"setVoltage",
            ost.str());
    }

    DMMAT_D2A_Outputs out;
    for (int i = 0; i < noutputs; i++) out.active[i] = out.counts[i] = 0;

    for (unsigned int i = 0; i < which.size(); i++) {

        if (which[i] < 0 || which[i] >= noutputs)  {
            ostringstream ost;
            ost << "output number=" << which[i] <<
                " is out of the range 0:" << noutputs - 1;
            throw n_u::InvalidParameterException(getName(),"setVoltage",
                ost.str());
        }

        out.active[which[i]] = 1;
        int cout = conv.cmin +
            (int)rint(val[i] /
                (conv.vmax - conv.vmin) * (conv.cmax - conv.cmin));
        cout = std::max(cout,conv.cmin);
        cout = std::min(cout,conv.cmax);
        out.counts[which[i]] = cout;
    }

    if (::ioctl(fd,DMMAT_D2A_SET,&out) < 0)
        throw n_u::IOException(devName,"ioctl SET_OUTPUT",errno);
}

void DSC_AnalogOut::setVoltage(int which,float val)
            throw(nidas::util::IOException,
                nidas::util::InvalidParameterException)
{
    vector<int> whiches;
    whiches.push_back(which);
    vector<float> vals;
    vals.push_back(val);
    setVoltages(whiches,vals);
}

void DSC_AnalogOut::fromDOMElement(
	const xercesc::DOMElement* node)
    throw(n_u::InvalidParameterException)
{
}

