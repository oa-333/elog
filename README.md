# ELog Logging Package

This is simple package for application message logging in C++.  
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

This project is licensed under the Apache 2.0 License - see the LICENSE file for details

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



### Defining Log Sources and Loggers

One of the main entities in the ELog system is the Log Source.  
It serves as a semantic module entity.  
From one Log Source many loggers can be obtained, one for each logging class/file.
A logger is the logging client's end point, with which log messages are partially formatted, before being sent to log targets for actual logging.
