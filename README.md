# ELog Logging Library

The ELog (Error Log) library is a simple, lightweight, high-performant and feature-rich C++ logging library.  
The library has connectors for various widely-used external systems, and was designed such that it can be extended  
for a broad range of use cases. In addition, it is fully configurable from file or string input (see examples below).

The project is still in pre-Beta phase, and more is expected to come.

## Basic Examples

Simple [logging macros](#logging-macros):

    ELOG_INFO("App starting");
    ELOG_INFO("This is an unsigned integer %u and a string '%s'", 5, "hi");

Possible output:

    2025-07-05 10:35:18.311 INFO   [32680] This is an unsigned integer 5 and a string 'hi'

Using [fmtlib](https://github.com/fmtlib/fmt) formatting style (requires enabling fmtlib extension):

    ELOG_FMT_INFO("This is an unsigned integer {} and a string '{}'", 5, "hi");

Logging with a designated [logger](#defining-log-sources-and-loggers) (no error checking):

    // initialize a thread-safe shared logger
    elog::ELogSource* logSource = elog::defineLogSource("core");
    elog::ELogLogger* logger = logSource->createSharedLogger();

    // user logger with ELOG_xxx_EX macro family:
    ELOG_INFO_EX(logger, "Obtained a logger from source %s with id %u",
            logSource->getQualifiedName(), 
            logSource->getId());

Initialize elog and configure an asynchronous (based on lock-free ring-buffer) rotating file log target, having log segment size 4M, and 20 log segments at most:

    elog::initialize();
    elog::configureTargetFromStr(
        "async://quantum?quantum_buffer_size=1000 | "
        "file:///./app.log?file_segment_size_mb=4&file_segment_count=20");

    // all log messages are now directed to the asynchronous rotating logger
    ELOG_INFO("App starting");

When stack traces are enabled (build ELog with ELOG_ENABLE_STACK_TRACE=ON), a stack trace containing file and line can be dumped:

    ELOG_STACK_TRACE_EX(logger, elog::ELEVEL_INFO, "", 0, "Testing current thread stack trace");

Sample output (Linux):

    2025-07-07 09:53:14.816 INFO   [46748] {main} <elog_root> [Thread 46748 (0xb69c) <main> stack trace]
 
    0# 0x716a4879e618 dbgutil::printStackTraceContext() +185   at dbg_stack_trace.cpp:185 (libdbgutil.so)
    1# 0x716a4903b8cf dbgutil::printStackTrace() +46           at dbg_stack_trace.h:238 (libelog.so)
    2# 0x716a4903b1c1 elog::logStackTrace() +101               at elog.cpp:1527 (libelog.so)
    3# 0x5c4bddcbfb30 initRecovery() +973                      at waL_test.cpp:299 (wal_test_linux)
    4# 0x5c4bddcbeb04 testLoadWALRecord() +120                 at waL_test.cpp:122 (wal_test_linux)
    5# 0x5c4bddcbe96d runTest() +51                            at waL_test.cpp:95 (wal_test_linux)
    6# 0x5c4bddcbe8da main() +389                              at waL_test.cpp:74 (wal_test_linux)
    7# 0x716a4842a1ca N/A                                      at <N/A>  (libc.so.6)
    8# 0x716a4842a28b __libc_start_main()                      at <N/A>  (libc.so.6)
    9# 0x5c4bddcba745 _start() +37                             at <N/A>  (wal_test_linux)

It is also possible to dump stack trace of all running threads (experimental):

    ELOG_APP_STACK_TRACE_EX(logger, elog::ELEVEL_INFO, "", 0, "Testing application stack trace");

This examples adds an asynchronous log target to send log lines to Grafana-Loki, while flushing (i.e. send HTTP message with accumulated log lines) each 100 log lines or 5 seconds, and restricting log shipping to ERROR log level, with retry timeout of 5 seconds (when Loki is down), and backlog of 1 MB of data (after which old log data is discard while Loki is down):

    elog::configureTargetFromStr(
        "async://quantum?quantum_buffer_size=1000&log_level=ERROR | "
        "mon://grafana?mode=json&"
        "loki_endpoint=http://192.168.108.111:3100&"
        "labels={\"app\": \"${app}$\"}&"
        "log_line_metadata={\"log_source\": \"${src}\", \"thread_name\": \"${tname}\"}&"
        "stack_trace=yes&"
        "flush_policy=((count == 100) OR (time == 5000))");

Each log line is accompanied by:

- "app" label, which equals to configured application name (see [field reference tokens](#log-record-field-reference-tokens) for more details)
- Log line metadata, containing the log source and thread name issuing the log message
- Logging thread's fully resolved call stack with function/file/line data for each frame

In this example, the [log line format](#configuring-log-line-format) can be configured:

    elog::configureLogFormat("${time} ${level:6} [${tid}] ${src} ${msg}");

The effect is the following line format:

- time stamp
- followed by log level aligned to the left using width of 6 characters
- followed by thread id, log source name and log message

## Features

The ELog library provides the following notable features:

- High performance
    - Although perhaps not the fastest out there, ELog still provides high performance logging with low overhead
    - Minimal formatting on logging application side, possibly combined with lock-free asynchronous logging
    - Full formatting takes place on background thread
    - **160 nano-seconds latency** using Quantum log target (asynchronous lock-free ring buffer, *scalable* in multi-threaded scenarios)
    - Check out the [benchmarks](#Benchmarks) below
- Synchronous and asynchronous logging schemes
    - Synchronous logging to file, allowing for efficient buffering and log rotation/segmentation
    - **Lock-free** synchronous log file rotation/segmentation
- Wide range of predefined log targets (i.e. "log sinks/appenders"):
    - stdout/stderr
    - syslog
    - Windows event log
    - log file (including rotation/segmentation)
    - databases (PostgreSQL, SQLite, MySQL)
    - message queues (Kafka)
    - RPC endpoints (gRPC)
    - monitoring tools (Grafana Loki, Datadog, Sentry)
- Flexible and rich in features
    - User can define multiple log targets
    - combine with complex filtering schemes and flush policies
    - possibly employing rate limiting
    - Can combine several log targets under one framework and apply common restrictions and properties
- Pre-initialization Logging
    - Accumulates log messages issued during static initialization (or any message before the ELog library is initialized) and issues the accumulated log messages in each added log target.
- *Full Call Stack Dumping* (with function/file/line information)
    - Voluntary current thread call stack dumping (i.e. not in signal handler, so no thread context pointer required)
    - Voluntary full application call stack dumping (i.e. stack trace of all active threads)
- Exception/Crash Handling
    - Out of the box, depends on [dbgutil](#https://github.com/oa-333/dbgutil)
    - Writes to log full exception information, including call stack
    - Generates core dump (mini-dump file on Windows)
- Configurability
    - The entire library is *fully configurable* from file or string, including very complex scenarios (see [basic examples](#basic-examples) above)
    - The purpose is to reduce amount of boilerplate code required just to get things started
    - The following can be configured by file or string:
        - log levels
        - output destinations (log targets/sinks)
        - log file buffering and rotation
        - asynchronous logging schemes
        - formatting patterns, including fonts, colors and conditional formatting
        - complex filters
        - complex flush policies
        - log source hierarchy (file only)
        - log level, with propagation in the log source hierarchy
        - log target affinity (restricting log sources to specific log targets - file only)
        - rate limiting
    - All configurable parameters can be set either globally and/or per log target
- Extendibility
    - All entities in the library are extendible such that they can also be loaded from configuration (i.e. if you extend the library, there is provision to have your extensions to be loadable from configuration file)
        - log targets
        - flush policies
        - log record filters
        - log record formatting
        - log record field reference tokens
    - This requires static registration, which is normally achieved through helper macros
- Various formatting styles
    - Supports both printf and fmtlib formatting style
- Multiple platform support
    - **Linux, Windows, MinGW**
- Intuitive API
    - Most common tasks are achieved via powerful configuration abilities, and using the logging macros
- Extensive Documentation
    - Below you can find a clear and elaborate explanation on how to use, configure and extend the ELog library

Planned/considered future features:

- Support on MacOS
- Connectivity to external TCP/UDP receiver
- Inverse connector with TCP/UDP server and multicast publish beacon (for embedded systems with no IP known in advance)
- Shared memory log target with separate child logging process (for instrumentation scenarios where opening log file is not allowed)


## Common Use Cases

### Logging Library

The most common use case is a utility logging library, in order to write log messages to file, but much more can be done with it.  
For instance, it can be rather easily extended to be hooked to an external message queue, while applying complex message filtering and transformations. One such extension, namely Kafka Connector, is already implemented built-in.  
This could be useful for DevOps use cases.

### Log Flooding

One more use case is a bug investigation that requires log flooding.  
In this case, sending messages to a log file may affect application timing and hinder bug reproduction during heavy logging.  
For such a situation there is a specialized log target (ELogQuantumTarget), which is designed to minimize the logging latency, by using a lock-free ring buffer and a designated background CPU-bound thread that logs batches of log messages.  
Pay attention that simply using a queue guarded by a mutex is not scalable (check out the [benchmark](#multi-threaded-asynchronous-file-log-target-comparison) below).

### External Systems Connectivity

The ELog system also allows directing log messages to several destinations, so tapping to external log analysis tools, for instance, in addition to doing regular logging to file, is also rather straightforward.  
See [dependencies](#external-dependencies) below for a comprehensive list of external systems integrated with elog out of the box.

### Library Development

One more use case is when developing infrastructure library which requires logging, but the actual logging system
that will be used by the enclosing application is not known (and cannot be known).
In this case, the ELog can be used to log messages inside the library, and the using application may configure
the ELog system to redirect and adapt library log message to its own logging system.
This can be done actually quite easily and with much flexibility.

For more information, see [documentation](#documentation) below.

## Getting Started

In order to use the library, include the main header "elog.h", which is the library facade.  
In the application code, make sure to call one of the elog::initializeXXX() functions before using any of the logging macros. After this, you can use ELOG_INFO() and the rest of the macros.  
At application exit make sure to call elog::terminate().

### External Dependencies

The ELog system has no special dependencies, unless connecting to one of the external systems listed above.
In particular the following compile/runtime dependencies exist in each case:

- Kafka connector requires librdkafka.so
- PostgreSQL connector requires libpq.so
- SQLite connector requires libsqlite3.so
- MySQL connector requires mysqlcppconn.lib for compile and mysqlcppconn-10-vs14.dll for runtime (Windows only)
- gRPC connector depends on gRPC and protobuf libraries
- Grafana/Loki and Datadog connectors requires json/nlohmann and httplib
- Sentry connector requires the Sentry native library
- Stack trace logging and exception/crash handling requires [dbgutil](https://github.com/oa-333/dbgutil)
- fmtlib formatting style requires [fmtlib](#https://github.com/fmtlib/fmt)

### Installing

The library can be built and installed by running:

    build.sh --install-dir <install-path>
    build.bat --install-dir <install-path>

(Checkout the possible options with --help switch).

Add to compiler include path:

    -I<install-path>/elog/include/elog
    
Add to linker flags:

    -L<install-path>/bin -lelog

For CMake builds it is possible to use FetchContent as follows:

    FetchContent_Declare(elog
        GIT_REPOSITORY https://github.com/oa-333/elog.git
        GIT_TAG v0.1.0
    )
    FetchContent_MakeAvailable(elog)
    target_include_directories(
        <your project name here>
        PRIVATE
        ${elog_SOURCE_DIR}/src/elog/include
    )
    target_link_libraries(<your project name here> elog)

According to requirements, in the future it may be uploaded to package managers (e.g. vcpkg).

## Help

See [documentation](#Documentation) section below, and documentation in header files for more information.

## Authors

Oren A. (oa.github.333@gmail.com)

## License

This project is licensed under the Apache 2.0 License - see the LICENSE file for details.

# Documentation

## Contents
- [Basic Usage](#basic-usage)
    - [Initialization and Termination](#initialization-and-termination)
    - [Logging Macros](#logging-macros)
    - [Defining Log Sources and Loggers](#defining-log-sources-and-loggers)
    - [Configuring Log Line Format](#configuring-log-line-format)
    - [Log Record Field Reference Tokens](#log-record-field-reference-tokens)
    - [Filtering Log Messages](#filtering-log-messages)
    - [Limiting Log Rate](#limiting-log-rate)
    - [Log Targets](#log-targets)
    - [Flush Policy](#flush-policy)
- [Configuring from File or String](#configuring-from-file-or-string)
    - [Configuring Log Level](#configuring-log-level)
    - [Configuring Log Targets](#configuring-log-targets)
    - [Individual Log Target Configuration](#individual-log-target-configuration)
    - [Configuring Flush Policy](#configuring-flush-policy)
    - [Configuring Log Filters](#configuring-log-filters)
    - [Compound Log Targets](#compound-log-targets)
    - [Configuring Database Log Targets](#configuring-database-log-targets)
    - [Connecting to PostgreSQL](#connecting-to-postgresql)
    - [Connecting to SQLite](#connecting-to-sqlite)
    - [Connecting to MySQL (experimental)](#connecting-to-mysql-experimental)
    - [Connecting to Kafka Topic](#connecting-to-kafka-topic)
    - [Connecting to gRPC Endpoint](#connecting-to-grpc-endpoint)
    - [Connecting to Grafana-Loki](#connecting-to-grafana-loki)
    - [Connecting to Datadog](#connecting-to-datadog)
    - [Connecting to Sentry](#connecting-to-sentry)
    - [Nested Specification Style](#nested-specification-style)
    - [Terminal Text Formatting](#terminal-text-formatting)
    - [Terminal Text Formatting Syntax](#terminal-formatting-formal-syntax-specification)
- [Extending The Library](#extending-the-library)
    - [Extending The Formatting Scheme](#extending-the-formatting-scheme)
    - [Adding New Log Filter Types](#adding-new-log-filter-types)
    - [Adding New Flush Policy Types](#adding-new-flush-policy-types)
    - [Adding New Log Target Types](#adding-new-log-target-types)
    - [Adding New Schema Handler Types](#adding-new-schema-handler-type)
    - [Using Proprietary Protocol for gRPC Log Target](#using-proprietary-protocol-for-grpc-log-target)
- [Benchmarks](#benchmarks)
    - [Benchmark Highlights](#benchmark-highlights)
    - [Empty Logging Benchmark](#empty-logging-benchmark)
    - [Synchronous File Log Target with Count Flush Policy](#synchronous-file-log-target-with-count-flush-policy)
    - [Synchronous File Log Target with Size Flush Policy](#synchronous-file-log-target-with-size-flush-policy)
    - [Synchronous File Log Target with Time Flush Policy](#synchronous-file-log-target-with-time-flush-policy)
    - [Single-threaded Synchronous File Log Target Comparison](#single-threaded-synchronous-file-log-target-comparison)
    - [Multi-threaded Asynchronous File Log Target Comparison](#multi-threaded-asynchronous-file-log-target-comparison)

## Basic Usage

### Initialization and Termination

The ELog library can be used out of box as follows:

    #include "elog.h"
    ...

    int main(int argc, char* argv[]) {
        // initialize the elog system to log into file
        if (!elog::initializeLogFile("./test.log")) {
            fprintf(stderr, "Failed to initialize elog\n");
            return 1;
        }

        // do application stuff
        ELOG_INFO("App starting");
        ...

        // terminate the elog system
        elog::terminate();
        return 0;
    }

The example exhibits the main parts of the ELog library:

- Including the ELog library's main facade header "elog.h"
- Initialization (in this case using a log file)
- Invoking the logging macros
- Termination

There are several initialization functions, which can all be found in the ELogSystem facade.
In this example a synchronous segmented log file is used, with 4MB segment size:

    #include "elog.h"
    ...
    
    #define MB (1024 * 1024)

    int main(int argc, char* argv[]) {
        // initialize the elog system to log into segmented file
        if (!elog::initializeSegmentedLogFile(
                ".",        // log dir
                "test",     // segment base name
                4 * MB)) {  // segment size
            fprintf(stderr, "Failed to initialize elog\n);
            return 1;
        }

        // do application stuff
        ELOG_INFO("App starting");
        ...

        // terminate the elog system
        elog::terminate();
        return 0;
    }

### Logging Macros

The ELog system defines utility macros for logging.  
One group of macros requires a logger, and another group of macros does not.  
This is the easiest form of logging, without any logger defined:

    ELOG_INFO("Sample message with string parameter: %s", someStr);

In order to specify a logger, use the ELOG_xxx_EX() macro set, as follows:

    ELOG_INFO_EX(logger, "Sample message with string parameter: %s", someStr);

### Defining Log Sources and Loggers

One of the main entities in the ELog system is the Log Source.  
It serves as a semantic module entity.  
From one Log Source many loggers can be obtained, one for each logging class/file.
A logger is the logging client's end point, with which log messages are partially formatted,  
before being sent to log targets for actual logging.

Here is a simple example of defining a log source and obtaining a logger.

    elog::ELogSource* logSource = elog::defineLogSource("core");
    if (logSource == nullptr) {
        // NOTE: we use here the default logger
        ELOG_ERROR("Failed to define log source with name core");
    } else {
        elog::ELogLogger* logger = logSource->createSharedLogger();
        ELOG_INFO_EX(logger, "Obtained a logger from source %s with id %u",
            logSource->getQualifiedName(), 
            logSource->getId());
    }

Log sources form a tree hierarchy according to their qualified name, which is separated by dots.  
The root log source has an empty name, and does not have a dot following its name, so that second level  
log sources have a qualified name that is equal to their bare name.

The log level of each log source may be controlled individually.  
The log level of a log source affects all log sources underneath it, unless they specify a log level separately.  
For instance, suppose the following log source hierarchy is defined:

    core
    core.files
    core.thread
    core.net

The core log source may define log level of NOTICE, but the core.thread log level may define log level TRACE.  
The core.files, and core.net log sources will inherit the NOTICE level from the core parent log source.

In order to enable better level of control over the log level of the log source hierarchy, the log source  
provides the setLogLevel() method, which allows specifying how to propagate the log level to child log sources:

    void setLogLevel(ELogLevel logLevel, ELogPropagateMode propagateMode);

In particular there are 4 propagation modes:

- none: No log level propagation takes place at all.
- set: Each descendant log source inherits the log level of the log source ancestor
- restrict: Each descendent log source cannot have less restrictive log level than the log source ancestor
- loose: Each descendent log source cannot have more restrictive log level than the log source ancestor

This allows for more flexibility in configuring the log source tree.

### Configuring Log Line Format

The ELog system allows configuring log line format using a format specification string that supports special log field reference tokens.  
For instance, the default log line format specification that is used by ELog is:

    ${time} ${level:6} [${tid}] ${msg}

In code, it can be done as follows:

    elog::configureLogFormat("${time} ${level:6} [${tid}] ${msg}");

In configuration (globally or per log target), it can be done as follows:

    log_format=${time} ${level:6} [${tid}] ${msg}

This format in reality gets expanded to something like this:

    2025-04-08 11:40:58.807 INFO   [49108] Thread pool of 10 workers started

We see here all 4 components expanded:

- logging time
- log level (aligned to the left with width of 6 characters)
- logging thread id, enclosed with brackets
- formatted log message

### Log Record Field Reference Tokens

The following special log field reference tokens are understood by the ELog system:

- ${rid} - the log record id.
- ${time} - the logging time.
- ${host} - the host name.
- ${user} - the logged in user.
- ${os_name} - the operating system name.
- ${os_ver} - the operating system version.
- ${app} - configurable application name.
- ${prog} - the program name (extracted from executable image file name).
- ${pid} - the process id.
- ${tid} - the logging thread id.
- ${tname} - the logging thread name (requires user collaboration).
- ${level} - the log level.
- ${file} - The logging file name.
- ${line} - The logging line.
- ${func} - The logging function.
- ${src} - the log source of the logger (qualified name).
- ${mod} - the alternative module name associated with the source.
- ${msg} - the log message.

Tokens may contain justification number, where positive means justify to the left,  
and negative number means justify to the right. For instance: ${level:6}, ${tid:-8}.

An extended syntax for reference token was defined for special terminal formatting escape sequences (colors and fonts).
See [terminal text formatting](#terminal-text-formatting) for more details.

### Filtering Log Messages

By default, all messages are logged, but in some use cases, it may be required to filter out some log messages.
In general, the log level may be used to control which messages are logged, and this may be controlled at
each log source. There are some occasions that more fine-grained filtering is required.
In this case ELogFilter may be derived to provide more specialized control.

Pay attention that log filtering is usually applied at the global level, by the call:

    elog::setLogFilter(logFilter);

### Limiting Log Rate

A special instance of a log filter is the rate limiter, which may be applied globally or per log-target:

    // no more than 500 messages per second
    elog::ELogRateLimiter* rateLimiter = new elog::ELogRateLimiter(500);
    elog::setLogFilter(rateLimiter);


### Log Targets

Log targets are where log records are sent to after being partially formatted (and filtered).
The log target usually performs final formatting and writes the formatted log message to a file.
But, as mentioned above, much more can be done.

So, first the ELogFileTarget is defined for simple logging to file, in the logger's context.
In addition, the ELogSegmentedFileTarget is defined for segmented logging, where the segment size
can be specified.

If it is required to avoid logging (and possibly flushing, see below), on the logger's context, 
it is possible to defer that to another context by using the ELogDeferredTarget.
This log target takes another target as the final destination:

    // define a segmented log target with 4K segment size
    elog::ELogTarget* fileTarget = new elog::ELogSegmentedFileTarget("logs", "app.log", 4096, nullptr);

    // combine it with a deferred log target
    elog::ELogTarget* deferredTarget = new elog::ELogDeferredTarget(fileTarget);

    // now set it as the system log target
    elog::setLogTarget(deferredTarget);

The ELogDeferredTarget is quite simplistic. It wakes up the logging thread for each log message.
The ELogQueuedTarget provides more control, by allowing to specify a batch size and a timeout,
so the logging thread wakes up either when the queued number of log messages exceeds some limit, or that 
a specified timeout has passed since last time it woke up.
In the example above we can change it like this:

    // define a segmented log target with 4K segment size
    elog::ELogTarget* fileTarget = new elog::ELogSegmentedFileTarget("logs", "app.log", 4096, nullptr);

    // combine it with a queued log target with batch size 64, and 500 milliseconds timeout
    elog::ELogTarget* queuedTarget = new elog::ELogQueuedTarget(fileTarget, 64, 500);

    // now set it as the system log target
    elog::setLogTarget(queuedTarget);

The ELogQuantumTarget mentioned above can be use das follows:

    // define a segmented log target with 4K segment size
    elog::ELogTarget* fileTarget = new elog::ELogSegmentedFileTarget("logs", "app.log", 4096, nullptr);

    // combine it with a quantum log target that can handle a burst of 1 million log messages
    elog::ELogTarget* quantumTarget = new elog::ELogQuantumTarget(&fileTarget, 1024 * 1024);

    // now set it as the system log target
    elog::setLogTarget(quantumTarget);

The ELogSQLiteDbTarget can be used as follows:

    elog::ELogTarget* sqliteTarget = new elog::ELogSQLiteDbTarget("test.db", "INSERT INTO log_records values(${rid}, ${time}, ${level}, ${host}, ${user}, ${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})");

    // add it as an additional log target
    elog::addLogTarget(sqliteTarget);

### Flush Policy

As some log targets may require flushing (as in file, or any buffered channel), a policy can be defined.
By default, all log targets flush after each message logging.
This strategy is implemented by the ELogImmediateFlushPolicy class.

It may be preferred to avoid flushing altogether.  
This strategy is implemented by the ELogNeverFlushPolicy class.

It may be preferred to flush every X messages.
For this case the ELogCountFlushPolicy can be used.

Another strategy is to flush whenever the amount of logged data exceeds some size limit.
For this case the ELogSizeFlushPolicy can be used.

If a timeout is required (without using a deferred log target), the ELogTimedFlushPolicy can be used.
This is an active policy, which launches a designated thread for this purpose.

In case a combination of strategies is needed, either ELogAndFlushPolicy or ELogOrFlushPolicy can be used.

For any other specialized flush policy, derive from ELogFlushPolicy and override the shouldFlush() virtual method.

## Configuring from File or String

The ELog library provides utility functions to load configuration from file or string:

- configureByPropFile (load definitions from property file, key=value)
- configureByProps (load from property map)
- configureByPropFileEx (load from property file, extended position information)
- configureByPropsEx (load from property map, extended position information)
- configureByFile (load from file, nested/permissive JSON format)
- configureByStr (load from string, nested/permissive JSON format)
- configure (load from configuration object)

TODO: refine API, we don't need so many functions

### Configuring Log Level

The log level configuration items follow the following syntax:

    <log-level-string><propagation-spec>

The log level string is any one of the following:

    FATAL
    ERROR
    WARN
    NOTICE
    INFO
    TRACE
    DEBUG
    DIAG

The propagation specification could be empty, or any one of the following:

    * (asterisk sign): specifies to propagate the log level as is to all descendant log sources
    + (plus sign): specifies permissive (loose) log level propagation
    - (minus sign): specifies restrictive log level propagation

So, continuing the example above, we can define that the entire core module has INFO log level,  
but the files sub-module will have TRACE log level:

    core.log_level = INFO*
    core.files.log_level = TRACE*

Now suppose that for a short while we would like to investigate some bug in the core module,  
but we would like to avoid setting the files sub-module level to DEBUG, since it is really noisy,  
and it is not related to the bug we are investigating. The simplest way to do this is as follows:

    core.log_level = DEBUG+
    core.files.log_level = TRACE-

The above configuration ensures all core log sources have at least DEBUG log level (if any log source  
had a more permissive log level, then its log level is kept intact), but after that the cores.files  
module log level is restricted to TRACE, including all sub-packages.  
Pay attention that order matters here.


### Configuring Log Targets

As mentioned above, log targets can be configured using properties.  
The following syntax is supported:

    sys://stdout - add log target to standard output stream
    sys://stderr - add log target to standard error stream
    sys://syslog - add log target to syslog (or Windows event log, when running on Windows)
    file://path - add regular log target
    file://path?file_segment_size_mb=<file-segment-size-mb> - add segmented file log target
    db://provider?conn_string=<url>&insert_query=<insert-query>...
    msgq://provider?... (see example below for more details)
    rpc://provider?rpc_server=<host:port>&rpc_call=<function-name>(<param-list>)

Log targets may be assigned a name for identification, if further special configuration is required.  
Target name may be specified by the 'name' parameter, as follows:

    file://path?name=file-logger

Next, the log target may be located as follows:

    elog::ELogTarget* logTarget = elog::getLogTarget("file-logger");

### Individual Log Target Configuration

The log line format and the log level of each target can be configured separately. For instance:

    log_target = sys://syslog?log_level=FATAL&log_format=${level:6} ${prog} ${pid} [${tid}] <${src}> ${msg}
    log_target = sys://stderr?log_level=ERROR&log_format=***ERROR*** ${time} ${level:6} ${msg}

Pay attention that the rest of the log targets will use the global log level and line format configuration.

### Configuring Flush Policy

Log targets can be assigned a flush policy (file targets by default flush after every message).
The flush policy can be configured as follows:

    flush_policy=none|immediate|never|count|size|time

Where each value designates a different policy:

- none: no flushing is specified, behavior is determined by each log target separately
- immediate: orders to flush log target after each log message sending
- never: orders to avoid flushing the log target altogether, except for during shutdown
- count: Order to flush log target after a configured amount of log messages has been sent
- size: Order to flush log target after a configured amount of bytes of log messages has been sent
- time: Order to flush log target after a configured amount of time passed since last flush took place

The last three flush policies require the following addition parameter each respectively:

- flush_count
- flush_size_bytes
- flush_timeout_millis

Complex flush policies can be defined with the following free style syntax:

flush_policy=((count == 10) OR (time == 5000))

This is interpreted as flushing either when 10 log messages are accumulated or when timeout from last flush exceeds 5 seconds.  

The following operators are allowed:

- AND
- OR
- NOT
- CHAIN (experimental, used for group flush moderation)

The following flush policies are recognized (can be externally extended):

- count, size, time

Only == (equals) operator is recognized in this context.  
The expression must be properly formed with parenthesis enclosing each sub-expression.

### Configuring Log Filters

Log records can be filtered, such that some log records are dropped due to configured criteria.  
This can take place on a global level or on a per-target basis.  
By default there is no special log filtering except for log level.  
Log filters can be configured inside a log target URL, or using nested form.  
Due to their tendency to be complex, it is highly recommended that free-style expressions be used:

filter=((log_module == files) OR (log_level == FATAL))

This is interpreted as: allow logging only log messages that originate from the "files" module  
or that have a FATAL log level.

The following operators are allowed:

- AND
- OR
- NOT
- CONTAINS (search for substring)
- LIKE (regular expression matching)
- <, <=, >, >=, ==, != (integer/string/time/level comparison)

The following filter names are recognized (can be externally extended):

- record_id, record_time, thread_name, log_source, log_module, log_level, log_msg
- file_name, line_number, function_name

The expression must be properly formed with parenthesis enclosing each sub-expression.

The CONTAINS operator is useful when trying to match a source file.  
It may happen that the log record contains a full path name, so matching that with == is a bit awkward.  
In this case just use CONTAINS like this:

    (file_name CONTAINS <part of source file name>)

The same applies to function name filter, since on some platforms the function name may appear with full  
argument types and parenthesis.

Pay attention that some filters may have significant performance impact, and they should normally used  
in scenarios where one is chasing a bug and trying to narrow down what is being logged.

### Compound Log Targets

When using asynchronous logging schemes, it is required to specify two log targets: the asynchronous "outer" log target, and the "inner" end log target. In order to simplify syntax, the pipe sign '|' is used as follows:

    log_target = async://deferred | file://logs/app.log?file_segment_size_mb=4

So one URL is piped into another URL. The outer URL results in a log target that is added to the global list of log targets, while the inner URL generates a private log target that is managed by the outer log target.

ELog has 3 predefined asynchronous log targets:

- Deferred (mutex, condition variable, logging thread)
- Queued (deferred + periodic wake-up due to timeout/queue-size)
- Quantum (lock free ring buffer)

The deferred log target uses a simple logging thread with a queue guarded by a mutex. The logging thread is woken up for each logged message. The deferred log target has no special parameters.

The queued log target is based on the deferred log target, but adds logic of lazier wake-up, whenever timeout passes or queue size reaches some level. The queued log target therefore uses the following mandatory parameters:

    queue_batch_size=<batch-size>
    queue_timeout_millis=<timeout-millis>

The quantum log target uses a lock free ring buffer and a CPU-tight logging thread. The quantum log target requires the following mandatory parameter to be defined:

    quantum_buffer_size=<ring-buffer-size>

All asynchronous log target may be configured with log format, log level, filter and flush policy.

Here is an example for a deferred log target that uses count flush policy and passes logged message to a segmented file log target:

    log_target = async://deferred?flush_policy=count&flush_count=4096 | file://logs/app.log?file_segment_size_mb=4

### Configuring Database Log Targets

Here is another example for connecting to a MySQL database behind a queued log target:

    log_target = async://queued?queue_batch_size=1024&queue_timeout_millis=100 | 
        db://mysql?conn_string=tcp://127.0.0.1&db=test&user=root&passwd=root&
        insert_query=INSERT INTO log_records values(${time}, ${level}, ${host}, ${user}, ${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})

Pay attention to the outer log target:
- scheme: async
- path: queued (denoting queued asynchronous log target)
- queue_batch_size: Triggers background thread to wake up when log message queue reaches this size
- queue_timeout_millis: Triggers background thread to wake up each time this timeout passes

Pay attention to the inner log target:

- scheme: db
- path: mysql (designates the database type)
- conn_string: The connection string (url in MySQL terminology)
- db: The MySQL db schema being connected to
- user: The user name used to connect to the database
- passwd: The password used to connect to the database (for security put the configuration file in a directory with restricted access)
- insert_query: The query used to insert a log record into the database

When using the db scheme, the conn_string and insert_query components are mandatory.
The following parameters are optional for database target configuration:

- db_thread_model: Specified how the database should be accessed by multiple threads concurrently. Possible values:
    - none: No thread-model is in use.  
        User code is responsible for managing multi-threaded access to database log target.
    - lock: A single lock is used to serialize all access to db log target.  
        This is thread-safe but will not scale well, and may be suitable for simple multi-threaded scenarios.
    - conn-per-thread: Each thread is allocated a separate connection, and no lock is used.
        This is a thread-safe and scalable.
- db_max_threads: When specifying db_thread_model=conn-per-thread it is possible also to configure the maximum  
    number of threads expected to concurrently send log messages to the database log target.  
    If not specified, then a default value of 4096 is used.
- db_reconnect_timeout_millis: When using database log target, a background thread is used to reconnect to the  
    database after disconnect. This value determines the timeout in milliseconds between any two consecutive reconnect attempts.

Additional required components may differ from one database to another.

### Connecting to PostgreSQL

Here are the required parameters for connecting to PostgreSQL:

    db://postgresql?conn_string=localhost&port=5432&db=mydb&user=oren&passwd=1234&insert_query=INSERT INTO log_records values(${rid}, ${time}, ${level}, ${host}, ${user}, ${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})

Here are the relevant components:

    - scheme: db
    - path: postgresql
    - conn_string: simply denotes the host name/ip.
    - port: the server port (note, unlike MySQL, this is passed separately, and not as part of the connection string)
    - user: The user name used to connect to the database
    - passwd: The password used to connect to the database (for security put the configuration file in a directory with restricted access)
    - insert_query: The query used to insert a log record into the database

In this example there is no compound log target specification.

### Connecting to SQLite

Following is a sample configuration for SQLite connector:

    db://sqlite?conn_string=wal.db&insert_query=INSERT INTO log_records values(${rid}, ${time}, ${level}, ${host}, ${user}, ${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})

Here are the relevant components:

    - scheme: db
    - path: sqlite
    - conn_string: denotes the path to the DB file on disk.
    - insert_query: The query used to insert a log record into the database

### Connecting to MySQL (experimental)

Following is a sample configuration for MySQL connector:

    db://mysql?conn_string=tcp://127.0.0.1&db=test&user=root&passwd=root&insert_query=INSERT INTO log_records values(${time}, ${level}, ${host}, ${user}, ${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})

Here are the relevant components:

    - scheme: db
    - path: mysql
    - conn_string: denotes the server address (host and port)
    - db: denotes the database name
    - user: the user name used to login to the database
    - passwd: the password used to login to the database
    - insert_query: The query used to insert a log record into the database

### Connecting to Kafka Topic

The following example shows how to connect to a Kafka topic:

    log_target = msgq://kafka?kafka_bootstrap_servers=localhost:9092&msgq_topic=log_records

The kafka log target uses the 'msgq' scheme, and 'kafka' provider.  
Two mandatory parameters are expected: 'kafka_bootstrap_servers' and 'msgq_topic'.  
Optionally, a partition id may be passed as well with the syntax 'partition={id}, and also msgq_headers (see below).  
Pay attention that in the example above, the global log format is used as the message payload.  
If a more specialized message pay load is required, then add a 'log_format' parameter to the log target configuration.

In case a flush policy is used, then the flush timeouts, both during regular flush, and during shutdown flush,  
can be configured via 'kafka_flush_timeout_millis' and 'kafka_shutdown_flush_timeout_millis' respectively:

    log_target = msgq://kafka?kafka_bootstrap_servers=localhost:9092&msgq_topic=log_records&kafka_flush_timeout_millis=50&flush_policy=immediate

Pay attention that the flush_policy parameter enforces kafka message flush after each message is being produced.  
Different flush policies can be applied, as explained above.

In case message msgq_headers are to be passed as well, the 'msgq_headers' parameter should be used as a CSV property list:

    log_target = msgq://kafka?kafka_bootstrap_servers=localhost:9092&msgq_topic=log_records&msgq_headers={rid=${rid}, time=${time}, level=${level}, host=${host}, user=${user}, prog=${prog}, pid=${pid}, tid=${tid}, tname=${tname}, file=${file}, line=${line}, func=${func}, mod=${mod}, src=${src}, msg=${msg}}

### Connecting to gRPC Endpoint

The following example shows how to connect to a gRPC endpoint using Unary gRPC client:

    log_target = "rpc://grpc?rpc_server=localhost:5051&rpc_call=dummy(${rid}, ${time}, ${level}, ${msg})

Points to note:

- scheme being used is 'rpc'
- path is 'grpc'
- the gRPC server address is specified by 'rpc_server' (no typo here, there is no initial 'g')
- the gRPC invocation is configured by 'rpc_call' (again, no typo here)

The gRPC client uses a protocol predefined by ELog, and can be found at src/elog/proto/elog.proto.  
This protocol file defines a message with member variables corresponding to all log record fields.  
It also defines a service sending and streaming log messages.

The gRPC client may be configured to use various communications schemes via the 'grpc_client_mode' parameter, which can take the following values

- unary (the default if not specified)
- stream
- async
- async_callback_unary
- async_callback_stream

When using grpc_client_mode=async_callback_stream, an additional integer parameter is required:

- grpc_max_inflight_calls

Benchmark shows (as well as gRPC recommendations) that async_callback_stream has the best performance.

The following optional parameters are also recognized:

- grpc_deadline_timeout_millis
- grpc_server_ca_path
- grpc_client_ca_path
- grpc_client_key_path

In order to use a different proprietary protocol, more work needs to be done.  
Please refer to [this]() guide for more details.

### Connecting to Grafana Loki

The following example demonstrates how to configure a Grafana Loki log target:

    log_target = mon://grafana?mode=json&
        loki_address=http://localhost:3100&
        labels={app: ${app}}&
        log_line_metadata={src: ${src}, tname: ${tname}}&
        flush_policy=count?flush_count=10

The Grafana Loki log target uses the 'mon' scheme (for monitoring tools), and 'json' mode.  
This mode means that data is sent in plain json, without compressions.  
(Future versions may support gRPC content and snappy compression, according to need).    
Two mandatory parameters are expected: 'log_address' and 'labels'.  
Optionally, log line metadata may be attached to log lines reported to Grafana Loki.  
The syntax for both labels and log line metadata is similar, a property map, which is a permissive form of JSON.  
Pay attention that in the example above, the global log format is used as the message payload.  
If a more specialized message pay load is required, then add a 'log_format' parameter to the log target configuration.

Pay attention that since the Grafana Loki log target uses HTTP communication, it can specify [common HTTP timeouts](#common-http-timeouts) in the configuration string URL.

Since HTTP client is being used, the flush policy actually determines when an aggregated payload will be sent to Loki. Be advised that HTTP message sending may be slow, so it is recommended to consider putting the Grafana Loki log target behind an asynchronous log target, and configure periodic flushing according to payload size limits and/or timeout considerations.

### Connecting to Datadog

The following example demonstrates how to configure a Datadog log target:

    log_target = mon://address=https://http-intake.logs.datadoghq.eu&
        api_key=670d35294fa0d39af61180a42c6ef7db&"
        source=elog&
        service=elog_bench&
        flush_policy=count&
        flush_count=5&
        tags={log_source=${src}, module=${mod}, file=${file}, line=${line}}&
        stack_trace=yes&
        compress=yes

The Datadog log target uses the 'mon' scheme (for monitoring tools).  
Two mandatory parameters are expected: 'address', 'api_key'.

The following optional parameters map be specified: 'source', 'service', 'tags', 'stack_trace' and 'compress'.
The source and service are static names that may be attached to each log line report.  
The tags parameter is a property set (in curly braces, comma separated), that defines dynamic properties that  
can be associated with each log line report.  
The syntax for tags parameter is a property map, which is a permissive form of JSON object.  

The stack_trace parameter denotes whether to associate the current call stack with the log line report.  
This requires usage of [dbgutil](#https://github.com/oa-333/dbgutil), so ELog must be built with ELOG_ENABLE_STACK_TRACE=ON.  
Pay attention that resolving full stack trace with file and line information is a performance hit,  
so this option mostly suites FATAL and crash reports. If mixed reports are required (i.e. also ERROR level),  
then consider setting up two Datadog log targets, one for ERROR reports without stack trace, and one for FATAL  
and crash reports, with stack trace.  

The compress parameter denotes whether the HTTP message payload should be compressed (gzip).

Pay attention that in the example above, the global log format is used as the message payload.  
If a more specialized message pay load is required, then add a 'log_format' parameter to the log target configuration.

The datadog log target uses HTTP communication, so it can specify [common HTTP timeouts](#common-http-timeouts) in the configuration string URL.

Since HTTP client is being used, the flush policy actually determines when an aggregated payload will be sent to Datadog server. Be advised that HTTP message sending may be slow, so it is recommended to consider putting the Datadog log target behind an asynchronous log target, and configure periodic flushing according to payload size limits and/or timeout considerations.

### Connecting to Sentry

The following example demonstrates how to configure a Sentry log target:
    log_target = mon://sentry?
        dsn=https://39a372c6d69afb1af1e209d91f9730c5@o1208530129237538.ingest.de.sentry.io/1208530129237538&
        db_path=.sentry-native&
        release=native@1.0&
        env=staging&
        handler_path=vcpkg_installed\\x64-windows\\tools\\sentry-native\\crashpad_handler.exe&
        flush_policy=immediate&
        debug=true&
        logger_level=DEBUG&
        tags={log_source=${src}, module=${mod}, file=${file}, line=${line}}&
        stack_trace=yes&
        context={app=${app}, os=${os_name}, ver=${os_ver}}&
        context_title=Env Details

The Sentry log target uses the 'mon' scheme (for monitoring tools).  
Four mandatory parameters are expected: 'dsn', 'db_path', 'release', 'env'.  
The dsn denotes the HTTP address and ingestion endpoint used by sentry and is mandatory.  
The dsn may also be provided via the environment variable SENTRY_DSN, in which case it is not required to specify DSN in the URL (actually it will be ignored, so the environment variable SENTRY_DSN overrides the value specified in the configuration).

The db_path is a relative file system path used by sentry for local data management.  
The env parameter is static text used to distinguish between monitored environments (e.g. staging/prod).  
Also an optional distribution parameter (named 'dist') can be used to further identify the monitored application.

The handler_path is also required on Windows platforms, and should point to the sentry-native installation path. In normal cmake builds, this can be found relative to the build directory in the path specified above.

Although not specified above, a proxy server may be specified if needed to communicate with Sentry server, using the following syntax:  

    proxy=<proxy-address:port>

In case security certificates are being used the local certificates folder path may be specified as follows:

    ca_certs_path=<file-system-folder-path>

Sentry maintains a report context, which is a static description that is attached to each log report.  
The user may configure the title attached to the context with the 'context_title' parameter, and the context data may be given through the 'context' parameter, which is a property map, as in other monitoring tools configuration URLs. If 'context' was specified, then it is required also to specify 'context_title'.

Dynamic attributes may be attached to each log report using the 'tags' parameter, which is a property map that may refer to log record fields. 

Pay attention that when the current thread name is set properly, via elog::setCurrentThreadName(), then this name is automatically also being sent to Sentry with each log report.

The stack_trace parameter denotes whether to associate the current call stack with the log line report.  
This requires usage of [dbgutil](#https://github.com/oa-333/dbgutil), so ELog must be built with ELOG_ENABLE_STACK_TRACE=ON.  
Pay attention that resolving full stack trace with file and line information is a performance hit,  
so this option mostly suites FATAL and crash reports. If mixed reports are required (i.e. also ERROR level),  
then consider setting up two Sentry log targets, one for ERROR reports without stack trace, and one for FATAL  
and crash reports, with stack trace.  

Optional flush and shutdown timeout parameters may be specified with 'flush_timeout_millis' and 'shutdown_timeout_millis'.

Finally, in order to debug issues with the Sentry log target's connectivity with the Sentry server, the 'debug' parameter may be used, set to true. This will have Sentry native SDK log/trace messages to be redirected to ELog, and printed to the standard error stream via specialized logger and log source, using the log source name "elog.sentry".  
The 'logger_level' parameter controls the level of Sentry native SDK message being printed out.

Pay attention that in the example above, the global log format is used as the message payload.  
If a more specialized message pay load is required, then add a 'log_format' parameter to the log target configuration.

// TODO: add adding new log target type and new schema handler
// TODO: address problem of deallocating extended types (requires destroy function, which must be overridden, and we might provide a utility macro for that) - they must be deallocated at the same module where they were allocated
// conversely this can be solved by the factory class - let it also delete (because being a macro it is defined
// in the user's module)

### Common HTTP Timeouts

Several log targets (currently Grafana Loki and Datadog) use HTTP client for their communication with an end server.  
For these kind of log targets the following common configuration properties can be specified in the configuration string URL of the log target:

- connect_timeout_millis
- write_timeout_millis
- read_timeout_millis
- resend_timeout_millis
- backlog_limit_bytes
- shutdown_timeout_millis

The connect timeout is used to determine when a server connection setup failed.  
It may be that the server is not down, but setting up HTTP connection takes too much time.  
The write timeout is used to determine whether message sending failed, and therefore requires to be resent later.  
The read timeout is used to determine when a server response has timed out, and therefore resend will take place.  
The common HTTP client makes provision for resending messages periodically via a background thread,  
in case message sending failed.  
This backlog configuration parameter controls the size of the backlog used for failed messages pending to be resent.  
The resend timeout controls the period that the background thread waits until it attempts again to resend failed messages.  
Finally, the shutdown configuration parameter specifies the amount of time spent in last attempt to resend all pending messages. This includes the final message sent during last flush when the log target is being stopped.

The following table summarizes the default values for these configuration parameters:

| Parameter  | Default Value |
| ------------- | ------------- |
| connect_timeout_millis  | 200  |
| write_timeout_millis  | 50  |
| read_timeout_millis  | 100  |
| resend_timeout_millis  | 5000  |
| backlog_limit_bytes  | 1048576 (1MB)  |
| shutdown_timeout_millis  | 5000  |

### Nested Specification Style

As log target URLs tend to be rather complex in some cases, a different specification style was devised, namely nested style.
For instance, let's take the last example (sending log messages to Kafka topic with headers), but let's put it behind a deferred
log target. The result looks like this:

    log_target = async://deferred?flush_policy=count&flush_count=4096 | msgq://kafka?kafka_bootstrap_servers=localhost:9092&msgq_topic=log_records&msgq_headers={rid=${rid}, time=${time}, level=${level}, host=${host}, user=${user}, prog=${prog}, pid=${pid}, tid=${tid}, tname=${tname}, file=${file}, line=${line}, func=${func}, mod=${mod}, src=${src}, msg=${msg}}

This is rather not pleasing to the eye, and probably prone to error.  
ELog allows a more JSON-like syntax for msgq_headers as follows (colon instead of equals sign):

    msgq_headers={rid: ${rid}, time: ${time}, level: ${level}, host: ${host}, user: ${user}, prog: ${prog}, pid: ${pid}, tid: ${tid}, tname: ${tname}, file: ${file}, line: ${line}, func: ${func}, mod: ${mod}, src: ${src}, msg: ${msg}}

Instead of a long URL, here is how it may look using the nested specification style:

    log_target = {
        scheme = async,
        type = deferred,
        log_target = {
            scheme = msgq,
            type = kafka,
            kafka_bootstrap_server = localhost:9092,
            msgq_topic = log_records,
            msgq_headers = {
                rid=${rid}, 
                time=${time}, 
                level=${level}, 
                host=${host}, 
                user=${user}, 
                prog=${prog}, 
                pid=${pid}, 
                tid=${tid}, 
                tname=${tname}, 
                file=${file}, 
                line=${line}, 
                func=${func}, 
                mod=${mod}, 
                src=${src}, 
                msg=${msg}
            }
        }
    }

With this type of log target specification, the log target nesting is much clearer.  
Besides clarity, the nested specification style allows doing things which are not possible with URL style.  
For instance, suppose we have a syslog target configured with log level ERROR, but some module, say core.files, reports many errors due to disk failure, and causes log flooding in syslog. As a result, we would like to restrict the rate of this specific log source to syslog (assume we don't care about other log targets) to 10 messages per second, but we would not like to restrict other log sources. This can be achieved in configuration with compound log filters as follows:

    log_target = {
        scheme = sys,
        path = syslog,
        log_level = ERROR,
        filter = OR,
        filter_args = [
            {
                filter = NOT,
                filter_args = {
                    filter = log_source_filter,
                    log_source = core.files
                }
            },
            {
                filter = rate_limiter,
                max_msg_per_sec = 10
            }
        ]
    }

The logic this configuration uses is as follows: allow messages to pass if the log source is NOT core.files OR (i.e. the log source is core.files) the rate does not exceed 10 messages per second. In any case the log level is set at ERROR for syslog.  
This example is a bit complex, but it illustrates the flexibility nested configuration has.

Due to these complexities, the expression free-style flush policy specification was devised, and the same goal can be achieved as follows:

    log_target = sys://syslog?log_level=ERROR&filter=((log_source != core.files) OR (max_msg_per_sec == 10))

The same syntax may be used with the nested specification:

    log_target = {
        scheme = sys,
        path = syslog,
        log_level = ERROR,
        filter = ((log_source != core.files) OR (max_msg_per_sec == 10))
    }

In case of simply limiting the rate for all incoming messages, the configuration becomes much simpler:

    log_target = {
        scheme = sys,
        path = syslog,
        log_level = ERROR,
        filter = rate_limiter,
        max_msg_per_sec = 10
    }

Or in case of filter expression:

    log_target = {
        scheme = sys,
        path = syslog,
        log_level = ERROR,
        filter = (max_msg_per_sec == 10)
    }

### Terminal Text Formatting

When logging to terminal (stdout or stderr), it is possible to specify special formatting for the log line.  
For instance, consider the following log target specification:

    "sys://stderr?log_format=${time:text=faint} ${level:6:fg-color=green:bg-color=blue} "
        "[${tid:text=italic}] ${src:text=underline:fg-color=bright-red} "
        "${msg:text=cross-out,blink-rapid:fg-color=#993983}";

Here is the breakdown of this configuration URL:

- The time part is in faint (opposite of bold) font
- Log level color is green, and background is blue
- Thread id field is in italic font
- Log source name is underlined, and having bright red color
- The message has cross-out (strike-through, not supported on all terminals)
- The message blinks rapidly and has magenta like color specified by RGB value #993983

Following is the output in VSCode console:

![plot](./src/doc/image1.png)

Conditional formatting is supported as well. For instance:

    "sys://stderr?log_format=${time:text=faint} "
        "${if: (log_level == INFO): ${fmt:begin-fg-color=green}: ${fmt:begin-fg-color=red}}"
        "${level:6}${fmt:default} "
        "[${tid:text=italic}] ${src:text=underline:fg-color=bright-red} "
        "${msg:text=cross-out,blink-rapid:fg-color=#993983}";

In this example, the log level color is green in case the log level is INFO, otherwise it is red.
This is how it looks on VSCode console:

![plot](./src/doc/image2.png)

Please note that:

- Conditional formatting is specified with ${if ...} special syntax
- Formatting uses ${fmt ...} special syntax
- Formatting is reset to default with ${fmt:default}

Following is a formal specification of the terminal formatting syntax.

### Terminal Formatting Formal Syntax Specification

The extended reference token syntax follows the general form:

    ${<field-name>:<colon separated list of format specifiers>}

The field-name could be any one of the [predefined reference token names](#log-record-field-reference-tokens).  
It could also be \${fmt} which exists only for formatting purposes (no text emitted, only ANSI C escape sequences).  
Additional field names are for conditional formatting: \${if}, \${switch}, \${expr-switch}.

The format specifier list could be any of the following, and may be repeated (overriding previous specification):

    justify-left = <justify-count>
    justify-right = <justify-count>
    fg-color = <color-specification>
    bg-color = <color-specification>
    font = <font-specification>
    <positive integer value for left justification>
    <negative integer value for right justification>

The color specification may be specified in any of the following ways:

- Simple color: black, red, green, yellow, blue, magenta, cyan, white
- Bright color may be specified with "bright" prefix: bright-red, bright-blue, etc.
- VGA color: vga#RRGGBB where each pair is a hexadecimal value specifying component intensity, and cannot exceed 1F
- Gray-scale color: grey#<0-23>, or gray#<0-23>, where the value in the range [0-23] specifies the intensity, zero being dark, and 23 being light
- SVGA RGB color: #RRGGBB where each pair is a hexadecimal value specifying component intensity
- Font type: bold/faint/normal, italic/no-italic, underline/no-underline, cross-out/no-cross-out (strike-through/no-strike-through), slow-blink/rapid-blink/no-blink

Note that strike-through is a synonym for cross-out, although it has not been possible to observe this formatting in effect.

#### Spanning Formatting over Several Fields

When specifying formatting within a field reference token, the formatting applies only to that field, and formatting is automatically reset to default for the rest of the formatted log message.  
If it is desired to span formatting over a few fields, then the "begin" prefix may be used, terminated with a default/reset specifier:

    ${time:font=begin-bold} [${level:6:font=default}]

In this example, both time and level have bold font.
It is possible to use pure formatting reference token instead:

    ${fmt:font=begin-bold}${time} [${level:6}]${fmt:default}

Here is where usage of normal, no-italic, no-underline, etc. makes sense:

    ${fmt:font=begin-bold,begin-italic}${time} [${level:6}]${fmt:normal}

When 'normal' (resets bold/faint specification) is specified, 'italic' is still in effect.

#### Conditional Formatting Syntax

Conditional formatting takes place in three main forms:

    
    ${if: (filter-pred): ${name:<true format>} [: ${name:< false format>}] }
    ${switch: (expr): ${case: (expr) : ${fmt:<format>}}, ..., ${default:${fmt: <format>}} }
    ${expr-switch: ${case: (filter-pred) : ${fmt:<format>}}, ..., ${default:${fmt: <format>}} }

TODO: finish this after documenting filter predicates.

## Extending The Library

All aspects of the ELog library are extendible, such that the extension may be also loadable from configuration.  
In the following sections it will be explained how to extend each aspect of the ELog library.

### Extending The Formatting Scheme

The log line format may reference special tokens which point to the current log record being logged.  
In order to extend the formatting scheme with new reference tokens, ELogFieldSelector should be derived, implemented, and registered. The actual token value can be resolved to anything, and not necessarily be tied to the lgo record fields.

With that understanding in hand, suppose we would like to add a new special token that reflects current system state.  
Now let's assume there are global functions for that:

    enum SystemState { IDLE, NORMAL, URGENT, OVERLOADED };

    extern SystemState getSystemState();

    extern const char* systemStateToString(SystemState state);

    extern bool systemStateFromString(const char* stateStr, SystemState& state);

So the first step is to derive from ELogFieldSelector:

    class SystemStateFieldSelector : public ELogFieldSelector {
    public:
        SystemStateFieldSelector(const ELogFieldSpec& fieldSpec)
             : ELogFieldSelector(ELogFieldType::FT_TEXT, fieldSpec) {}
        ~SystemStateFieldSelector() override {}

        void selectField(const ELogRecord& record, ELogFieldReceptor* receptor) final {
            // obtain the system state and convert to string form
            SystemState currentSysState = getSystemState();
            const char* sysStateStr = systemStateToString(currentSysState);
            
            // pass the system state string to the receptor, with text justify value
            receptor->receiveStringField(getTypeId(), sysStateStr, m_fieldSpec);
        }

    private:
        ELOG_DECLARE_FIELD_SELECTOR(SystemStateFieldSelector, sys_state);
    };

Important points to note:

- The constructor should receive one parameter of type const ELogFieldSpec&, and pass it to the parent class
- The field selector should specify the field type (in our example it is FT_TEXT which is string type)
- in the selectField() virtual method, the system test should be passed to the receptor alongside with the field selector's type id and configured field specification

The new field selector class is being registered in the field selector global registry using the helper macro:

    ELOG_DECLARE_FIELD_SELECTOR(SystemStateFieldSelector, sys_state);

This helper macro requires a companion macro to be put in the source file (actually any source file):

    ELOG_IMPLEMENT_FIELD_SELECTOR(MyFieldSelector)

With this in place, the log line configuration string may now look like this:

    ${time} ${level:6} [${tid:5}] <<${sys_state}>> ${msg}

So each log line will include the system state within <<>> parenthesis.

For more examples refer to elog_field_selector.h and elog_field_selector.cpp source files.

### Adding New Log Filter Types

It is possible to simply derive from ELogFilter and implement the required filter logic,  
but in order to make the extended log filter loadable from configuration further steps are required.

First it is required to determine whether the log filter is comparable, i.e. is it expected to be used  
in conjunction with some comparison operation, for instance:

    filter_name <= some_value

If so, then the extended filter should derive from ELogCmpFilter, otherwise it should derive directly from ELogFilter.  
Since most filters are comparative, we will relate here only to the first case.  
Following the [previous example](#extending-the-formatting-scheme), we will add a new new filter that compares  
the current system state to some given value:

    class SystemStateFilter : public ELogCmpFilter {
    public:
        SystemStateFilter(SystemState sysState, ELogCmpOp cmpOp = ELogCmpOp::CMP_OP_EQ)
            : ELogCmpFilter(cmpOp), m_sysState(sysState) {}
        ~SystemStateFilter() final {}

        /** @brief Loads filter from configuration. */
        bool load(const ELogConfigMapNode* filterCfg) final;

        /** @brief Loads filter from a free-style predicate-like parsed expression. */
        bool loadExpr(const ELogExpression* expr) final;

        /**
        * @brief Filters a log record.
        * @param logRecord The log record to filter.
        * @return true If the log record is to be logged.
        * @return false If the log record is to be discarded.
        */
        bool filterLogRecord(const ELogRecord& logRecord) final;

    private:
        SystemState m_sysState;

        ELOG_DECLARE_FILTER(SystemStateFilter, sys_state);
    };

A few points to note:

- The filter must pass to parent class what kind of comparison operator it uses (this will be overridden later during load phase)
- In order to be loadable from configuration, the new filter needs to declare its reference name (sys_state) using the helper macro ELOG_DECLARE_FILTER().

The implementation is also straightforward, and follows the implementation of other predefined log filters:

    // register filter in global filter registry
    ELOG_IMPLEMENT_FILTER(SystemStateFilter)

    // load from configuration file (nested style)
    bool SystemStateFilter::load(const ELogConfigMapNode* filterCfg) {
        std::string sysState;
        if (!loadStringFilter(filterCfg, "sys_state", "system state", sysState)) {
            // full error message already logged by loadStringFilter()
            return false;
        }
        if (!systemStateFromString(sysState.c_str(), m_sysState)) {
            // systemStateFromString() should have already logged error message
            return false;
        }
        return true;
    }

    // load from configuration (expression style)
    bool SystemStateFilter::loadExpr(const ELogExpression* expr) {
        std::string sysState;
        if (!loadStringFilter(expr, "system state", sysState)) {
            return false;
        }
        if (!systemStateFromString(sysState.c_str(), m_sysState)) {
            // systemStateFromString() should have already logged error message
            return false;
        }
        return true;
    }

    bool SystemStateFilter::filterLogRecord(const ELogRecord& logRecord) {
        return compareInt(m_cmpOp, (int)getSystemState(), (int)m_sysState);
    }

A few points to note:

- The filter must be registered in the global filter registry with the helper macro ELOG_IMPLEMENT_FILTER()
- When loading from configuration or expression one can use the helper method of the parent class loadStringFilter(), which extracts the filter value from the configuration/expression
- The configured system state is first loaded as string, then converted to enumerated value and saved in a member variable
- When filtering a log record, the helper function compareInt is used to compare enumerated values. If not applicable then any logic may be implemented here
- When filtering a log record the current system state is always the left-hand side argument for comparison, while the value loaded from configuration is always the right-hand side argument.

So with this in hand, we can now filter based on system state as follows:

    (sys_state >= URGENT)

    ((sys_state >= NORMAL) AND (sys_state <= URGENT))

## Adding New Flush Policy Types

In order to add a new flush policy type, it is required to derive from ELogFilter and implement the flush policy logic.  
In order to make the extended flush policy loadable from configuration further steps are required.

First some basic concepts must be explained.  
A flush policy controls two aspects of flushing lgo messages:

- Whether a flush should take place (the controlling policy)
- How should a flush should take place, and whether it should be moderated (the moderating policy)

Most flush policies are only controlling policies, except for the experimental ELogGroupFlushPolicy,  
which implements group flush. 

In addition, a flush policy may be active or passive.  
Active flush policies may determine by themselves, in their own time (i.e. via some background thread)  
when to execute flush. One such example is the time flush policy.  
Passive flush policies make such a decision only when called by shouldFlush().

With this understanding we now can move one to explaining how to add a new flush policy type.  
Continuing with previous examples, let's assume we would like to add a flush policy that is sensitive to the  
current system state, such that in URGENT and OVERLOADED states, flush will take places much less than usual.  
This is not a trivial flush policy. For simpler examples please refer to elog_flush_policy.h/cpp.
So we begin with this declaration:

    class SystemStateFlushPolicy : public ELogFlushPolicy {
    public:
        SystemStateFlushPolicy(uint64_t normalLimitBytes = 0, uint64_t urgentLimitBytes = 0, 
            uint64_t overloadedLimitBytes = 0)
            : m_normalLimitBytes(normalLimitBytes), 
              m_urgentLimitBytes(urgentLimitBytes),
              m_overloadedLimitBytes(overloadedLimitBytes) {}
        ~SystemStateFlushPolicy() override {}

        /** @brief Loads flush policy from configuration. */
        bool load(const ELogConfigMapNode* flushPolicyCfg) final;

        /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
        bool loadExpr(const ELogExpression* expr) final;

        bool shouldFlush(uint32_t msgSizeBytes) final;

    private:
        uint64_t m_normalLimitBytes;
        uint64_t m_urgentLimitBytes;
        uint64_t m_overloadedLimitBytes;

        // use atomic variable, access may be multi-threaded
        std::atomic<uint64_t> m_currentLogSizeBytes;

        ELOG_DECLARE_FLUSH_POLICY(SystemStateFlushPolicy, sys_state);
    };

This is no different than adding a new filter type or adding a new token reference.  
The flush logic is also straightforward, but the loading part is a bit tricky.  
First let's take a look at the flush logic:

    bool SystemStateFlushPolicy::shouldFlush(uint32_t msgSizeBytes) {
        // compute previous and current amount of logged bytes (thread-safe)
        uint64_t prevSizeBytes =
            m_currentLogSizeBytes.fetch_add(msgSizeBytes, std::memory_order_relaxed);
        uint64_t currSizeBytes = prevSizeBytes + msgSizeBytes;

        // decide limit according to system state
        uint64_t limitBytes = 0;
        switch (getSystemState()) {
            case OVERLOADED:
                limitBytes = m_overloadedLimitBytes;
                break;

            case URGENT:
                limitBytes = m_urgentLimitBytes;

            case NORMAL:
            case IDLE:
            default:
                limitBytes = m_normalLimitBytes;
        }

        // now decide whether flush should take place (we flush if we crossed a "flush boundary")
        return (currSizeBytes / limitBytes) > (prevSizeBytes / limitBytes);
    }

So we see here that the flush frequency is adjusted to the current system state.  
Now let's address the configuration aspect, and we will start with free-style expression.  
The flush policy is not something simple as "(count == 10)".  
We could model it as an if-else statement, but that would be too complex.  
Instead, we can model it as something similar to a function call:

    sys_state(normal_size_bytes == 1024, urgent_size_bytes == 4096, overloaded_size_bytes == 8192)

Actually, a more readable form would be:

    sys_state(normal_size_bytes: 1024, urgent_size_bytes: 4096, overloaded_size_bytes: 8192)

Both forms of syntax above are supported by ELog.  
In a similar manner, when using string URL or nested-style we can require the following properties:

    - normal_size_bytes
    - urgent_size_bytes
    - overloaded_size_bytes

With this we can now implement the loading functions:

    bool SystemStateFlushPolicy::load(const ELogConfigMapNode* flushPolicyCfg) {
        // this takes care both of string URL and nested style configuration
        return loadIntFlushPolicy(flushPolicyCfg, "sys_state", "normal_size_bytes", m_normalLimitBytes) &&
            loadIntFlushPolicy(flushPolicyCfg, "sys_state", "urgent_size_bytes", m_urgentLimitBytes) &&
            loadIntFlushPolicy(flushPolicyCfg, "sys_state", "overloaded_size_bytes", m_overloadedLimitBytes);
    }

    bool SystemStateFlushPolicy::loadExpr(const ELogExpression* expr) {
        // first make sure we have a function expression
        if (expr->m_type != ET_FUNC_EXPR) {
            return false;
        }
        ELogFunctionExpression* funcExpr = (ELogFunctionExpression*)expr;

        // make sure we have exactly 3 parameters
        if (funcExpr->m_expressions.size() != 3) {
            return false;
        }

        // finally, load all sub-expressions
        // for simplicity, we assume strict order
        return loadIntFlushPolicy(funcExpr->m_expressions[0], "sys_state", m_normalLimitBytes, "normal_size_bytes") &&
            loadIntFlushPolicy(funcExpr->m_expressions[1], "sys_state", m_urgentLimitBytes, "urgent_size_bytes") &&
            loadIntFlushPolicy(funcExpr->m_expressions[2], "sys_state", m_overloadedLimitBytes, "overloaded_size_bytes");
    }

With this in hand we can now specify in configuration:

    flush_policy=(sys_state(normal_size_bytes: 1024, urgent_size_bytes: 4096, overloaded_size_bytes: 8192))

Notice that the flush policy must be surrounded by parenthesis, so that it gets parsed as an expression.  

Finally, the flush policy needs to get registered in the global flush policy registry, so add this in the source file:

    ELOG_IMPLEMENT_FLUSH_POLICY(SystemStateFlushPolicy)

### Adding New Log Target Types

TBD

### Adding New Schema Handler Type

TBD

### Using Proprietary Protocol for gRPC Log Target

TBD

## Benchmarks

The following unofficial benchmarks results illustrate the high performance of the ELog library.  

The benchmark is divided into four parts:

- Test the impact of empty logging with private and shared logger
- Search for best configuration of each flush policy (count, size, time)
- Single-threaded comparison of all log targets (file, async file)
- Multi-threaded scaling of each tested configuration

All multi-threaded tests were conducted with each thread hammering its own private logger.
Shared logger multi-threaded tests are provided only for the quantum log target.

All tests were performed on Windows, compiling with g++ for MinGW, running under MSYSTEM console.  
All tests were conducted on commodity hardware.

Please note that all benchmarks calculate the average value and not percentiles.  
The reason for that is that collecting each sample time affected greatly the test, reducing performance up to 20%.  
Since there is no interaction with external system (at least in this benchmark), only averages are presented,  
as outliers are not expected.

### Benchmark Highlights

The following table summarizes the main KPIs of the ELog library:

| KPI | Value |
|:---|---:|
| Private Logger Latency (no log issued) | 0.2 nano-seconds |
| Shared Logger Latency (no log issued) | 1.3 nano-seconds |
| Synchronous Logging Throughput (flush each message) | 163444 Msg/Sec |
| Synchronous Logging Throughput (delayed flush) | 2.4 Million Msg/Sec |
| Asynchronous Logging Throughput* (single-threaded) | 6.4 Million Msg/Sec |
| Asynchronous Logging Latency (single-threaded) | 156 nano-seconds |

NOTE: Asynchronous logging throughput refers to the logger's capacity to push messages to the end log target, not to the throughput of disk writing.

### Empty Logging Benchmark

The first benchmark checked the impact of using the logging macros without logging (i.e. when log level does not match).  
Separate tests were conducted with a shared logger and a private logger.  
The private logger, as expected, performs slightly better (due to thread local access in shared logger).  
The benchmark test results illustrate that:

| Logger    | Throughput (Msg/Sec)  |
|:----------|----------------------:|
| Private   | 5000000000.000        |
| Shared    |  769230769.231        |

So although both exhibit very low impact (10000 messages took 2 and 13 microseconds respectively),  
the private is clearly performing better (X6.5 times faster).  
Note that the latency on the logging application is minimal: 0.2 and 1.3 nano-seconds on private and shared logger respectively. 

### Synchronous File Log Target with Count Flush Policy

Following are the benchmark test results for synchronous file log target with flush_policy=count, setting varying count values:

![plot](./src/elog_bench/png/flush_count.png)

As it can be seen, flush_count=512 and flush_count=1024 both yield the best results (around 2.4 Million messages per second), and setting a higher number does need yield any better results.  
Most probably this has to do with underlying system file buffers.  
NOTE: Doing direct/async I/O is not being considered at this time.

### Synchronous File Log Target with Size Flush Policy

Following are the benchmark test results for synchronous file log target with flush_policy=size, setting varying size values:

![plot](./src/elog_bench/png/flush_size.png)

The results of this test are rather illuminating.
First, we can conclude that setting buffer size of 64KB yields top results, peaking at around 2.2 Million messages per second.
Second, increasing the buffer size does not have any notable effect.  
Again, this is most probably related to the underlying system file buffers.

### Synchronous File Log Target with Time Flush Policy

Following are the benchmark test results for synchronous file log target with flush_policy=time, setting varying time values:

![plot](./src/elog_bench/png/flush_time.png)

The results here shows that a flush period of 500 ms and 1000ms yield the best results, but the differences are not so clear.  
This again probably has to do with underlying buffer size, and the right timing to flush them (not too early and not too late).

### Single-threaded Synchronous File Log Target Comparison

In the following bar chart, all synchronous file log target configurations are compared together:

![plot](./src/elog_bench/png/log_st.png)

This last comparison needs further explanation:

- The 'immediate' policy flushes after each log message write, and exhibits the lowest performance
- The 'never' policy simply never flushes, and serves as a baseline for other configurations
- All other synchronous logging methods exhibit the same single-threaded performance (around 950,000 messages per second)

The last 3 logging methods are asynchronous and mostly exhibit high performance as expected, but it should be noted that  
this does not relate to disk write performance, but rather to logger throughput.  
In other words, this actually illustrates the logger latency when using asynchronous log target.

Points to note:

- The deferred log target can receive 4.2 Million messages per second, that is an average latency of 238 nano-seconds per message
- The quantum log target can receive 6.4 Million messages per second, that is an average latency of 156 nano-seconds per message
- The queued log target can receive 1.6 Million messages per second, and requires further investigation

All asynchronous loggers were configured with generous buffer sizes for testing peak performance.  
In reality this measures the peak performance during a log message burst.  
So when configuring log targets to withstand the largest message burst, this is the expected performance.

### Multi-threaded Asynchronous File Log Target Comparison

This test checks the scalability of each log target, and the results are interesting:

![plot](./src/elog_bench/png/async_log.png)

As the graph depicts, the deferred and queued log targets are not scalable, since both impose a lock,  
whereas the quantum log target employs a lock-free ring buffer.  
The results show that the quantum logger is fully scalable, as much as system resources allow.  
In addition, when using a shared logger (for instance, in a code section that is concurrently accessed by many threads), the quantum logger is still scalable, though slightly lagging behind the private logger performance.