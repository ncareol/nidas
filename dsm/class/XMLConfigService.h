/*
 ********************************************************************
    Copyright by the National Center for Atmospheric Research

    $LastChangedDate: 2004-10-15 17:53:32 -0600 (Fri, 15 Oct 2004) $

    $LastChangedRevision$

    $LastChangedBy$

    $HeadURL: http://orion/svn/hiaper/ads3/dsm/class/RTL_DSMSensor.h $
 ********************************************************************

*/


#ifndef DSM_XMLCONFIGSERVICE_H
#define DSM_XMLCONFIGSERVICE_H

#include <DSMService.h>

namespace dsm {

class XMLConfigService: public DSMService
{
public:
    XMLConfigService();

    /**
     * Copy constructor.
     */
    XMLConfigService(const XMLConfigService&);

    ~XMLConfigService();

    int run() throw(atdUtil::Exception);

    void offer(atdUtil::Socket* sock,int pseudoPort) throw(atdUtil::Exception);

    void schedule() throw(atdUtil::Exception);

/*
    int getPseudoPort() const
    {
        return XML_CONFIG;
	return output->getPseudoPort();
    }
*/

    void fromDOMElement(const xercesc::DOMElement* node)
	throw(atdUtil::InvalidParameterException);

    xercesc::DOMElement*
    	toDOMParent(xercesc::DOMElement* parent)
		throw(xercesc::DOMException);

    xercesc::DOMElement*
    	toDOMElement(xercesc::DOMElement* node)
		throw(xercesc::DOMException);

protected:
    Output* output;
};

}

#endif
