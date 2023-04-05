// -*- mode: C++; c-basic-offset: 4 -*-

#define BOOST_TEST_DYN_LINK
//#define BOOST_AUTO_TEST_MAIN
//#define BOOST_TEST_MODULE SerialSensorAutoConfig
#include <boost/test/unit_test.hpp>

// handle older boost libs...
// #if !defined( BOOST_TEST )
//     #define BOOST_TEST( P ) BOOST_CHECK( P )
// #endif
// #if !defined( BOOT_TEST_REQUIRE )
//     #define BOOST_TEST_REQUIRE( P ) BOOST_REQUIRE( P )
// #endif

using boost::unit_test_framework::test_suite;

#include "nidas/core/Project.h"
#include <nidas/core/DSMConfig.h>
#include "nidas/dynld/DSMSerialSensor.h"
#include "MockSerialSensor.h"
#include "nidas/util/SerialPort.h"
#include "nidas/core/NidasApp.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

#define UNIT_TEST_DEBUG_LOG 0

using namespace nidas::util;
using namespace nidas::core;
using namespace nidas::dynld;
using namespace nidas::dynld::isff;

namespace nidas { namespace util {
extern bool autoConfigUnitTest;
}}

class AutoProject
{
public:
    AutoProject() { Project::getInstance(); }
    ~AutoProject() { Project::destroyInstance(); }
    Project& operator()() {return *Project::getInstance();}
};

PortConfig _defaultPortConfig(19200, 7, Parity::EVEN, 1, RS422, TERM_120_OHM, -1);
PortConfig defaultPortConfig(_defaultPortConfig);
PortConfig _deviceOperatingPortConfig(38400, 8, Parity::NONE, 1, RS232, NO_TERM, 0);
PortConfig deviceOperatingPortConfig(_deviceOperatingPortConfig);

void resetPortConfigs()
{
    defaultPortConfig = _defaultPortConfig;
    deviceOperatingPortConfig = _deviceOperatingPortConfig;
}

void cleanup(int /*signal*/)
{
    if (errno != 0)
        perror("Socat child process died and became zombified!");
    else
        perror("Waiting for socat child process to exit...");

    while (waitpid(-1, (int*)0, WNOHANG) > 0) {}
}

struct Fixture {
    Fixture()
    {
        // Needs to be set up same as SerialSensor::SerialSensor() ctors
        // in order for mocked checkResponse() to work.
        _defaultPortConfig.termios.setRaw(true);
        _defaultPortConfig.termios.setRawLength(1);
        _defaultPortConfig.termios.setRawTimeout(0);
        _deviceOperatingPortConfig.termios.setRaw(true);
        _deviceOperatingPortConfig.termios.setRawLength(1);
        _deviceOperatingPortConfig.termios.setRawTimeout(0);
    }

    ~Fixture() {}
};

bool init_unit_test()
{
    autoConfigUnitTest = true;
    int fd = nidas::util::SerialPort::createPtyLink("/tmp/ttyDSM0");
    if (fd < 0) {
        std::cerr << "Failed to create a Pty for use in autoconfig unit tests." << std::endl;
        return false;
    }

    return true;
}

// entry point:
int main(int argc, char* argv[])
{
    nidas::core::NidasApp napp(argv[0]);
    napp.enableArguments(napp.loggingArgs());
    napp.allowUnrecognized(true);
    ArgVector args = napp.parseArgs(argc, argv);
    DLOG(("main entered, args parsed, debugging enabled..."));
    int retval = boost::unit_test::unit_test_main( &init_unit_test, argc, argv );
    return retval;
}


BOOST_FIXTURE_TEST_SUITE(autoconfig_test_suite, Fixture)

BOOST_AUTO_TEST_CASE(test_serialsensor_ctors)
{
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("Testing SerialSensor Constructors...");

    resetPortConfigs();

    BOOST_TEST_MESSAGE("    Testing SerialSensor Default Constructor...");
    SerialSensor ss;

    BOOST_TEST(ss.getName() == std::string("unknown:"));
    BOOST_TEST(ss.supportsAutoConfig() == false);
    BOOST_TEST(ss.getAutoConfigState() == AUTOCONFIG_UNSUPPORTED);
    BOOST_TEST(ss.getSerialConfigState() == AUTOCONFIG_UNSUPPORTED);
    BOOST_TEST(ss.getScienceConfigState() == AUTOCONFIG_UNSUPPORTED);
    // SerialSensor has no port configs when constructed.
    PortConfig pc = ss.getFirstPortConfig();
    BOOST_TEST(pc == PortConfig());
    BOOST_TEST(ss.getPortConfigs().size() == 0);
    BOOST_TEST(pc.termios.getBaudRate() == 9600);
    BOOST_TEST(pc.termios.getDataBits() == 8);
    BOOST_TEST(pc.termios.getParity() == Parity::NONE);
    BOOST_TEST(pc.termios.getStopBits() == 1);
    BOOST_TEST(pc.port_type == RS232);
    BOOST_TEST(pc.port_term == NO_TERM);

    ss.addPortConfig(defaultPortConfig);
    BOOST_TEST(ss.getFirstPortConfig() == defaultPortConfig);

    BOOST_TEST_MESSAGE("    Testing AutoConfig MockSerialSensor Constructor which passes default PortConfig arg to SerialSensor...");
    MockSerialSensor mss;

    BOOST_TEST(mss.supportsAutoConfig() == true);
    BOOST_TEST(mss.getName() == std::string("unknown:"));
    BOOST_TEST(mss.getAutoConfigState() == WAITING_IDLE);
    BOOST_TEST(mss.getSerialConfigState() == WAITING_IDLE);
    BOOST_TEST(mss.getScienceConfigState() == WAITING_IDLE);
    pc = mss.getFirstPortConfig();
    BOOST_TEST(pc.termios.getBaudRate() == 19200);
    BOOST_TEST(pc.termios.getDataBits() == 7);
    BOOST_TEST(pc.termios.getParity() == Parity::EVEN);
    BOOST_TEST(pc.termios.getStopBits() == 1);
    BOOST_TEST(pc.port_type == RS485_FULL);
    BOOST_TEST(pc.port_term == TERM_120_OHM);
    BOOST_TEST(pc.rts485 == -1);
}

BOOST_AUTO_TEST_CASE(test_serialsensor_findWorkingSerialPortConfig)
{
    BOOST_TEST_MESSAGE("Testing SerialSensor::findWorkingSerialPortConfig()...");

    resetPortConfigs();

    BOOST_TEST_MESSAGE("    Default port config different from sensor operating config.");
    BOOST_TEST_MESSAGE("        causes sweepCommParameters() to be invoked.");

    MockSerialSensor mss;

    mss.setDeviceName("/tmp/ttyDSM0");
    mss.open(O_RDWR);
    // on open, the first specified port becomes the active one.
    BOOST_TEST(mss.getPortConfig() == defaultPortConfig);

    BOOST_TEST_MESSAGE("    Default port config same as sensor operating config.");

    defaultPortConfig = deviceOperatingPortConfig;

    MockSerialSensor mss2;
    mss2.setDeviceName("/tmp/ttyDSM0");
    mss2.open(O_RDWR);
    BOOST_TEST(mss2.getFirstPortConfig() == deviceOperatingPortConfig);

    BOOST_TEST_MESSAGE("    Sensor operating config not in allowed port configs.");
    BOOST_TEST_MESSAGE("        causes sweepCommParameters() to be invoked.");
    MockSerialSensor mss3;
    mss3.setDeviceName("/tmp/ttyDSM0");
    deviceOperatingPortConfig.termios.setBaudRate(115200);
    mss3.open(O_RDWR);
    BOOST_TEST(mss3.getPortConfig() != deviceOperatingPortConfig);
}

BOOST_AUTO_TEST_CASE(test_serialsensor_fromDOMElement)
{
    BOOST_TEST_MESSAGE("");
    BOOST_TEST_MESSAGE("Testing SerialSensor::fromDOMElement()...");

    struct stat statbuf;
    std::string xmlFileName = "";

    resetPortConfigs();

    {
        AutoProject ap;

        BOOST_TEST_MESSAGE("    Testing Legacy DSMSerialSensor Dynld Implementation");
        xmlFileName = "legacy_autoconfig.xml";
        BOOST_TEST_REQUIRE(::stat(xmlFileName.c_str(),&statbuf) == 0);

        ap().parseXMLConfigFile(xmlFileName);
        DSMConfigIterator di = ap().getDSMConfigIterator();
        BOOST_REQUIRE(di.hasNext() == true);
        DSMConfig* pDsm = const_cast<DSMConfig*>(di.next());
        (*pDsm).validate();

        SensorIterator sensIter = ap().getSensorIterator();
        BOOST_REQUIRE(sensIter.hasNext() == true );
        SerialSensor* pSerialSensor = dynamic_cast<SerialSensor*>(sensIter.next());
        BOOST_REQUIRE(pSerialSensor != 0);
        // validate that the sensor really is a legacy DSMSerialSensor...
        DSMSerialSensor* pDSMSerialSensor = dynamic_cast<DSMSerialSensor*>(pSerialSensor);
        BOOST_TEST(pDSMSerialSensor != static_cast<DSMSerialSensor*>(0));
        pSerialSensor->init();
        // Don't open/close because the pty being mocked can't handle termios
        // details, but at least test as much as creating the io device with
        // the right port config.
        pSerialSensor->setIODevice(pSerialSensor->buildIODevice());
        auto* ios = dynamic_cast<SerialPortIODevice*>(pSerialSensor->getIODevice());
        BOOST_TEST(ios);
        PortConfig pc = pSerialSensor->getPortConfig();
        BOOST_TEST(ios->getPortConfig() == pc);
        BOOST_TEST(pc.termios.getBaudRate() == 9600);
        BOOST_TEST(pc.termios.getParity() == Parity::EVEN);
        BOOST_TEST(pc.termios.getDataBits() == 7);
        BOOST_TEST(pc.termios.getStopBits() ==1);
        BOOST_TEST(pSerialSensor->getInitString() == std::string("DSMSensorTestInitString"));
    }

    {
        AutoProject ap;

        BOOST_TEST_MESSAGE("    Testing MockSerialSensor - No <serialSensor> tag attributes...");
        xmlFileName = "autoconfig0.xml";
        BOOST_TEST_REQUIRE(::stat(xmlFileName.c_str(),&statbuf) == 0);

        ap().parseXMLConfigFile(xmlFileName);
        DSMConfigIterator di = ap().getDSMConfigIterator();
        BOOST_REQUIRE(di.hasNext() == true);
        DSMConfig* pDsm = const_cast<DSMConfig*>(di.next());
        (*pDsm).validate();

        SensorIterator sensIter = ap().getSensorIterator();
        BOOST_REQUIRE(sensIter.hasNext() == true );
        MockSerialSensor* pMockSerialSensor = dynamic_cast<MockSerialSensor*>(sensIter.next());
        BOOST_TEST(pMockSerialSensor != static_cast<MockSerialSensor*>(0));
        pMockSerialSensor->init();
        // Don't open/close because the pty being for mock can't handle termios details

        // This sensor has no port config in the xml, so the one activated should be the
        // first hardcoded option, ie, _defaultPortConfig.
        pMockSerialSensor->setIODevice(pMockSerialSensor->buildIODevice());
        PortConfig pc = pMockSerialSensor->getPortConfig();
        BOOST_TEST(pc.termios.getBaudRate() == 19200);
        BOOST_TEST(pc.termios.getParity() == Parity::EVEN);
        BOOST_TEST(pc.termios.getDataBits() == 7);
        BOOST_TEST(pc.termios.getStopBits() == 1);
        BOOST_TEST(pc.port_term == TERM_120_OHM);
        BOOST_TEST(pc.port_type == RS422);
        BOOST_TEST(pc.rts485 == -1);
        BOOST_TEST(pMockSerialSensor->getInitString() == std::string("MockSensor0TestInitString"));
    }

    {
        AutoProject ap;

        BOOST_TEST_MESSAGE("    Testing MockSerialSensor - Use <serialSensor> tag attributes");
        xmlFileName = "autoconfig1.xml";
        BOOST_TEST_REQUIRE(::stat(xmlFileName.c_str(),&statbuf) == 0);

        ap().parseXMLConfigFile(xmlFileName);
        DSMConfigIterator di = ap().getDSMConfigIterator();
        BOOST_REQUIRE(di.hasNext() == true);
        DSMConfig* pDsm = const_cast<DSMConfig*>(di.next());
        (*pDsm).validate();

        SensorIterator sensIter = ap().getSensorIterator();
        BOOST_REQUIRE(sensIter.hasNext() == true );
        MockSerialSensor* pMockSerialSensor = dynamic_cast<MockSerialSensor*>(sensIter.next());
        BOOST_TEST(pMockSerialSensor != static_cast<MockSerialSensor*>(0));
        pMockSerialSensor->init();
        // Don't open/close because the pty being for mock can't handle termios details
        pMockSerialSensor->setIODevice(pMockSerialSensor->buildIODevice());
        PortConfig pc = pMockSerialSensor->getPortConfig();
        BOOST_TEST(pc.termios.getBaudRate() == 9600);
        BOOST_TEST(pc.termios.getParity() == Parity::EVEN);
        BOOST_TEST(pc.termios.getDataBits() == 7);
        BOOST_TEST(pc.termios.getStopBits() == 1);
        BOOST_TEST(pc.port_term == TERM_120_OHM);
        BOOST_TEST(pc.port_type == RS232);
        BOOST_TEST(pc.rts485 == 1);
        BOOST_TEST(pMockSerialSensor->getInitString() == std::string("MockSensor1TestInitString"));
    }

    {
        AutoProject ap;

        BOOST_TEST_MESSAGE("    Testing MockSerialSensor - Use <autoconfig> tag attributes");
        xmlFileName = "autoconfig2.xml";
        BOOST_TEST_REQUIRE(::stat(xmlFileName.c_str(),&statbuf) == 0);

        ap().parseXMLConfigFile(xmlFileName);
        DSMConfigIterator di = ap().getDSMConfigIterator();
        BOOST_REQUIRE(di.hasNext() == true);
        DSMConfig* pDsm = const_cast<DSMConfig*>(di.next());
        (*pDsm).validate();

        SensorIterator sensIter = ap().getSensorIterator();
        BOOST_REQUIRE(sensIter.hasNext() == true );
        MockSerialSensor* pMockSerialSensor = dynamic_cast<MockSerialSensor*>(sensIter.next());
        BOOST_REQUIRE(pMockSerialSensor);

        for (auto pc : pMockSerialSensor->getPortConfigs())
        {
            std::cerr << pc << std::endl;
        }

        // at this point the active port config has not changed, and the first port config is
        // from the autoconfig element:
        BOOST_TEST(pMockSerialSensor->getPortConfig() == PortConfig());
        BOOST_TEST(pMockSerialSensor->getFirstPortConfig() == PortConfig(19200, 8, Parity::O, 2, RS232));

        pMockSerialSensor->init();
        pMockSerialSensor->setIODevice(pMockSerialSensor->buildIODevice());
        PortConfig pc = pMockSerialSensor->getPortConfig();
        // this sensor has no port config in the sensor element, only in the
        // autoconfig element, so those are the settings that should be
        // activated.
        BOOST_TEST(pc.termios.getBaudRate() == 19200);
        BOOST_TEST(pc.termios.getParity() == Parity::ODD);
        BOOST_TEST(pc.termios.getDataBits() == 8);
        BOOST_TEST(pc.termios.getStopBits() == 2);
        BOOST_TEST(pMockSerialSensor->getInitString() == std::string("MockSensor2TestInitString"));
    }
}

BOOST_AUTO_TEST_SUITE_END()

