#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
using boost::unit_test_framework::test_suite;

#include <boost/regex.hpp>

#include <nidas/core/Parameter.h>
#include <nidas/core/XMLParser.h>

using namespace nidas::util;
using namespace nidas::core;


namespace {
    struct TFixture
    {
        ~TFixture() {
            XMLImplementation::terminate();
        }
    };
}


const char xml_bool[] = R"(
<parameter name='working' type='bool' value='true false 0 1'/>
)";


const char xml_string[] = R"(
<parameter name='onestring' type='string'
           value='one string to rule them all'/>
)";


const char xml_strings[] = R"(
<parameter name='nstrings' type='strings'
           value='one string to rule them all'/>
)";


BOOST_AUTO_TEST_CASE(test_parameters)
{
    TFixture fix;
    // in the normal case, we can create a parameter using the template
    // subclasses.

    std::unique_ptr<ParameterT<float> > pint{ new ParameterT<float>() };
    pint->setValue(99);
    BOOST_TEST(pint->getName() == "");
    BOOST_TEST(pint->getValue(0) == 99);

    std::unique_ptr<xercesc::DOMDocument>
        doc{XMLParser::ParseString(xml_bool)};

    std::unique_ptr<Parameter> param{
        Parameter::createParameter(doc->getDocumentElement()) };
    BOOST_TEST(param.get());
    BOOST_TEST(param->getName() == "working");
    BOOST_TEST(param->getLength() == 4);
    // false because there is more than one value
    BOOST_TEST(param->getBoolValue() == false);
    BOOST_TEST(param->getNumericValue(0) == 1);
    BOOST_TEST(param->getNumericValue(1) == 0);
    BOOST_TEST(param->getNumericValue(2) == 0);
    BOOST_TEST(param->getNumericValue(3) == 1);

    doc.reset(XMLParser::ParseString(xml_string));
    param.reset(Parameter::createParameter(doc->getDocumentElement()));
    BOOST_TEST(param->getName() == "onestring");
    BOOST_TEST(param->getLength() == 1);
    BOOST_TEST(param->getStringValue(0) == "one string to rule them all");

    doc.reset(XMLParser::ParseString(xml_strings));
    param.reset(Parameter::createParameter(doc->getDocumentElement()));
    BOOST_TEST(param->getName() == "nstrings");
    BOOST_TEST(param->getLength() == 6);
    BOOST_TEST(param->getStringValue(0) == "one");
    BOOST_TEST(param->getStringValue(5) == "all");

}
