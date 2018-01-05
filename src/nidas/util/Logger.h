// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2006, Copyright University Corporation for Atmospheric Research
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

#ifndef NIDAS_UTIL_LOGGER_H_
#define NIDAS_UTIL_LOGGER_H_

#include "ThreadSupport.h"

#include <cstdarg>
#include <syslog.h>

#include <iostream>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>

#if !defined(SVR4) && ( defined(__GNUC__) && __GNUC__ < 2)
#include <varargs.h>
#endif

namespace nidas { namespace util {

/**
 * @defgroup Logging Logging
 *
 * Three main classes comprise the nidas::util logging facility:
 *
 *  - nidas::util::LogContext
 *  - nidas::util::Logger
 *  - nidas::util::LogConfig
 *
 * The intended use of this interface is through the @ref LoggerMacros.
 * These macros automatically instantiate a LogContext and control the
 * formatting and sending of the log message according to the current
 * logging configuration.  The advantage of the macros is that they are
 * less verbose and there is no overhead formatting the message parameters
 * if the log message is not currently enabled.  However, the original form
 * of calling the Logger::log() method directly still works.  That form
 * could be more natural in some cases.  It is equivalent to the macros
 * except the log message call cannot suppressed: the message itself is
 * suppressed within the log() method by checking the LogContext against
 * the current configuration.  The syslog macros like LOG_EMERG are
 * redefined to be a comma-separated list of the LogContext parameters, so
 * those parameters are passed into the log() call.  In other words,
 * consider original logging code that looks like this:
 *
 * @code
 * n_u::Logger::getInstance()->log(LOG_INFO,
 *    "~DSMEngine %s: %s",output->getName().c_str(),e.what());
 * @endcode
 *
 * LOG_INFO is expanded to this:
 *
 * @code
 * nidas::util::LOGGER_INFO, __FILE__, __PRETTY_FUNCTION__, __LINE__
 * @endcode
 *
 * So the log() call receives all of the context information it needs
 * to first of all compile a more complete log message, and second to
 * determine if the message has been enabled.
 *
 * The log() call above could be rewritten using a macro as follows:
 *
 * @code
 * ILOG(("~DSMEngine %s: %s",output->getName().c_str(),e.what()));
 * @endcode
 * 
 * or alternatively with ostream operators:
 *
 * @code
 * ILOG(("~DSMEngine ") << output->getName() << ": " << e.what());
 * @endcode
 *
 * LogConfig objects can be created to configure the set of log points
 * which will be active.  See @ref LoggerSchemes.
 *
 * NIDAS can configure the available logging schemes and the current scheme
 * through the XML config file.  Below is an excerpt from within the
 * `<project>` element.  The log scheme is set with the `<logger>` element.
 *
 * @todo A nice enhancement someday would be to set the log scheme name
 * from the dsm command-line.
 *
 * @code

 <logger scheme='everything' />

 <logscheme name='everything'>
 <showfields>all</showfields>
 <logconfig level='debug'/>
 <logconfig filematch='nidas'/>
 </logscheme>

 <logscheme name='everything-but-utils'>
 <showfields>all</showfields>
 <logconfig level='debug'/>
 <logconfig filematch='util' activate='false'/>
 </logscheme>

<logscheme name='nothing'>
    </logscheme>

    <logscheme name='info'>
    <logconfig level='info'/>
    </logscheme>

    <logscheme name='sampledebug'>
    <logconfig level='debug'/>
    <logconfig tagmatch='samples'/>
    </logscheme>

    <logscheme name='iss-debug'>
    <showfields>level,time,function,message</showfields>
    <logconfig filematch='dynld/iss'/>
    </logscheme>

    <logscheme name='tilt-sensor-debug'>
    <logconfig filematch='dynld/iss'/>
    <logconfig filematch='dynld/psql'/>
    <logconfig filematch='nidas/core'/>
    </logscheme>

    * @endcode
*/
    /**@{*/

    /** @file Logger.h
     *
     * Header file for the nidas::util logging facility.
     **/

    /**
     * @defgroup LogLevelSymbols Integer Logger Levels
     *
     * Define constants equal to the syslog levels, so we can redefine the
     * syslog symbols.
     **/
    /**@ingroup LogLevelSymbols
       @{*/
    const int LOGGER_EMERG = LOG_EMERG;
    const int LOGGER_EMERGENCY = LOG_EMERG;
    const int LOGGER_ALERT = LOG_ALERT;
    const int LOGGER_CRIT = LOG_CRIT;
    const int LOGGER_CRITICAL = LOG_CRIT;
    const int LOGGER_ERROR = LOG_ERR;
    const int LOGGER_ERR = LOG_ERR;
    const int LOGGER_PROBLEM = LOG_ERR;
    const int LOGGER_WARNING = LOG_WARNING;
    const int LOGGER_NOTICE = LOG_NOTICE;
    const int LOGGER_INFO = LOG_INFO;
    const int LOGGER_DEBUG = LOG_DEBUG;
    const int LOGGER_VERBOSE = LOG_DEBUG+1;
    const int LOGGER_NONE = LOG_EMERG-1;
    /**@}*/

#undef	LOG_EMERG
#undef	LOG_ALERT
#undef	LOG_CRIT
#undef	LOG_ERR
#undef	LOG_WARNING
#undef	LOG_NOTICE
#undef	LOG_INFO
#undef	LOG_DEBUG

#define LOG_CONTEXT(LEVEL) \
    nidas::util::LEVEL, __FILE__, __PRETTY_FUNCTION__, __LINE__

    // Redefine the log levels to be a full LogContext instance.
#define LOG_EMERG LOG_CONTEXT(LOGGER_EMERG)
#define	LOG_ALERT LOG_CONTEXT(LOGGER_ALERT)
#define	LOG_CRIT LOG_CONTEXT(LOGGER_CRIT)
#define	LOG_ERR LOG_CONTEXT(LOGGER_ERR)
#define	LOG_WARNING LOG_CONTEXT(LOGGER_WARNING)
#define	LOG_NOTICE LOG_CONTEXT(LOGGER_NOTICE)
#define	LOG_INFO LOG_CONTEXT(LOGGER_INFO)
#define	LOG_DEBUG LOG_CONTEXT(LOGGER_DEBUG)
#define	LOG_VERBOSE LOG_CONTEXT(LOGGER_VERBOSE)

/**
 * This macro creates a static LogContext instance that is not in
 * thread-local storage and therefore is not thread-safe.  The active flag
 * will be written from any thread which changes the log configuration, and
 * reads of the active flag happen in all threads which execute this log
 * point.  TLS was implemented at one point, but it turned out not to be
 * portable enough.  Locking is not really warranted given that in practice
 * logging configurations do not change during runtime.
 *
 * If thread-safety is required, then an automatic lock can be added to the
 * beginning of the do-while block.  I don't think the static
 * initialization needs to happen inside the lock, at least for GCC.  GCC
 * already guards static initialization, but helgrind or DRD may not be
 * able to recognize that without surrounding the block with a pthread
 * lock.  The actual check of the active flag probably should be
 * thread-safe, since presumably one thread will initially write it and
 * then multiple threads will read it.  If the write happens inside the
 * guard and only reads happen outside, then maybe that's safe enough since
 * the reconfiguration of running log points does not happen in practice.
 *
 * The lock used cannot be the global logging lock unless it is unlocked
 * before calling the log() method, since the global lock is locked by the
 * log() method and the lock is not recurisve.  One goal for all this is to
 * make things look reasonable and consistent to program checkers like
 * helgrind.
 *
 * The VERBOSE log level is intended for very verbose log messages which
 * typically would only be used by developers.  They are never enabled by a
 * default LogConfig, they are above the DEBUG threshold, and in practice
 * their overhead should be minimized by testing whether the log point is
 * active() before generating any log output.  It should be safe to compile
 * them into code, and that should be preferred over surrounding them in a
 * pre-processor conditional compilation block.
 **/
#define LOGGER_LOGPOINT(LEVEL,TAGS,MSG)                                 \
    do {                                                                \
        static nidas::util::LogContext logctxt                          \
            (nidas::util::LEVEL, __FILE__,__PRETTY_FUNCTION__,          \
             __LINE__,TAGS);                                            \
        if (logctxt.active())                                           \
            logctxt.log(nidas::util::LogMessage().format MSG); }        \
    while (0)

    /**
     * @defgroup LoggerMacros Logging Macros
     *
     * These macros specify a log point in the code from which the given
     * message can be logged with the given log level.  The macro generates
     * code which first tests whether the log point is active before
     * generating the message, thus minimizing the time overhead for
     * logging.  The active() status is tested without locking, so if
     * multiple threads are sharing a log point, or if logging is
     * reconfigured while threads are running, then that could be
     * considered a violation of mutual exclusion.  Concurrency checkers
     * like helgrind may complain about this in multithreaded code.
     *
     * The @p MSG argument must be in parentheses, so that it can be a
     * variable argument list.  The whole argument list is passed to
     * LogMessage::format() to generate the message string.
     *
     * @code
     * DLOG(("the current value of pi is %f", pi));
     * @endcode
     *
     * Since LogMessage::format() returns the LogMessage instance, further text
     * can be added to the message with the streaming operator<<, like so:
     *
     * @code
     * DLOG(("pi = ") << pi);
     * @endcode
     *
     * The macro forms with the T suffix include a tags string which will be
     * associated with the LogContext:
     *
     * @code
     * DLOGT("samples,variable_conversion",
     *       ("convert %s: calculating, ", vname) << nvar << " processed.");
     * @endcode
     *
     * Tags just allow one more way to discriminate among log points.  Log
     * points can be activated or deactivated according to their tags through a
     * LogConfig.
     **/
    /**@ingroup LoggerMacros
       @{*/
#define ELOG(MSG) LOGGER_LOGPOINT(LOGGER_EMERG,"",MSG)
#define ALOG(MSG) LOGGER_LOGPOINT(LOGGER_ALERT,"",MSG)
#define CLOG(MSG) LOGGER_LOGPOINT(LOGGER_CRITICAL,"",MSG)
#define PLOG(MSG) LOGGER_LOGPOINT(LOGGER_ERR,"",MSG) // For Problem, as in Error
#define WLOG(MSG) LOGGER_LOGPOINT(LOGGER_WARNING,"",MSG)
#define NLOG(MSG) LOGGER_LOGPOINT(LOGGER_NOTICE,"",MSG)
#define ILOG(MSG) LOGGER_LOGPOINT(LOGGER_INFO,"",MSG)
#define DLOG(MSG) LOGGER_LOGPOINT(LOGGER_DEBUG,"",MSG)
#define VLOG(MSG) LOGGER_LOGPOINT(LOGGER_VERBOSE,"",MSG)

#define ELOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_EMERG,TAGS,MSG)
#define ALOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_ALERT,TAGS,MSG)
#define CLOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_CRITICAL,TAGS,MSG)
#define PLOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_ERR,TAGS,MSG) // For Problem, as in Error
#define WLOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_WARNING,TAGS,MSG)
#define NLOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_NOTICE,TAGS,MSG)
#define ILOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_INFO,TAGS,MSG)
#define DLOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_DEBUG,TAGS,MSG)
#define VLOGT(TAGS,MSG) LOGGER_LOGPOINT(LOGGER_VERBOSE,TAGS,MSG)
    /**@}*/

    class Logger;
    class LoggerPrivate;
    class LogMessage;

    /**
     * Convert the name of a log level to its integer value.
     * @returns The log level value, or -1 if the name is not recognized.
     **/ 
    int
    stringToLogLevel(const std::string& slevel);

    /**
     * Convert an integral log level to a string name.
     * @returns The name for the given log level, or else "emergency".
     **/ 
    std::string
    logLevelToString(int);

    class Mutex;

    /**
     * The LogContext is created at a point in the application code and filled
     * in with details about that log point, such as the file and line number,
     * and the function name of the containing function, and the log level of
     * that log point, such as LOGGER_DEBUG or LOGGER_ERROR.  Except for the
     * log level and tags, the rest of the LogContext can be filled in with CPP
     * macros and predefined compiler symbols like __PRETTY_FUNCTION__.  The
     * LogContext holds its own active state, so a simple, inline boolean test
     * determines whether a log point has been activated and thus needs to
     * generate and log its message.  Otherwise the log point can be skipped
     * with minimal time overhead.  Where further discrimination of log points
     * is needed, a log point can be associated with a string of tags, and a
     * LogConfig can match the log point by one of its tags.  The tags string
     * is empty unless specified at creation.
     *
     * The LogContext effectively stashes context info for a log point so that
     * info can be used in the log message output, and so that info can be used
     * as criteria for selecting which messages are logged.  Thus a LogContext
     * registers itself with the Logger class, and the Logger class can enable
     * or disable a log point according to the current configuration scheme,
     * @ref LoggerSchemes.  The LogContext contains a flag which is false if
     * the log point is not active, meaning almost all overhead of generating a
     * log message can be avoided by testing the active() flag first.  The
     * typical usage of a LogContext will be as a static object, and the log
     * macros DLOG, ELOG, and so on test the active() flag before passing a
     * message.
     *
     * The ultimate goal is to make all log messages always available in the
     * runtime while incurring as little performance overhead as possible when
     * they are not needed, and without requiring alternate compile
     * configurations.  However, if it should ever be necessary, then the log
     * macros can still be defined as completely empty.
     *
     * See LogContext::log() for using a LogContext instance to enclose more
     * complicated blocks of logging output.
     *
     * A LogContext has no support for concurrency.  If it might be shared
     * among multiple threads, then it should be guarded with a lock or
     * created in thread-local storage.  However, a LogContext does record
     * the thread which created it, and the name of that thread is returned 
     * by the threadName() method.
     **/
    class LogContext
    {
    public:

        /**
         * The LogContext constructor initializes all the static context
         * information, adds this context to the global registry of log
         * points, then sets the active() status according to the current
         * log configuration.  The constructor also records the ID of the
         * current thread, so if this LogContext is instantiated in
         * thread-local storage, all messages logged through it will be
         * associated with the correct thread name.
         **/
        LogContext (int level, const char* file, const char* function, int line,
                    const char* tags = 0);

        ~LogContext();

        /**
         * Return true if log messages from this context would be accepted.
         * If this returns false, then there is no point in generating and
         * submitting a log message.
         **/
        bool
        active() const
        {
            return _active;
        }

        const char*
        filename() const
        {
            return _file;
        }

        const char*
        function() const
        {
            return _function;
        }

        int
        line() const
        {
            return _line;
        }

        int
        level() const
        {
            return _level;
        }

        const char*
        tags() const
        {
            return _tags;
        }

        /**
         * Return the name of the thread which created this context.  This
         * is *not* necessarily the name of the currently running thread,
         * so it may not be the same as the thread named in a log message
         * when LogScheme::ThreadField is enabled in the LogScheme.
         **/
        std::string
        threadName() const;

        std::string
        levelName() const
        {
            return logLevelToString(_level);
        }

        /**
         * Convenience method which writes the given message to the current
         * Logger instance, passing this object as the LogContext instance.
         *
         * @code
         * static LogContext lp(LOG_INFO);
         * if (lp.active())
         * {
         *     LogMessage msg;
         *     msg << "complicated info output...";
         *     lp.log(msg);
         * }
         * @endcode
         **/
        inline void
        log(const std::string& msg) const;

        /**
         * Return a LogMessage associated with this LogContext, so the
         * message will be logged through this context when the LogMessage
         * goes out of scope.  Typically this is used as a temporary object
         * to which the log message content can be streamed, which can be a
         * little more convenient than formatting the message separately
         * and then passing it to log(const std::string& msg).  Also see
         * the LogMessage() constructor which takes a LogContext.
         *
         * @code
         * static LogContext lp(LOG_INFO);
         * if (lp.active())
         * {
         *     lp.log() << "complicated info output...";
         * }
         * @endcode
         **/
        inline LogMessage
        log() const;

    private:
        int _level;
        const char* _file;
        const char* _function;
        int _line;
        const char* _tags;

        bool _active;

        pthread_t _threadId;

        friend class nidas::util::Logger;
        friend class nidas::util::LoggerPrivate;

        /** No copying */
        LogContext (const LogContext&);

        /** No assignment */
        LogContext& operator= (const LogContext&);
    };


    /**
     * A configuration to enable or disable a matching set of log points.
     * A LogConfig specifies which log points to match and whether to
     * activate or disable them.  The set of log points are matched by
     * filename, function name, line number, and level.  For example, all
     * the log messages in a given file can be enabled with a LogConfig
     * that sets @c filename_match to the filename and @c level to @c
     * LOGGER_DEBUG.  Methods within an object all include the class name
     * in the function signature, so those can be enabled with a
     * function_match set to the class name.  The default LogConfig matches
     * every log point with threshold DEBUG or lower, ie, not VERBOSE
     * messages.  File and function names are matched as substrings, so a
     * @c function_match set to "Logger::" will match all methods of the
     * Logger class.
     **/
    class LogConfig
    {
    public:

        /**
         * Apply this config to log points in filenames which contain the 
         * given string.  An empty string matches everything.
         **/
        std::string filename_match;

        /**
         * Apply this config to log points in functions which contain the
         * given string.  An empty string matches everything.
         **/
        std::string function_match;

        /**
         * Apply this config to log points whose tags include this tag.  An
         * empty string matches all tags.
         **/
        std::string tag_match;

        /**
         * Apply this config to log points with the given line number.  Really
         * this only makes sense if filename_match is set also.  The default of
         * zero matches every line number.
         **/
        int line;

        /**
         * For all matching log points, enable the log point if the log level is
         * more severe (lower value) than this threshold level.  The default
         * threshold includes all log levels.
         **/
        int level;

        /**
         * If true, then all matching log points will be enabled.  If false, any
         * matching log points will be disabled.
         **/
        bool activate;

        /**
         * Return true if this config matches the given LogContext @p lc.
         **/
        bool
        matches(const LogContext& lc) const;

        /**
         * Parse LogConfig settings from a string specifier, using
         * comma-separated fields to assign to each of the config fields.
         * Return true if the parse is successful, otherwise return false
         * and leave the config in an indeterminate state.
         *
         * Here are the fields:
         *
         * <loglevelint>|<loglevelname>
         * tag=<tag>
         * file=<file>
         * function=<function>
         * line=<line>
         * enable|disable
         *
         * This example enables all log messages for filenames which
         * contain the string 'TwoD':
         *
         * verbose,file=TwoD
         *
         * Enable all debug messages in the core library:
         *
         * debug,file=nidas/core
         **/
        bool
        parse(const std::string& text);

        /**
         * Construct a default LogConfig which matches and enables every
         * log point with level DEBUG or higher, then configure it further
         * by calling parse() on the passed text string.  Throws
         * std::runtime_error if the text fails to parse.
         **/
        LogConfig(const std::string& text = "");
    };


    /**
     * A LogScheme is a vector of LogConfig's and the vector of fields to show
     * in log messages.
     **/
    class LogScheme
    {
    public:

        enum LogField 
        {
            NoneField = 0,
            ThreadField = 1, 
            FunctionField = 2, 
            FileField = 4, 
            LevelField = 8,
            MessageField = 16,
            TimeField = 32,
            AllFields = 63
        };

        /**
         * The type for the vector of LogConfigs returned by getConfigs().
         **/
        typedef std::vector<LogConfig> log_configs_v;

        /**
         * Convert the name of a log information field to its enum, such as
         * Logger::ThreadField.  Returns NoneField if the name is not
         * recognized.
         **/
        static LogField
        stringToField(const std::string& s);

        /**
         * Convert a LogField to its lower-case string name.  Returns "none"
         * if the LogField is NoneField or invalid.
         **/
        static std::string
        fieldToString(LogScheme::LogField lf);

        /**
         * Return the LogScheme currently in effect for the global Logger
         * instance.
         **/
        static LogScheme
        current();

        /**
         * The default LogScheme has show fields set to "time,level,message" and
         * a single LogConfig to enable all messages with level LOGGER_WARNING
         * and above.  The default name is "default".  Explicitly passing an
         * empty name will also force a name of "default", since a LogScheme is
         * prohibited from having an empty name.
         *
         * Initially a LogScheme has no LogConfig entries, and so a
         * default scheme does not enable any log messages, 
         **/
        explicit
        LogScheme(const std::string& name = "default");

        /**
         * LogScheme names must not be empty, so this method has no effect
         * unless name.length() > 0.
         **/
        LogScheme&
        setName(const std::string& name)
        {
            if (name.length() > 0)
                _name = name;
            return *this;
        }

        const std::string&
        getName() const
        {
            return _name;
        }

        /**
         * Clear the set of LogConfig's in this scheme.  An empty set of
         * LogConfig's disables all logging for this scheme.
         **/
        LogScheme&
        clearConfigs();

        /**
         * Push a new LogConfig onto the list of existing configs.  This config
         * will be applied to any log points which it matches.  For example, if
         * the @c activate flag of this config is false, then any matching log
         * points will be disabled.  The configurations are cumulative, so the
         * latest config on the list will take precedence.  For example, if an
         * earlier config enables all points, then further configs can disable a
         * subset of log points.
         *
         * @param lc The LogConfig to be added to this scheme.
         * @returns This scheme.
         **/
        LogScheme&
        addConfig(const LogConfig& lc);

        /**
         * Return a copy of the LogConfig instances added to this
         * LogScheme.
         **/
        log_configs_v
        getConfigs();

        /**
         * Return the highest level of log messages enabled in all the
         * LogConfig instances added to this LogScheme.  For basic
         * configurations, where a single LogConfig just enables all
         * messages of a certain level, this method returns that level.  In
         * more complicated configurations, there may be only a few log
         * messages enabled at the returned level, but there will be no
         * messages enabled at a higher level.
         *
         * Since a LogScheme with no configs enables no messages, it
         * returns LOGGER_NONE.
         **/
        int
        logLevel();

        /**
         * Set the fields to show in log messages and their order.  The fields
         * in @p fields will be shown in the same order as in the vector.  Any
         * fields not included will be omitted.
         *
         * @param fields A vector<LogField>.
         **/
        LogScheme&
        setShowFields(const std::vector<LogField>& fields);

        /**
         * Parse a comma-separated string of field names, with no spaces,
         * into a vector of LogField values, and pass that to setShowFields().
         *
         * Valid fields: thread,function,file,level,time,message
         **/
        LogScheme&
        setShowFields(const std::string& fields);

        /**
         * Return a comma-separated string describing the list of fields
         * to be shown by this scheme.
         **/
        std::string
        getShowFieldsString () const;

        /**
         * Parse a parameter setting using syntax <name>=<value>.
         **/
        bool
        parseParameter(const std::string& text);

        /**
         * Set a parameter for this scheme.
         **/
        void
        setParameter(const std::string& name, const std::string& value);

        /**
         * Return the value of the parameter.  If the parameter has not
         * been set, then return the default.
         **/
        std::string
        getParameter(const std::string& name, const std::string& dvalue="");

        template <typename T>
        T
        getParameterT(const std::string& name, const T& dvalue = T());

        /**
         * Return the value of the parameter.  If the parameter has not
         * been set, then look for a value in the environment, and
         * otherwise return the default.
         **/
        std::string
        getEnvParameter(const std::string& name, const std::string& dvalue="");

        /**
         * If @p show is true, then all the known log points will be listed
         * in log messages, and all future log points will be logged as
         * they are created.  Disable this logging by passing false.
         **/
        void
        showLogPoints(bool show);

        /**
         * Return true if showLogPoints() is enabled.
         **/
        bool
        getShowLogPoints()
        {
            return _showlogpoints;
        }

    private:

        void
        show_log_point(LogContext& lp);

        std::string _name;
        log_configs_v log_configs;
        std::vector<LogField> log_fields;
        std::map<std::string, std::string> _parameters;
        bool _showlogpoints;

        friend class nidas::util::LoggerPrivate;
        friend class nidas::util::Logger;
        friend class nidas::util::LogContext;
    };


    /**
     * A class for formatting and streaming a log message.  Text can be
     * appended to the message with a printf() format or streamed with the
     * stream output operator<<.  The implementation essentially wraps an
     * ostringstream.
     **/
    class LogMessage
    {
    public:
        /**
         * Create a LogMessage, optionally set to an initial string.
         **/
        LogMessage(const std::string& s = "") :
            msg(),
            _log_context(0)
        {
            msg << s;
        }

        /**
         * Associate this LogMessage with a LogContext.  When the
         * LogMessage is destroyed (eg, goes out of scope) or when the
         * log() method is called, the current message (if any) is logged
         * through the LogContext::log() method.  The message optionally
         * can be given an initial value.
         *
         * This can be used to build up complicated logging messages which
         * must be streamed incrementally, possibly dispersed throughout
         * the code.  Instantiate a LogContext and associate a LogMessage
         * with it, then stream to the LogMessage if the context is active.
         * The LogMessage can be reused to send multiple log messages, or
         * to stream data and push the log message when it gets too long.
         *
         * @code
         * static n_u::LogContext sdlog(LOG_VERBOSE, "slice_debug");
         * static n_u::LogMessage sdmsg(&sdlog);
         * if (sdlog.active())
         * {
         *    sdmsg << "initial data: " << value;
         * }
         * ...
         * if (sdlog.active())
         * {
         *    sdmsg << "more data: " << value2;
         *    if (sdmsg.length() > 80)
         *    {
         *       sdmsg << endlog;
         *    }
         * }
         * @endcode
         **/
        LogMessage(const LogContext* lp, const std::string& s = "") :
            msg(),
            _log_context(lp)
        {
            msg << s;
        }

        LogMessage(const LogMessage& right) :
            msg(),
            _log_context(right._log_context)
        {
            msg << right.msg.str();
        }

        LogMessage&
        operator=(const LogMessage& right)
        {
            msg << right.msg.str();
            _log_context = right._log_context;
            return *this;
        }

        LogMessage&
        format(const char *fmt, ...);

        std::string
        getMessage() const
        {
            return msg.str();
        }

        operator std::string () const
        {
            return msg.str();
        }

        template <typename T>
        LogMessage&
        operator<< (const T& t);

        /**
         * If this message was associated with a LogContext, then send the
         * completed message to it when this instance is destroyed, ie,
         * when it goes out of scope.
         **/
        ~LogMessage()
        {
            log();
        }

        /**
         * Return the length of the current message buffer.  This can be
         * used to test whether there is anything to log yet, or to cut off
         * a stream of log info to limit the line length.
         *
         * @code
         * if (logmsg.length() > 80)
         * {
         *     logmsg << endlog;
         * }
         * logmsg << data << ",";
         * @endcode
         **/
        inline std::streampos
        length()
        {
            return msg.tellp();
        }

        /**
         * If this LogMessage is associated with a LogContext and if the
         * current message is not empty, then log the current message with
         * the LogContext.  Clear the buffer and start a new message.
         **/
        void
        log()
        {
            if (_log_context && length() > 0)
            {
                _log_context->log(*this);
            }
            msg.str("");
            msg.clear();
        }

    private:
        std::ostringstream msg;
        const LogContext* _log_context;
    };


    /**
     * Everything streamed to a LogMessage is passed on to the underlying
     * ostringstream, including ostream manipulators.
     **/
    template <typename T>
    inline
    LogMessage&
    LogMessage::
    operator<< (const T& t)
    {
        msg << t;
        return *this;
    }


    /**
     * LogMessage manipulator which logs the current message buffer, if
     * any, and then clears the message.
     **/
    inline LogMessage&
    endlog(LogMessage& logmsg)
    {
        logmsg.log();
        return logmsg;
    }

    /**
     * Template to call LogMessage manipulators like endlog when streamed
     * to a LogMessage.
     **/
    inline LogMessage&
    operator<<(LogMessage& logmsg, LogMessage& (*op)(LogMessage&))
    {
        return (*op)(logmsg);
    }

    /**
     * Simple logging class, based on UNIX syslog interface.  The Logger is a
     * singleton through which all log messages are sent.  It determines
     * whether messages are sent to syslog or written to an output stream
     * provided by the application, such as std::cerr, or a file output stream,
     * or an ostringstream.
     */
    class Logger {
    protected:
        Logger(const char *ident, int logopt, int facility, const char *TZ);
        Logger(std::ostream* );
        Logger();

    public:

        ~Logger();

        /** 
         * Create a syslog-type Logger. See syslog(2) man page.
         * @param ident: see syslog parameter
         * @param logopt: see syslog parameter
         * @param facility: see syslog parameter
         * @param TZ: string containing timezone for syslog time strings
         *        If NULL(0), use default timezone.
         */
        static Logger* 
        createInstance(const char *ident, int logopt, int facility,
                       const char *TZ = 0);

        /**
         * Create a logger to the given output stream.  The output stream
         * should remain valid as long as this logger exists.  This Logger
         * does not take responsibility for closing or destroying the
         * stream.
         *
         * @param out Pointer to the output stream.
         **/
        static Logger* createInstance(std::ostream* out);

        /**
         * Return a pointer to the currently active Logger singleton.
         **/
        static Logger* getInstance();

        /**
         * Build a message from a printf-format string and variable args, and log
         * the message.  The severity should be passed using one of the syslog
         * macros, such as @c LOG_DEBUG or @c LOG_EMERG.  Those macros are
         * redefined such that they fill in the first four parameters
         * automatically:
         *
         * @code
         * Logger::getInstance()->log (LOG_DEBUG, "pi=%f", 3.14159);
         * @endcode
         *
         * This method is mostly for backwards compatibility.  The newer log
         * method is to use the log context macros @c DLOG, @c ILOG, @c ELOG, and
         * so on, since those macros will not waste time generating the message
         * if the message will not be logged.
         **/
#if defined(SVR4) || ( defined(__GNUC__) && __GNUC__ > 1 )
        void
        log(int severity, const char* file, const char* fn,
            int line, const char *fmt, ...);
#elif defined(__GNUC__)
        void
        log(...);
#else
        void
        log(va_alist);
#endif

        /**
         * Send a log message for the given LogContext @p lc.  No check is
         * done for whether the context is active or not, the message is
         * just immediately sent to the current log output, formatted
         * according to the context.  For syslog output, the message and
         * severity level are passed.  For all other output, the message
         * includes the current time and the log context info, such as
         * filename, line number, function name, and thread name.
         **/
        void
        msg(const LogContext& lc, const std::string& msg);

        inline void
        msg(const LogContext& lc, const LogMessage& m)
        {
            msg (lc, m.getMessage());
        }

        /**
         * @defgroup LoggerSchemes Logging Configuration Schemes
         * 
         * The @ref Logging facility can store multiple logging configuration
         * schemes, each with a particular name.  Each scheme is a list of
         * LogConfig objects.  These methods are used to manipulate the set of
         * schemes known by the Logger.  At any time the logging configuration
         * can switch schemes by calling setScheme().  The first time a new
         * scheme name is referenced, a default LogScheme is created with that
         * name.
         **/
        /**@{*/


        /**
         * Set the current scheme.  If a scheme named @p name does not exist and
         * @p name is non-empty, a default scheme is created with this name.
         * Trying to set the current scheme to an empty name has no effect.
         **/
        void
        setScheme(const std::string& name);

        /**
         * Set the current scheme to the given @p scheme.  The scheme is first
         * added with updateScheme(), then it becomes the current scheme.
         **/
        void
        setScheme(const LogScheme& scheme);

        /**
         * Update or insert this scheme in the collection of log configuration
         * schemes.  If a scheme already exists with the same name, this scheme
         * will replace it.  If the given scheme has the same name as the
         * Logger's current scheme, then the Logger's scheme will be updated
         * also.
         **/
        void
        updateScheme(const LogScheme& scheme);

        /**
         * Get a copy of the scheme with the given @p name.  If the name does not
         * exist, a default scheme is returned.  If the name is empty, then
         * the current scheme is returned.
         **/
        LogScheme
        getScheme(const std::string& name = "") const;

        /**@}*/


    protected:
        /**
         * Set the timezone to be used in the log messages, whether passed to
         * syslog or formatted to include the current time.
         *
         * To be careful, don't make this public. The user should set the
         * TZ once in the constructor. Otherwise, in a multithreaded app
         * the log() method could have problems, since this is a singleton
         * shared by multiple threads, and we don't provide a locking
         * mechanism for loggerTZ.
         */
        void setTZ(const char* val);

        static Logger* _instance;

        std::ostream* output;
        bool syslogit;

        char* loggerTZ;
        char* saveTZ;
    private:

        /**
         * Other Log classes call this method to log a message when the log
         * mutex is already locked.
         **/
        void
        msg_locked(const nidas::util::LogContext& lc, const std::string& msg);

        friend class nidas::util::LogContext;
        friend class nidas::util::LogScheme;

        static nidas::util::Mutex mutex;

        /** No copying */
        Logger(const Logger&);

        /** No assignment */
        Logger& operator=(const Logger&);
    };

    inline void
    LogContext::
    log(const std::string& msg) const
    {
        Logger::getInstance()->msg (*this, msg);
    }

    inline LogMessage
    LogContext::
    log() const
    {
        return LogMessage(this);
    }

    template <typename T>
    T
    LogScheme::
    getParameterT(const std::string& name, const T& dvalue)
    {
        // A rudimentary implementation which converts strings
        // to the given type.
        T value;
        std::istringstream text(getParameter(name));
        text >> value;
        if (text.fail())
            value = dvalue;
        return value;
    }

    /**@}*/

}}	// namespace nidas namespace util

#endif
