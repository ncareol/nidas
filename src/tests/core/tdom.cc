
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
using boost::unit_test_framework::test_suite;

#include <nidas/core/DOMable.h>
#include <nidas/core/XMLParser.h>
#include <nidas/core/Project.h>
#include <nidas/core/DSMConfig.h>
#include <nidas/core/DSMSensor.h>
#include <nidas/core/Site.h>

using namespace nidas::core;
using nidas::util::InvalidParameterException;


namespace {
    struct TFixture
    {
        ~TFixture() {
            XMLImplementation::terminate();
        }
    };
}


struct TDomable: public DOMable
{
    using DOMable::getHandledAttributes;
    using DOMable::getAttribute;
    using DOMable::checkUnhandledAttributes;

    virtual void fromDOMElement(const xercesc::DOMElement* node) override
    {
        DOMableContext dmc(this, "TDomable: ", node);
        std::string value;
        getAttribute(node, "name", name);
        addContext(name);
        base_context = context();
        getAttribute(node, "last", last);
        if (getAttribute(node, "x", value))
            x = asInt(value);
        if (getAttribute(node, "why", value))
            why = asBool(value);
    }

    std::string name{};
    std::string last{};
    int x{0};
    bool why{false};

    std::string base_context{};
};


const char xml[] = R"(
<tdom name='hello' last='goodbye' why='true' x='2'/>
)";


BOOST_AUTO_TEST_CASE(test_domable)
{
    TFixture fix;
    TDomable tdom;

    std::unique_ptr<xercesc::DOMDocument> doc{XMLParser::ParseString(xml)};
    xercesc::DOMElement* node{doc->getDocumentElement()};

    BOOST_TEST(tdom.getHandledAttributes().size() == 0);

    tdom.fromDOMElement(node);

    BOOST_TEST(tdom.name == "hello");
    BOOST_TEST(tdom.last == "goodbye");
    BOOST_TEST(tdom.x == 2);
    BOOST_TEST(tdom.why == true);

    std::vector<std::string> xatts{"name", "last", "x", "why"};
    BOOST_TEST(tdom.getHandledAttributes() == xatts);

    std::string value{"unset"};
    BOOST_TEST(tdom.getAttribute(node, "notthere", value) == false);
    BOOST_TEST(value == "unset");
    BOOST_TEST(tdom.getAttribute(node, "name", value) == true);
}


// subclass TDom to test context stacking and detection of unhandled
// attributes

struct TDomSub: public TDomable
{
    virtual void fromDOMElement(const xercesc::DOMElement* node) override
    {
        DOMableContext dmc(this, "TDomSub: ", node);
        TDomable::fromDOMElement(node);
        sub_context = context();
        getAttribute(node, "because", because);
    }

    std::string because{};

    std::string sub_context{};
};


BOOST_AUTO_TEST_CASE(test_domable_subclass)
{
    TFixture fix;
    TDomSub tdom;

    std::unique_ptr<xercesc::DOMDocument> doc{XMLParser::ParseString(xml)};
    xercesc::DOMElement* node{doc->getDocumentElement()};

    tdom.fromDOMElement(node);

    BOOST_TEST(tdom.base_context == "TDomable: hello");
    BOOST_TEST(tdom.sub_context == "TDomSub: ");

    BOOST_TEST(tdom.because == "");
    BOOST_TEST(tdom.last == "goodbye");
    BOOST_TEST(tdom.x == 2);
    BOOST_TEST(tdom.why == true);
}


const char xml_wrong[] = R"(
<tdom name='hello' last='goodbye' why='true' x='2' extra='problem'/>
)";


BOOST_AUTO_TEST_CASE(test_unhandled_attribute)
{
    TFixture fix;
    TDomSub tdom;
    std::unique_ptr<xercesc::DOMDocument>
        doc{XMLParser::ParseString(xml_wrong)};
    xercesc::DOMElement* node{doc->getDocumentElement()};

    try
    {
        tdom.fromDOMElement(node);
        BOOST_FAIL("expected exception for unhandled attribute");
    }
    catch (const InvalidParameterException& ex)
    {
        BOOST_TEST(true, std::string("exception caught: ") + ex.what());
    }
}


BOOST_AUTO_TEST_CASE(test_cross_sites)
{
    TFixture fix;
    nidas::core::Project project;
    // the test is if it can be read without an expcetion due to duplicated
    // variable name, meaning the site attribute for the second sonic is
    // accepted.
    project.parseXMLConfigFile("test_m2hats.xml");

    DSMConfig* t2 = const_cast<DSMConfig*>(project.findDSM("t2"));
    BOOST_REQUIRE(t2);
    BOOST_TEST(t2->getId() == 2);
    auto sensors = t2->getSensors();
    // second sensor is first sonic
    dsm_sample_id_t sid = SET_DSM_ID(0, 2) | SET_SPS_ID(0, 220);
    DSMSensor* sonic1 = project.findSensor(sid);
    DSMSensor* sonic2 = project.findSensor(sid-200);

    // make sure SITE token is t1 on sonic1 and t2 on sonic2
    std::string site_token;
    BOOST_TEST(sonic1->getTokenValue("SITE", site_token));
    BOOST_TEST(site_token == "t1");
    BOOST_TEST(sonic2->getTokenValue("SITE", site_token));
    BOOST_TEST(site_token == "t2");

    // setting same site on both should trigger exception on validate()
    try
    {
        sonic1->setSite(const_cast<Site*>(sonic2->getSite()));
        // call Site::validate() for the duplicate variable name check.
        Site* site = const_cast<Site*>(sonic1->getSite());
        site->validate();
        BOOST_FAIL("exception for duplicate variable name not thrown.");
    }
    catch (const InvalidParameterException& ex)
    {
        BOOST_TEST(true, "exception caught");
    }
}
