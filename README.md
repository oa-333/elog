# ELog Logging Package

The ELog (stands for Error Log) is simple, lightweight, yet robust and high-performant package for application message logging in C++.  
The package was designed such that it can be extended for a broad range of use cases.

## Description

The most common use case is a utility logging library, in order to write log messages to file, but much more can be done with it.  
For instance, it can be rather easily extended to be hooked to an external message queue, while applying complex message filtering and transformations.  
This could be useful for DevOps use cases.

One more use case is when chasing a bug that requires log flooding.  
In this case, sending messages to log files may affect timing and prevents from the bug being reproduced during heavy logging.  
For such a situation there is a specialized log target (ELogQuantumTarget), which is designed to minimize the logging latency, by using a lock-free ring buffer and a designated background thread that logs batches of log messages.

Another common use case is log file segmentation (i.e. breaking log file to segments of some size).

The ELog system also allows directing log messages to several destinations, so tapping to external log analysis tools, for instance, in addition to doing regular logging to file, is also rather straightforward.

One more use case is when developing infrastructure library which requires logging, but the actual logging system
that will be used by the enclosing application is not known (and cannot be known).
In this case, the ELog can be used to log messages inside the library, and the using application may configure
the ELog system to redirect and adapt library log message to its own logging system.
This can be done actually quite easily and with much flexibility.

For more information, soo examples below

## Getting Started

In order to use the package, include "elog_system.h", which is the package facade.  
In the application code, make sure to call one of the elog::ElogSystem::initializeXXX() functions before using any of the logging macros. After this, you can use ELOG_INFO() and the rest of the macros.  
At application exit make sure to call elog::ELogSystem::terminate().

### Dependencies

The ELog system has no special dependencies.

### Installing

The package can be built and installed by running:

    make -j INSTALL_DIR=<install-path> install

Add to compiler include path:

    -I<install-path>/elog/include/elog
    
Add to linker flags:

    -L<install-path>/bin -lelog

## Help

See examples section below, and documentation in header files for more information.

## Authors

Oren Amor (oren.amor@gmail.com)

## License

This project is licensed under the Apache 2.0 License - see the LICENSE file for details.

## Examples

### Initialization and Termination

Following is a simple example of initializing and terminating the elog system:

    #include "elog_system.h"
    ...
    // import common elog definition into global name space without name space pollution
    ELOG_USING()

    int main(int argc, char* argv[]) {
        // initialize the elog system to log into file
        if (!elog::ELogSystem::initializeLogFile("./test.log")) {
            fprintf(stderr, "Failed to initialize elog\n);
            return 1;
        }

        // do application stuff
        ...

        // terminate the elog system
        elog::ELogSystem::terminate();
        return 0;
    }

In this example a segmented log file is used, with 4MB segment size:

    #include "elog_system.h"
    ...
    // import common elog definition into global name space without name space pollution
    ELOG_USING()

    #define MB (1024 * 1024)

    int main(int argc, char* argv[]) {
        // initialize the elog system to log into segmented file
        if (!elog::ELogSystem::initializeSegmentedLogFile(
                ".",        // log dir
                "test.log", // segment base name
                4 * MB)) {  // segment size
            fprintf(stderr, "Failed to initialize elog\n);
            return 1;
        }

        // do application stuff
        ELOG_INFO("App starting);
        ...

        // terminate the elog system
        elog::ELogSystem::terminate();
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

    using namespace elog;
    ELogSource* logSource = ELogSystem::defineLogSource("core");
    if (logSource == nullptr) {
        // NOTE: we use here the default logger
        ELOG_ERROR("Failed to define log source with name core");
    } else {
        ELogLogger* logger = logSource->createSharedLogger();
        ELOG_INFO_EX(logger, "Obtained a logger from source %s with id %u",
            logSource->getQualifiedName(), 
            logSource->getId());
    }

Log sources form a tree hierarchy according to their qualified name, which is separated by dots.  
The root log source has empty name, and does not have a dor following its name, so that second level  
log sources have a qualified name equal to their bare name.

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

    void setLogLevel(ELogLevel logLevel, PropagateMode propagateMode);

In particular there are 4 propagation modes:

- none: No log level propagation takes place at all.
- set: Each descendant log source inherits the log source ancestor
- restrict: Each descendent log source cannot have less restrictive log level than the log source ancestor
- loose: Each descendent log source cannot have more restrictive log level than the log source ancestor

This allows for more flexibility in configuring the log source tree.

### Configuring Log Line Format

The ELog system allows configuring log line format using a format specification string that supports special tokens.
For instance, the default log line format specification that is use dby ELog is:

    ${time} ${level:6} [${tid}] ${msg}

This format in reality gets expands to something like this:

    2025-04-08 11:40:58.807 INFO   [49108] Thread pool of 1 workers started

We see here all 4 components expanded:

- logging time
- log level (aligned to the left with width of 6 characters)
- logging thread id, enclosed with brackets
- formatted log message

The following special tokens are understood by the ELog system:

- ${rid} - the log record id.
- ${time} - the logging time.
- ${host} - the host name.
- ${user} - the logged in user.
- ${prog} - the program name.
- ${pid} - the process id.
- ${tid} - the logging thread id.
- ${level} - the log level
- ${src} - the log source of the logger (qualified name).
- ${mod} - the alternative module name associated with the source.
- ${msg} - the log message.

Tokens may contain justification number, where positive means justify to the left,  
and negative number means justify to the right. For instance: ${level:6}, ${tid:-8}.

In order to use some other formatting, the ELogFormatter may be derived.  
In addition, the list of special tokens understood by ELog may be extended by deriving from ELogFormatter  
and overriding the createFieldSelector() virtual method.  
A specialized field selector will be needed as well.

### Filtering Log Messages

By default, all messages are logged, but in some use cases, it is required to filter out some log messages.
In general, the log level may be used to control which messages are logged, and this may be controlled at
each log source. There are some occasions that more fine-grained filtering is required.
In this case ELogFilter may be derived to provide more specialized control.

Pay attention that log filtering may be applied at a global level, by calling:

    ELogSystem::setLogFilter(logFilter);


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
    ELogSegmentedFileTarget fileTarget("logs", "app.log", 4096, nullptr);

    // combine it with a deferred log target
    ELogDeferredTarget deferredTarget(&fileTarget);

    // now set it as the system log target
    ELogSystem::setLogTarget(&deferredTarget);

The ELogDeferredTarget is quite simplistic, and simple wakes up the logging thread for each log message.
The ELogQueuedTarget provides more control over this, by allowing to specify batch size and timeout,
so the logging thread wakes up either when the queued number of messages exceeds some limit, or that 
a specified timeout has passed since last time it woke up.
In the example above we can change it like this:

    // define a segmented log target with 4K segment size
    ELogSegmentedFileTarget fileTarget("logs", "app.log", 4096, nullptr);

    // combine it with a queued log target with batch size 64, and 500 milliseconds timeout
    ELogQueuedTarget queuedTarget(&fileTarget, 64, 500);

    // now set it as the system log target
    ELogSystem::setLogTarget(&queuedTarget);

The ELogQuantumTarget mentioned above can be use das follows:

    // define a segmented log target with 4K segment size
    ELogSegmentedFileTarget fileTarget("logs", "app.log", 4096, nullptr);

    // combine it with a quantum log target that can handle a burst of 1 million log messages
    ELogQuantumTarget quantumTarget(&fileTarget, 1024 * 1024);

    // now set it as the system log target
    ELogSystem::setLogTarget(&quantumTarget);

### Flush Policy

As some log targets may require flushing (as in file, or any buffered channel), a policy can be defined.
By default, all log targets flush after each message logging.
This strategy is implemented by the ELogImmediateFlushPolicy class.

It may be preferred to flush every X messages.
For this case the ELogCountFlushPolicy can be used.

Another strategy is to flush whenever the amount of logged data exceeds some size limit.
For this case the ELogSizeFlushPolicy can be used.

If a timeout is required (without using a deferred log target), the ELogTimedFlushPolicy can be used.
This is an active policy, which launches a designated thread for this purpose.

In case a combination of strategies is needed, either ELogAndFlushPolicy or ELogOrFlushPolicy can be used.

For any other specialized flush policy, derive from ELogFlushPolicy and override the shouldFlush() virtual method.

### Configuring from properties

The ELogSystem facade provides a utility function to load configuration from properties map:

    static bool configureFromProperties(const ELogPropertyMap& props,
                                        bool defineLogSources = false,
                                        bool defineMissingPath = false);

The properties map is actually a vector of pairs, since order of configuring log sources' log levels matter.  
The following properties are recognized (the rest are ignored):

- log_format: configures log line format specification
- log_level: configures global log level of the root log source, allows specifying propagation (see below)
- <source-name>.log_level: configures log level of a specific log source
- log_target: configures a log target (may be repeated several times)

#### Configuring log level

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

So, continuing the example above, we can define that the entire core package has INFO log level,  
but the files sub-package will have TRACE log level:

    core.log_level = INFO*
    core.files.log_level = TRACE*

Now suppose that for a short while we would like to investigate some bug in the core package,  
but we would like to avoid setting the files sub-package level to DIAG, since it is really noisy,  
and it is not related to the bug we are investigating. The simplest way to do this is as follows:

    core.log_level = DEBUG+
    core.files.log_level = TRACE-

The above configuration ensures all core log sources have at least DEBUG log level (if any log source  
had a more permissive log level, then its log level is kept intact), but after that the cores.files  
package log level is restricted to TRACE, including all sub-packages.  
Pay attention the order matters here.


#### Configuring Log Targets

As mentioned above, log targets can be configured using properties.  
The following syntax is supported:

    sys://stdout - add log target to standard output stream
    sys://stderr - add log target to standard error stream
    sys://syslog - add log target to syslog (or Windows event log, when running on Windows)
    file://path - add regular log target
    file://path?segment-size=<segment-size> - add segmented log target

The following optional parameters are supported for compound log targets:

    defer (no value associated)
    queue-batch-size=<batch-size>,queue-timeout-millis=<timeout-millis>
    quantum-buffer-size=<buffer-size>

These optional parameters can be used to specify a compound deferred or queued log target.  
The defer option specifies a deferred log target.  
The queue options specify a queued log target.  
The quantum option specifies a quantum log target.  
All options should be separated by an ampersand (&).

Log targets may be assigned a name for identification, if further special configuration is required.  
Target name may be specified by the 'name' parameter, as follows:

    file://path?name=file-logger

Next, the log target may be located as follows:

    ELogTarget* logTarget = ELogSystem::getLogTarget("file-logger");