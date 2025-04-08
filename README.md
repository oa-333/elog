# ELog Logging Package

This is simple package for application message logging in C++.  
The package was designed such that it can be extended for a broad range of use cases.

## Description

The most common use case is a utility logging library, in order to write log messages to file, but much more can be done with it.  
For instance, it can be hooked to an external message queue, while applying complex message filtering and transformations.  
This could be useful for DevOps use cases.

One more use case is when chasing a bug that requires log flooding.  
In this case, sending messages to log files may affect timing and prevents from the bug being reproduced during heavy logging.  
For such a situation there is a special lock-free deferred/queued log target, which minimizes the logging latency, by using a lock-free queue and a designated thread that logs batches of log messages.

Another common use case is log file segmentation (i.e. breaking log file to segments of some size).

## Getting Started

In order to use the package, include "elog_system.h", which is the package facade.  
In the application code, make sure to call one of the elog::ElogSystem::initializeXXX() function before using any of the logging macros. After this, you can use ELOG_INFO() and the rest of the macros.  
At application exit make sure to call elog::ELogSystem::terminate().

### Dependencies

The ELog system has no special dependencies.

### Installing

The package can be build by running:

    make -j INSTALL_DIR=<install-path> install

Add to compiler include path:

    -I<install-path>/elog/include/elog
    
Add to linker flags:

    -L<install-path>/bin -lelog

## Help

See documentation in header files for more information.

## Authors

Oren Amor (oren.amor@gmail.com)

## License

This project is licensed under the Apache 2.0 License - see the LICENSE file for details
