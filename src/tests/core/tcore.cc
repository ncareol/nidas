#define BOOST_TEST_DYN_LINK
#define BOOST_AUTO_TEST_MAIN
#include <boost/test/unit_test.hpp>
using boost::unit_test_framework::test_suite;

#include <boost/regex.hpp>

#include <nidas/core/NidasApp.h>

#include <sys/types.h>
#include <signal.h>

using namespace nidas::util;
using namespace nidas::core;


class SampleId
{
public:
  SampleId(int dsm, int sid) :
    _id(0)
  {
    _id = SET_DSM_ID(_id, dsm);
    _id = SET_SHORT_ID(_id, sid);
  }

  operator dsm_sample_id_t()
  {
    return _id;
  }

private:
  dsm_sample_id_t _id;
};


template< typename T, size_t N >
std::vector<T>
array_vector( const T (&data)[N] )
{
  return std::vector<T>(data, data+N);
}


template< size_t N >
std::vector<std::string>
array_vector( const char* (&data)[N] )
{
  return std::vector<std::string>(data, data+N);
}

template<typename T>
std::vector<T>
make_vector_t(const T& a1=T(),
	      const T& a2=T(),
	      const T& a3=T(),
	      const T& a4=T(),
	      const T& a5=T())
{
  std::vector<T> args;
  if (a1 != T())
    args.push_back(a1);
  if (a2 != T())
    args.push_back(a2);
  if (a3 != T())
    args.push_back(a3);
  if (a4 != T())
    args.push_back(a4);
  if (a5 != T())
    args.push_back(a5);
  return args;
}

std::vector<std::string>
make_vector(const std::string& a1="", const std::string& a2="",
	    const std::string& a3="", const std::string& a4="",
	    const std::string& a5="")
{
  return make_vector_t<std::string>(a1, a2, a3, a4, a5);
}

template< typename V >
typename V::iterator
vfind(V& v, typename V::value_type target)
{
  return std::find(v.begin(), v.end(), target);
}


BOOST_AUTO_TEST_CASE(test_sample_match_all)
{
  SampleMatcher sm;

  BOOST_REQUIRE(sm.addCriteria("-1,*"));
  for (int dsm = 1; dsm < 5; ++dsm)
  {
    for (int sid = 1; sid < 10; ++sid)
    {
      BOOST_TEST(sm.match(SampleId(dsm, sid)));
    }
  }

  sm = SampleMatcher();
  // match everything, same as *,* or -1,-1
  BOOST_REQUIRE(sm.addCriteria("/"));
  BOOST_TEST(sm.match(SampleId(1, 1)));
}


// Make sure a null matcher matches all samples.
BOOST_AUTO_TEST_CASE(test_sample_match_null)
{
  SampleMatcher sm;
  for (int dsm = 1; dsm < 5; ++dsm)
  {
    for (int sid = 1; sid < 10; ++sid)
    {
      BOOST_CHECK_EQUAL(sm.match(SampleId(dsm, sid)), true);
    }
  }
}


BOOST_AUTO_TEST_CASE(test_sample_match_ranges)
{
  SampleMatcher sm;
  BOOST_REQUIRE(sm.addCriteria("10-20,1-5"));
  BOOST_REQUIRE(sm.addCriteria("5-8,1-5"));

  for (int dsm = 1; dsm < 30; ++dsm)
  {
    for (int sid = 1; sid < 10; ++sid)
    {
      bool match = (((10 <= dsm && dsm <= 20) || (5 <= dsm && dsm <= 8)) &&
		    (1 <= sid) && (sid <= 5));
      BOOST_CHECK_EQUAL(sm.match(SampleId(dsm, sid)), match);
    }
  }
}

BOOST_AUTO_TEST_CASE(test_sample_match_exclusive)
{
  SampleMatcher sm;
  BOOST_CHECK_EQUAL(sm.exclusiveMatch(), false);
  BOOST_REQUIRE(sm.addCriteria("2,3"));
  BOOST_CHECK_EQUAL(sm.exclusiveMatch(), true);
  BOOST_REQUIRE(sm.addCriteria("2,4"));
  BOOST_CHECK_EQUAL(sm.exclusiveMatch(), false);
}


BOOST_AUTO_TEST_CASE(test_sample_match_fail)
{
  SampleMatcher sm;

  BOOST_CHECK_EQUAL(sm.addCriteria("1,x1"), false);
  BOOST_CHECK_EQUAL(sm.addCriteria(",1"), false);
  BOOST_CHECK_EQUAL(sm.addCriteria("!,1"), false);
  BOOST_CHECK_EQUAL(sm.addCriteria("-,1"), false);
  BOOST_CHECK_EQUAL(sm.addCriteria("1,-"), false);
  BOOST_CHECK_EQUAL(sm.addCriteria("1:1"), false);
  BOOST_CHECK_EQUAL(sm.addCriteria("^1:1"), false);
  BOOST_CHECK_EQUAL(sm.addCriteria("^1,1^"), false);
  BOOST_CHECK_EQUAL(sm.numRanges(), 0);
}


BOOST_AUTO_TEST_CASE(test_sample_match_one)
{
  SampleMatcher sm;

  // Make sure a single match works, and the returns the same result more
  // than once.
  BOOST_REQUIRE(sm.addCriteria("1,1"));
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 1)), true);
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 2)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 2)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 1)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 2)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 2)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 1)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 1)), true);
}


BOOST_AUTO_TEST_CASE(test_sample_match_first)
{
  SampleMatcher sm;

  // not allowed to match . in sample id
  BOOST_TEST(!sm.addCriteria("1,."), ". not allowed in sid");
  BOOST_TEST(!sm.addCriteria("-2,1"), "-1 only negative allowed in spec");
  BOOST_TEST(!sm.addCriteria("1,-2"), "-1 only negative allowed in spec");
  BOOST_TEST(sm.addCriteria("."));

  // after setting first dsm on match, that's the only one that matches.
  BOOST_TEST(sm.match(SampleId(10, 20)));
  BOOST_TEST(sm.match(SampleId(10, 21)));
  BOOST_TEST(sm.match(SampleId(10, 22)));
  BOOST_TEST(!sm.match(SampleId(1, 20)));
  BOOST_TEST(!sm.match(SampleId(2, 20)));
  BOOST_TEST(!sm.match(SampleId(3, 20)));
}


BOOST_AUTO_TEST_CASE(test_sample_match_excluded)
{
  SampleMatcher sm;

  BOOST_REQUIRE(sm.addCriteria("^1,1"));
  BOOST_REQUIRE(sm.addCriteria("-1,-1"));
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 1)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 2)), true);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 2)), true);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 1)), true);
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 2)), true);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 2)), true);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 1)), true);
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 1)), false);
}


BOOST_AUTO_TEST_CASE(test_match_cache_invalid)
{
  SampleMatcher sm;

  // The cache should be flushed if the criteria change.
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 1)), true);
  BOOST_REQUIRE(sm.addCriteria("^1,1"));
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 1)), false);
  BOOST_REQUIRE(sm.addCriteria("2,3"));
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 3)), true);
}


BOOST_AUTO_TEST_CASE(test_match_implicit_include)
{
  SampleMatcher sm;

  // Samples are accepted if they are not explicitly excluded.
  BOOST_REQUIRE(sm.addCriteria("^1,1"));
  BOOST_REQUIRE(sm.addCriteria("^2,3"));
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 1)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(2, 3)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(1, 1)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(3, 3)), true);
  BOOST_CHECK_EQUAL(sm.match(SampleId(4, 5)), true);
  // But as soon as one inclusion is added they are excluded.
  BOOST_REQUIRE(sm.addCriteria("9,9"));
  BOOST_CHECK_EQUAL(sm.match(SampleId(3, 3)), false);
  BOOST_CHECK_EQUAL(sm.match(SampleId(4, 5)), false);
}


BOOST_AUTO_TEST_CASE(test_log_level_parse)
{
  // These tests assume the logging scheme is at its initial state and with
  // no configs, so reset it each time.  Otherwise they would accumulate.
  // Maybe there needs to be a method which explicitly sets the config,
  // meaning any existing configs are first cleared.
  NidasApp app("test");
  app.resetLogging();

  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_INFO);

  app.parseLogLevel("debug");
  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_DEBUG);

  app.resetLogging();
  app.parseLogLevel("7");
  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_DEBUG);

  app.resetLogging();
  app.parseLogLevel("5");
  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_NOTICE);

  app.resetLogging();
  app.parseLogLevel("error");
  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_ERROR);

  // Emergency can be set as 0 or as a string.
  app.resetLogging();
  app.parseLogLevel("0");
  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_EMERG);

  app.resetLogging();
  app.parseLogLevel("verbose");
  BOOST_CHECK_EQUAL(app.logLevel(), 8);

  app.resetLogging();
  app.parseLogLevel("emergency");
  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_EMERG);

  // These should throw exceptions without changing level.
  int last = app.logLevel();
  BOOST_CHECK_THROW(app.parseLogLevel("x"), NidasAppException);
  BOOST_CHECK_EQUAL(app.logLevel(), last);
  BOOST_CHECK_THROW(app.parseLogLevel("9"), NidasAppException);
  BOOST_CHECK_EQUAL(app.logLevel(), last);
  BOOST_CHECK_THROW(app.parseLogLevel("-1"), NidasAppException);
  BOOST_CHECK_EQUAL(app.logLevel(), last);
  BOOST_CHECK_THROW(app.parseLogLevel("info2"), NidasAppException);
  BOOST_CHECK_EQUAL(app.logLevel(), last);
  BOOST_CHECK_THROW(app.parseLogLevel("idebug"), NidasAppException);
  BOOST_CHECK_EQUAL(app.logLevel(), last);
  // function, not fn
  BOOST_CHECK_THROW(app.parseLogLevel("fn=x"), NidasAppException);
  // file, not filename
  BOOST_CHECK_THROW(app.parseLogLevel("filename=x"), NidasAppException);
}


BOOST_AUTO_TEST_CASE(test_nidas_app_args)
{
  NidasApp app("test");

  app.parseOutput("file");
  BOOST_CHECK_EQUAL(app.outputFileName(), "file");
  BOOST_CHECK_EQUAL(app.outputFileLength(), 0);

  app.parseOutput("file-%Y@12h");
  BOOST_CHECK_EQUAL(app.outputFileName(), "file-%Y");
  BOOST_CHECK_EQUAL(app.outputFileLength(), 12*3600);

  app.parseOutput("file-%Y%m%d@30m");
  BOOST_CHECK_EQUAL(app.outputFileName(), "file-%Y%m%d");
  BOOST_CHECK_EQUAL(app.outputFileLength(), 30*60);

  app.parseOutput("file-%Y%m%d@300");
  BOOST_CHECK_EQUAL(app.outputFileName(), "file-%Y%m%d");
  BOOST_CHECK_EQUAL(app.outputFileLength(), 300);

  BOOST_CHECK_THROW(app.parseOutput("file-%Y%m%d@"), NidasAppException);
}


BOOST_AUTO_TEST_CASE(test_nidas_app_output)
{
  NidasApp app("test");
  app.resetLogging();

  BOOST_CHECK_EQUAL(app.getName(), "test");

  app.enableArguments(app.XmlHeaderFile);
  app.enableArguments(app.LogConfig | app.ProcessData);

  const char* argv[] = { "-x", "xmlfile", "-p", "-l", "debug" };
  //int argc = sizeof(argv)/sizeof(argv[0]);
  //std::vector<std::string> args(argv, argv+argc);
  std::vector<std::string> args = array_vector(argv);

  BOOST_CHECK_EQUAL(app.processData(), false);
  args = app.parseArgs(args);
  BOOST_CHECK_EQUAL(app.xmlHeaderFile(), "xmlfile");
  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_DEBUG);
  BOOST_CHECK_EQUAL(args.empty(), true);
  BOOST_CHECK_EQUAL(app.processData(), true);
}

// Make sure long log args are accepted.
BOOST_AUTO_TEST_CASE(test_nidas_app_long_args)
{
  std::vector<std::string> args;
  {
    NidasApp app("test");
    app.enableArguments(app.LogConfig);
    const char* argv[] = { "--log", "debug" };
    args = array_vector(argv);
    app.resetLogging();
    args = app.parseArgs(args);
    BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_DEBUG);
    BOOST_CHECK_EQUAL(args.empty(), true);
  }
  {
    // Make sure deprecated options still accepted but not in usage.
    NidasApp app("test");
    BOOST_CHECK_EQUAL(app.LogConfig.accept("-l"), true);
    BOOST_CHECK_EQUAL(app.LogConfig.accept("--log"), true);
    BOOST_CHECK_EQUAL(app.LogConfig.accept("--logconfig"), true);
    BOOST_CHECK_EQUAL(app.LogConfig.accept("--loglevel"), true);
  }
  {
    NidasApp app("test");
    app.enableArguments(app.LogConfig);
    const char* argv[] = { "--loglevel", "debug" };
    args = array_vector(argv);
    app.resetLogging();
    args = app.parseArgs(args);
    BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_DEBUG);
    BOOST_CHECK_EQUAL(args.empty(), true);
  }
}

BOOST_AUTO_TEST_CASE(test_nidas_app_neg_args)
{
  NidasAppArg Long("--long", "", "Go long.", "false");
  BOOST_CHECK_EQUAL(Long.getUsageFlags(), "--[no-]long");
  BOOST_CHECK_EQUAL(Long.asBool(), false);
  Long.parse(make_vector("--no-long"));
  BOOST_CHECK_EQUAL(Long.asBool(), false);
  BOOST_CHECK_EQUAL(Long.asBool(), false);
  Long.parse(make_vector("--long"));
  BOOST_CHECK_EQUAL(Long.asBool(), true);

  // Test default of true.
  NidasAppArg Short("--short", "", "Go short.", "true");
  BOOST_CHECK_EQUAL(Short.asBool(), true);
  Short.parse(make_vector("--short"));
  BOOST_CHECK_EQUAL(Short.asBool(), true);
  Short.parse(make_vector("--no-short"));
  BOOST_CHECK_EQUAL(Short.asBool(), false);
}


// Make sure extra args are left in argument list.
BOOST_AUTO_TEST_CASE(test_nidas_app_xargs)
{
  NidasApp app("test");
  app.resetLogging();

  const char* argv[] = { "-x", "xmlfile", "-m", "myargument", "-l", "debug" };
  int argc = sizeof(argv)/sizeof(argv[0]);
  std::vector<std::string> args(argv, argv+argc);

  app.allowUnrecognized(true);
  app.enableArguments(app.XmlHeaderFile | app.LogConfig);
  args = app.parseArgs(args);
  BOOST_CHECK_EQUAL(app.xmlHeaderFile(), "xmlfile");
  BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_DEBUG);
  BOOST_REQUIRE_EQUAL(args.size(), 2);
  BOOST_CHECK_EQUAL(args[0], "-m");
  BOOST_CHECK_EQUAL(args[1], "myargument");
}

namespace
{
  int iflag = 0;

  void setflag(int)
  {
    iflag = 9;
  }
}


BOOST_AUTO_TEST_CASE(test_nidas_app_interrupt)
{
  NidasApp app("test");

  app.setupSignals(setflag);

  // In theory this is asynchronous and the signal might actually be
  // received after the check for it.  However, in practice this works,
  // perhaps because the kill() is a system call and so the process
  // immediately handles the signal while it is blocking on the system
  // call.
  BOOST_CHECK_EQUAL(app.interrupted(), false);
  kill(getpid(), SIGINT);
  BOOST_CHECK_EQUAL(app.interrupted(), true);
  BOOST_CHECK_EQUAL(iflag, 9);
}


BOOST_AUTO_TEST_CASE(test_nidas_app_setargs)
{
  // Make sure the set of allowed arguments can be configured.
  NidasApp app("test");

  app.InputFiles.allowFiles = true;
  app.InputFiles.allowSockets = true;
  app.InputFiles.setDefaultInput("sock:localhost", 30000);
  app.StartTime.addFlag("-B");
  app.EndTime.addFlag("-E");
  app.enableArguments(app.XmlHeaderFile | app.ProcessData |
		      app.InputFiles | app.StartTime | app.EndTime |
		      app.InputFiles | app.StartTime | app.EndTime |
		      app.SampleRanges | app.Help | app.Version);

  nidas_app_arglist_t appargs = app.getArguments();
  // LogShow and LogFields are always included.
  BOOST_CHECK_EQUAL(appargs.size(), 8);

  BOOST_CHECK(vfind(appargs, &app.InputFiles) != appargs.end());
  BOOST_CHECK(vfind(appargs, &app.OutputFiles) == appargs.end());
  BOOST_CHECK(vfind(appargs, &app.StartTime) != appargs.end());
  BOOST_CHECK(vfind(appargs, &app.EndTime) != appargs.end());
  BOOST_CHECK(vfind(appargs, &app.Help) != appargs.end());
  BOOST_CHECK(vfind(appargs, &app.InputFiles) != appargs.end());
  BOOST_CHECK(vfind(appargs, &app.SampleRanges) != appargs.end());
  BOOST_CHECK(vfind(appargs, &app.Version) != appargs.end());

  BOOST_CHECK_EQUAL(app.InputFiles.allowFiles, true);
  BOOST_CHECK_EQUAL(app.InputFiles.allowSockets, true);

  // Parse a simple command line.
  ArgVector args{"-x", "/tmp/header.xml"};
  args = app.parseArgs(args);
  BOOST_CHECK_EQUAL(args.size(), 0);
  BOOST_CHECK_EQUAL(app.xmlHeaderFile(), "/tmp/header.xml");

  // Now try parsing a line with a valid but disabled option, so it should
  // not be consumed when parsed.
  args = ArgVector{"-l", "debug", "-x", "/tmp/header2.xml"};

  app.allowUnrecognized(true);
  args = app.parseArgs(args);
  BOOST_REQUIRE_EQUAL(args.size(), 2);
  BOOST_CHECK_EQUAL(args[0], "-l");
  BOOST_CHECK_EQUAL(args[1], "debug");
  BOOST_CHECK_EQUAL(app.xmlHeaderFile(), "/tmp/header2.xml");
}


BOOST_AUTO_TEST_CASE(test_nidas_app_longargs)
{
  // Make sure long arguments are accepted.
  NidasApp app("test");
  app.allowUnrecognized(true);

  app.enableArguments(app.XmlHeaderFile | app.ProcessData | app.SampleRanges);
  app.requireLongFlag(app.XmlHeaderFile | app.ProcessData | app.SampleRanges);
  
  BOOST_CHECK(app.ProcessData.accept("--process"));

  // Short flags should not be allowed.
  ArgVector args{"-x", "/tmp/header.xml"};
  args = app.parseArgs(args);
  BOOST_CHECK_EQUAL(args.size(), 2);
  BOOST_CHECK_EQUAL(app.xmlHeaderFile(), "");

  args = ArgVector{"--xml", "/tmp/header.xml", "--process",
                   "--samples", "1,1"};
  args = app.parseArgs(args);
  std::copy(args.begin(), args.end(),
	    std::ostream_iterator<std::string>(std::cerr, "\n"));
  BOOST_CHECK_EQUAL(args.size(), 0);
  BOOST_CHECK_EQUAL(app.xmlHeaderFile(), "/tmp/header.xml");
  BOOST_CHECK_EQUAL(app.processData(), true);
}


BOOST_AUTO_TEST_CASE(test_nidas_app_requireargs)
{
  NidasApp app("test");
  ILOG(("Testing missing required args"));

  app.requireArguments(app.XmlHeaderFile | app.StartTime | app.EndTime);
  app.enableArguments(app.ProcessData | app.Help | app.Hostname);

  // Required arguments are implicitly enabled.
  nidas_app_arglist_t appargs = app.getArguments();
  nidas_app_arglist_t::const_iterator it = appargs.begin();
  for (it = appargs.begin(); it != appargs.end(); ++it)
  {
    DLOG(("") << (*it)->getUsageFlags());
  }
  BOOST_CHECK_EQUAL(app.getArguments().size(), 6);
  BOOST_CHECK(find(appargs.begin(), appargs.end(), &app.XmlHeaderFile) !=
	      appargs.end());
  BOOST_CHECK(find(appargs.begin(), appargs.end(), &app.Help) != appargs.end());
  BOOST_CHECK(find(appargs.begin(), appargs.end(), &app.EndTime) !=
	      appargs.end());
  BOOST_CHECK(find(appargs.begin(), appargs.end(), &app.Hostname) !=
	      appargs.end());

  // And the required ones are required...
  BOOST_CHECK_EQUAL(app.XmlHeaderFile.isRequired(), true);
  BOOST_CHECK_EQUAL(app.StartTime.isRequired(), true);
  BOOST_CHECK_EQUAL(app.EndTime.isRequired(), true);
  BOOST_CHECK_EQUAL(app.Help.isRequired(), false);
  BOOST_CHECK_EQUAL(app.Hostname.isRequired(), false);
  BOOST_CHECK_EQUAL(app.ProcessData.isRequired(), false);

  ArgVector args{"-h", "-p"};
  app.parseArgs(args);
  // An app would check for help before failing on missing arguments.
  BOOST_CHECK_EQUAL(app.helpRequested(), true);
  // But a further check for required arguments should throw.
  BOOST_CHECK_THROW(app.checkRequiredArguments(), NidasAppException);

  NidasApp app2("test2");
  ILOG(("Testing no missing required args"));

  app2.requireArguments(app2.XmlHeaderFile);
  app2.enableArguments(app2.ProcessData | app2.Help | app2.Hostname);

  ArgVector args2{"-x", "/tmp/some.xml", "-h", "-p"};
  BOOST_CHECK_NO_THROW(app2.parseArgs(args2));

  NidasApp app3("test3");
  ILOG(("Testing no required args specified"));

  app3.enableArguments(app3.XmlHeaderFile | app3.ProcessData |
		       app3.Help | app3.Hostname);

  ArgVector args3;
  BOOST_CHECK_NO_THROW(app2.parseArgs(args3));
}


BOOST_AUTO_TEST_CASE(test_nidas_app_badargs)
{
  // Make sure incomplete arguments throw exceptions.
  NidasApp app("test");

  app.allowUnrecognized(true);
  app.enableArguments(app.XmlHeaderFile);
  
  ArgVector args{"--xml"};
  BOOST_CHECK_THROW(app.parseArgs(args), NidasAppException);
  BOOST_CHECK_EQUAL(args.size(), 1);
  BOOST_CHECK_EQUAL(app.xmlHeaderFile(), "");

  args = ArgVector{"--unknown", "first", "--xml", "xmlfile", "last"};
  args = app.parseArgs(args);
  BOOST_CHECK_EQUAL(app.xmlHeaderFile(), "xmlfile");
  BOOST_CHECK_EQUAL(args.size(), 3);
  BOOST_CHECK_EQUAL(args[0], "--unknown");
  BOOST_CHECK_EQUAL(args[1], "first");
  BOOST_CHECK_EQUAL(args[2], "last");
}



BOOST_AUTO_TEST_CASE(test_nidas_app_argflags)
{
  NidasApp app("test");

  BOOST_CHECK_EQUAL(app.LogConfig.getUsageFlags(), "-l,--log");
  app.LogConfig.acceptShortFlag(false);
  BOOST_CHECK_EQUAL(app.LogConfig.getUsageFlags(), "--log");

  BOOST_CHECK_EQUAL(app.Help.getUsageFlags(), "-h,--help");
  app.Help.acceptShortFlag(false);
  BOOST_CHECK_EQUAL(app.Help.getUsageFlags(), "--help");

  BOOST_CHECK_EQUAL(app.LogShow.getUsageFlags(), "--logshow");
  app.LogShow.acceptShortFlag(false);
  BOOST_CHECK_EQUAL(app.LogShow.getUsageFlags(), "--logshow");
}



BOOST_AUTO_TEST_CASE(test_nidas_app_instance)
{
  {
    NidasApp app("test");

    app.setApplicationInstance();
    BOOST_CHECK_EQUAL(NidasApp::getApplicationInstance(), &app);
  }
  BOOST_CHECK(! NidasApp::getApplicationInstance());
}



BOOST_AUTO_TEST_CASE(test_nidas_app_process_name)
{
  const char* argv[] = { "/bin/test", "--period", "25", "--repeat" };
  int argc = 4;

  NidasApp app("test");
  app.allowUnrecognized(true);
  BOOST_CHECK_EQUAL(app.getName(), "test");
  BOOST_CHECK_EQUAL(app.getProcessName(), "test");
  ArgVector args = app.parseArgs(argc, argv);
  BOOST_CHECK_EQUAL(app.getProcessName(), "/bin/test");
  // no args should be consumed
  BOOST_CHECK_EQUAL(args.size(), 3);
  app.setProcessName("/usr/bin/test");
  BOOST_CHECK_EQUAL(app.getProcessName(), "/usr/bin/test");
}




class MyApp
{
public:
  MyApp() :
    app("myapp"),
    Repeat("--repeat", "", "Keep repeating."),
    Period("--period", "period", "Reporting period in seconds.", "20"),
    _repeat(false),
    _period(0)
  {
  }

public:

  NidasApp app;
  NidasAppArg Repeat;
  NidasAppArg Period;

  void
  parseArguments(const ArgVector& args)
  {
    app.enableArguments(Repeat | Period | app.LogConfig | app.Help |
			app.Version | app.XmlHeaderFile);
    app.startArgs(args);
    NidasAppArg* arg;
    while ((arg = app.parseNext()))
    {
      if (arg == &Repeat)
	_repeat = Repeat.asBool();
      else if (arg == &Period)
	_period = Period.asInt();
    }
  }

  bool _repeat;
  int _period;

};


BOOST_AUTO_TEST_CASE(test_nidas_app_argv)
{
  // Test usage with a standard argv list.
  const char* argv[] = { "/bin/myapp", "--period", "25", "--repeat" };
  int argc = 4;

  MyApp app;
  
  // Just make sure no arguments means no settings change.
  app.parseArguments(ArgVector(argv+1, argv+1));
  BOOST_CHECK_EQUAL(app._repeat, false);
  BOOST_CHECK_EQUAL(app._period, 0);

  app.parseArguments(ArgVector(argv+1, argv+argc));
  BOOST_CHECK_EQUAL(app._repeat, true);
  BOOST_CHECK_EQUAL(app._period, 25);
}


BOOST_AUTO_TEST_CASE(test_nidas_app_custom_args)
{
  MyApp app;
  
  BOOST_CHECK_EQUAL(app._repeat, false);
  BOOST_CHECK_EQUAL(app._period, 0);

  // Check for default.
  BOOST_CHECK_EQUAL(app.Period.asInt(), 20);

  ArgVector args{"--repeat", "--period", "10"};
  app.parseArguments(args);
  BOOST_CHECK_EQUAL(app._repeat, true);
  BOOST_CHECK_EQUAL(app._period, 10);
}


BOOST_AUTO_TEST_CASE(test_nidas_app_parse_int)
{
  // Make sure int parsing fails.
  MyApp app;
  
  ArgVector args{"--period", "10f"};
  app.Period.parse(args);
  BOOST_CHECK_THROW(app.Period.asInt(), NidasAppException);

  args = ArgVector{"--period", "1.0"};
  app.Period.parse(args);
  BOOST_CHECK_THROW(app.Period.asInt(), NidasAppException);
}


BOOST_AUTO_TEST_CASE(test_nidas_app_parse_float)
{
  // Test float parsing.
  MyApp app;
  
  ArgVector args{"--period", "1.25"};

  app.Period.parse(args);
  BOOST_CHECK_EQUAL(app.Period.asFloat(), 1.25);

  args = ArgVector{"--period", "1.2x345"};
  app.Period.parse(args);
  BOOST_CHECK_THROW(app.Period.asFloat(), NidasAppException);
}


class DaemonApp : public NidasApp
{
public:
  DaemonApp() :
    NidasApp("daemon")
  {
    enableArguments(Help | ConfigsArg |
                    Username | Hostname | DebugDaemon |
                    DatasetName |
                    loggingArgs() | Version);
  }
};


BOOST_AUTO_TEST_CASE(test_nidas_app_daemon_debug)
{
  // Make sure debug-daemon mode is false unless specified, but logging
  // config can still change.
  {
    DaemonApp app;
    ArgVector cmdline{"--log", "debug"};
    ArgVector args = app.parseArgs(cmdline);
    BOOST_CHECK(args.empty());
    BOOST_CHECK(!app.DebugDaemon.asBool());
    BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_DEBUG);
  }
  // Likewise we should be able to disable daemon mode but change the
  // logging from debug.
  {
    DaemonApp app;
    ArgVector cmdline{"-d", "--log", "info"};
    ArgVector args = app.parseArgs(cmdline);
    BOOST_CHECK(args.empty());
    BOOST_CHECK(app.DebugDaemon.asBool());
    BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_INFO);
  }
  // Order does not matter, because the daemon debug is a fallback.
  {
    DaemonApp app;
    ArgVector cmdline{"--log", "info", "-d"};
    ArgVector args = app.parseArgs(cmdline);
    BOOST_CHECK(args.empty());
    BOOST_CHECK(app.DebugDaemon.asBool());
    BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_INFO);
  }
  // Log fields is set for daemon mode unless set by a user argument.
  {
    DaemonApp app;
    ArgVector cmdline{"-d", "--logfields", LogScheme().getShowFieldsString()};
    ArgVector args = app.parseArgs(cmdline);
    app.setupDaemonLogging();
    BOOST_CHECK_EQUAL(app.logLevel(), LOGGER_DEBUG);
    BOOST_CHECK_EQUAL(Logger::getScheme().getShowFieldsString(),
                      LogScheme().getShowFieldsString());
  }
  // Make sure the log level for daemons defaults to INFO and removes time
  // from the fields.
  {
    DaemonApp app;
    BOOST_CHECK(! app.DebugDaemon.asBool());
    app.setupDaemonLogging();
    BOOST_CHECK_EQUAL(Logger::getScheme().logLevel(), LOGGER_INFO);
    BOOST_CHECK_EQUAL(Logger::getScheme().getShowFieldsString(),
                      "level,message");
  }
  // Finally, since this is a common use case, make sure logging can be
  // restricted to lower log levels in daemon mode.
  {
    DaemonApp app;
    ArgVector cmdline{"-l", "5"};
    ArgVector args = app.parseArgs(cmdline);
    BOOST_CHECK_EQUAL(Logger::getScheme().logLevel(), LOGGER_NOTICE);
    BOOST_CHECK_EQUAL(Logger::getScheme().getConfigs().size(), 1);
    app.setupDaemonLogging();
    BOOST_CHECK_EQUAL(Logger::getScheme().getShowFieldsString(),
                      "level,message");
    BOOST_CHECK_EQUAL(app.DebugDaemon.asBool(), false);
  }
}
