
#define BOOST_TEST_DYN_LINK
#include <boost/test/unit_test.hpp>
using boost::unit_test_framework::test_suite;

#include <nidas/dynld/DSMSerialSensor.h>
#include <nidas/core/XMLParser.h>
#include <nidas/core/Project.h>
#include <nidas/core/Prompt.h>
#include <xercesc/framework/MemBufInputSource.hpp>
#include <memory>
#include <nidas/util/util.h>

using namespace nidas::util;
using namespace nidas::core;
using nidas::dynld::DSMSerialSensor;

xercesc::DOMDocument*
parseString(const std::string& xml)
{
    XMLParser parser;
    xercesc::MemBufInputSource mbis((const XMLByte *)xml.c_str(),
                    xml.size(), "buffer", false);
    xercesc::DOMDocument* doc = parser.parse(mbis);
    return doc;
}


void
load_prompt_xml(Prompt& prompt, const std::string& pxml)
{
    std::string xml = Project::getInstance()->expandString(pxml);
    std::unique_ptr<xercesc::DOMDocument> doc(parseString(xml));
    prompt.fromDOMElement(doc->getDocumentElement());
    doc.reset();
    XMLImplementation::terminate();
    Project::destroyInstance();
}


void
load_sensor_xml(DSMSerialSensor& dss, const std::string& xml_in)
{
    dss.setDSMId(1);

    std::string xml = Project::getInstance()->expandString(xml_in);
    std::unique_ptr<xercesc::DOMDocument> doc(parseString(xml));
    dss.fromDOMElement(doc->getDocumentElement());
    dss.validate();
    dss.init();

    doc.reset();
    XMLImplementation::terminate();
}



BOOST_AUTO_TEST_CASE(load_prompt_from_xml)
{
    Prompt prompt;
    BOOST_CHECK(!prompt.valid());
    BOOST_TEST(prompt.toXML() == "<prompt/>");
    std::string xml = "<prompt string='4D0!' rate='0.002' offset='55'/>";
    load_prompt_xml(prompt, xml);
    BOOST_CHECK_EQUAL(prompt.getString(), "4D0!");
    BOOST_CHECK_EQUAL(prompt.getRate(), 0.002);
    BOOST_CHECK_EQUAL(prompt.getOffset(), 55);
    BOOST_TEST(prompt.toXML() == xml);
    BOOST_CHECK(prompt.valid());

    prompt = Prompt();
    BOOST_CHECK(!prompt.valid());
    load_prompt_xml(prompt,
                    "<prompt string='HERE:'/>");
    BOOST_CHECK_EQUAL(prompt.getString(), "HERE:");
    BOOST_CHECK_EQUAL(prompt.getRate(), 0.0);
    BOOST_CHECK_EQUAL(prompt.getOffset(), 0);
    BOOST_CHECK(prompt.hasPrompt());
    // prompt without a rate is invalid, even with a prompt string.
    BOOST_CHECK(!prompt.valid());
}


BOOST_AUTO_TEST_CASE(invalid_prompt_xml_throws)
{
    Prompt prompt;

    std::vector<std::string> xmls { 
        "<prompt string='HERE:' offset=''/>",
        "<prompt string='HERE:' rate=''/>",
        "<prompt string='HERE:' rate='-1'/>",
        "<prompt string='HERE:' rate='x'/>"
    };

    for (auto xml: xmls)
    {
        try {
            load_prompt_xml(prompt, xml);
            BOOST_FAIL(std::string("exception not thrown: ") + xml);
        }
        catch (const InvalidParameterException& ipe)
        {
            BOOST_CHECK(true);
        }
    }
}


BOOST_AUTO_TEST_CASE(prompt_xml_defaults)
{
    // check that unspecified attributes are reset to defaults.
    Prompt prompt;
    load_prompt_xml(prompt, "<prompt string='4D0!'/>");
    BOOST_TEST(prompt.getString() == "4D0!");
    BOOST_TEST(prompt.getRate() == 0);
    BOOST_TEST(prompt.getOffset() == 0);

    load_prompt_xml(prompt, "<prompt rate='2'/>");
    BOOST_TEST(prompt.getString() == "");
    BOOST_TEST(prompt.getRate() == 2);
    BOOST_TEST(prompt.getOffset() == 0);

    load_prompt_xml(prompt, "<prompt offset='50'/>");
    BOOST_TEST(prompt.getString() == "");
    BOOST_TEST(prompt.getRate() == 0);
    BOOST_TEST(prompt.getOffset() == 50);
}


// XML for a snow pillow sensor with complicated prompting.
const char* pillow_xml = R"XML(
<serialSensor ID="PILLOWS" class="DSMSerialSensor"
              baud="9600" parity="none" databits="8" stopbits="1"
              devicename="/dev/ttySPIUSB" id="1050" suffix="">

    <prompt rate="0.002"/>
    <sample id="18" scanfFormat="4%f%f%f">
        <variable name="SWE.p4" units="cm" longname="Snow Water Equivalent" plotrange="0 50"></variable>
        <variable name="Text.p4" units="degC" longname="External temperature" plotrange="$T_RANGE"></variable>
        <variable name="Tint.p4" units="degC" longname="Internal temperature" plotrange="$T_RANGE"></variable>
        <prompt string="4D0!" offset="55"/>
    </sample>

</serialSensor>
)XML";


BOOST_AUTO_TEST_CASE(prompt_inherits_rates)
{
    // make sure a sample prompt can inherit the rate of the sensor prompt.
    DSMSerialSensor dss;

    load_sensor_xml(dss, pillow_xml);
    BOOST_CHECK(dss.getPrompt() == Prompt("", 0.002));
    BOOST_TEST(! dss.getPrompt().valid());

    std::list<SampleTag*>& stags = dss.getSampleTags();
    BOOST_REQUIRE(stags.size() == 1);

    SampleTag& tag = *(stags.front());
    // the sample should have a rate which matches the sensor prompt, since it
    // wasn't set otherwise, and the sample prompt also inherits the rate.
    BOOST_TEST(tag.getPrompt() == Prompt("4D0!", 0.002, 55));
    BOOST_TEST(tag.getRate() == 0.002);

    // there should be two prompts associated with this sensor, even though
    // the first is invalid.
    BOOST_TEST(dss.getPrompts().size() == 2);
}


const char* mismatch_rate_xml = R"XML(
<serialSensor class="DSMSerialSensor"
              baud="9600" parity="none" databits="8" stopbits="1"
              devicename="/dev/ttySPIUSB" id="1050" suffix="">

    <sample id="18" scanfFormat="4%f%f%f" rate='20'>
        <variable name="SWE.p4" units="cm" longname="Snow Water Equivalent" plotrange="0 50"></variable>
        <variable name="Text.p4" units="degC" longname="External temperature" plotrange="$T_RANGE"></variable>
        <variable name="Tint.p4" units="degC" longname="Internal temperature" plotrange="$T_RANGE"></variable>
        <prompt rate='10' string="4D0!" offset="55"/>
    </sample>

</serialSensor>
)XML";


BOOST_AUTO_TEST_CASE(prompt_rate_mismatch)
{
    DSMSerialSensor dss;

    try {
        load_sensor_xml(dss, mismatch_rate_xml);
        BOOST_FAIL("exception not thrown for rate mismatch");
    }
    catch (InvalidParameterException& ipe)
    {
        BOOST_CHECK(true);
    }
}


BOOST_AUTO_TEST_CASE(prompt_prefix)
{
    Prompt prompt;

    std::string xml = "<prompt string='4D0!' rate='0.002' prefix='S1:'/>";
    load_prompt_xml(prompt, xml);
    BOOST_CHECK_EQUAL(prompt.getString(), "4D0!");
    BOOST_CHECK_EQUAL(prompt.getRate(), 0.002);
    BOOST_CHECK_EQUAL(prompt.getOffset(), 0);
    BOOST_TEST(prompt.getPrefix() == "S1:");
    BOOST_TEST(prompt.hasPrefix());
    BOOST_TEST(prompt.toXML() == xml);
    BOOST_CHECK(prompt.valid());

    prompt = Prompt();
    xml = "<prompt rate='0.002' prefix=''/>";
    load_prompt_xml(prompt, xml);
    BOOST_CHECK_EQUAL(prompt.getString(), "");
    BOOST_CHECK_EQUAL(prompt.getRate(), 0.002);
    BOOST_TEST(prompt.getPrefix() == "");
    BOOST_TEST(prompt.hasPrefix());
    BOOST_TEST(prompt.toXML() == xml);
    BOOST_CHECK(prompt.valid());
}


BOOST_AUTO_TEST_CASE(load_snow_pillow_xml)
{
    std::string xmlpath = "snow_pillow_sensor.xml";
    Project project;
    project.parseXMLConfigFile(xmlpath);
    project.initSensors();
    DSMSensor* ds = project.findSensor(SET_DSM_ID(SET_SPS_ID(0, 1052), 40));
    DSMSerialSensor* dss = dynamic_cast<DSMSerialSensor*>(ds);
    BOOST_REQUIRE(dss);


}


// XML for a snow pillow sensor with multiple prompts outside of samples.
const char* multi_sensor_prompts_xml = R"XML(
<serialSensor ID="PILLOWS" class="DSMSerialSensor"
              baud="9600" parity="none" databits="8" stopbits="1"
              devicename="/dev/ttySPIUSB" id="1050" suffix="">

    <prompt rate='0.05'/>
    <prompt string="1M1!\n" offset="0" prefix="LS1:"/>
    <sample id="2" scanfFormat="LM1:1%f%f%f%f">
        <prompt string="1D0!\n" offset="3" prefix="LM1:"/>
        <variable name="Load.1.p1" units="kg" longname="Load cell 1" plotrange="0 500"></variable>
        <variable name="Load.2.p1" units="kg" longname="Load cell 2" plotrange="0 500"></variable>
        <variable name="Load.3.p1" units="kg" longname="Load cell 3" plotrange="0 500"></variable>
        <variable name="Load.4.p1" units="kg" longname="Load cell 4" plotrange="0 500"></variable>
    </sample>
</serialSensor>
)XML";


BOOST_AUTO_TEST_CASE(multi_sensor_prompts)
{
    // make sure a sample prompt can inherit the rate of the sensor prompt.
    DSMSerialSensor dss;
    BOOST_TEST(dss.isPrompted() == false);
    BOOST_TEST(dss.getPrompts().size() == 1);
    BOOST_TEST(dss.getPrompt() == Prompt());

    load_sensor_xml(dss, multi_sensor_prompts_xml);
    BOOST_TEST(dss.getPrompt() == Prompt("", 0.05));
    BOOST_TEST(! dss.getPrompt().valid());
    BOOST_TEST(dss.isPrompted());

    // there should be three prompts associated with this sensor, even though
    // the first is invalid and only serves to set the rate for the rest.
    const std::list<Prompt>& prompts = dss.getPrompts();
    BOOST_TEST(prompts.size() == 3);
    auto it = prompts.begin();
    BOOST_TEST(*it == Prompt("", 0.05));
    BOOST_TEST(*(++it) == Prompt("1M1!\\n", 0.05, 0).setPrefix("LS1:"));
    BOOST_TEST(*(++it) == Prompt("1D0!\\n", 0.05, 3).setPrefix("LM1:"));

    std::list<SampleTag*>& stags = dss.getSampleTags();
    BOOST_REQUIRE(stags.size() == 1);

    SampleTag& tag = *(stags.front());
    // the sample should have a rate which matches the sensor prompt, since it
    // wasn't set otherwise, and the sample prompt also inherits the rate.
    BOOST_TEST(tag.getPrompt() == Prompt("1D0!\\n", 0.05, 3).setPrefix("LM1:"));
    BOOST_TEST(tag.getRate() == 0.05);

    // make sure auto range iterator can be used to modify each prompt
    for (auto& pi : const_cast<std::list<Prompt>&>(dss.getPrompts()))
    {
        pi.setOffset(pi.getOffset() + 5);
    }
    it = prompts.begin();
    BOOST_TEST(it->getOffset() == 5);
    BOOST_TEST((++it)->getOffset() == 5);
    BOOST_TEST((++it)->getOffset() == 8);
}


BOOST_AUTO_TEST_CASE(sensor_prefix)
{
    DSMSerialSensor dss;
    BOOST_TEST(dss.getPrefix().size() == 0);
    dss.setPrefix("42:");
    BOOST_TEST(dss.getPrefix() == "42:");

    // test all kinds of combinations of lengths and throw in some embedded
    // null bytes for good measure.
    std::string data(512, 'x');
    for (unsigned int c = 0; c < data.size(); ++c)
    {
        data[c] = (c % 10) ? c % 256 : 0;
    }

    for (unsigned int i = 0; i < data.size(); ++i)
    {
        dss.setPrefix(data.substr(0, i));
        for (unsigned int j = 1; j < data.size(); ++j)
        {
            Sample* sample = getSample<char>(j);
            memcpy(sample->getVoidDataPtr(), data.data(), j);
            sample = dss.prefixSample(sample);
            BOOST_TEST(sample->getDataLength() == i + j);
            BOOST_TEST(memcmp(sample->getVoidDataPtr(), data.data(), i) == 0);
            BOOST_TEST(memcmp(((char*)sample->getVoidDataPtr())+i,
                              data.data(), j) == 0);
            sample->freeReference();
        }
    }
}
