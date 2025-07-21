cmake_minimum_required(VERSION 3.23)
include(FetchContent)

#############################################################
# project definitions
#############################################################
project(ELog VERSION 0.1.0 DESCRIPTION "ELog Error Logging Library" LANGUAGES CXX)

# build shared library
add_library(elog SHARED)
set_target_properties(elog PROPERTIES POSITION_INDEPENDENT_CODE ON)

# build test if not from fetch content
option(ELOG_BUILD_BENCH "Build benchmark test" OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    message(STATUS "Building also local benchmark test")
    set(ELOG_BUILD_BENCH ON)
endif()

# build executable benchmark
if (ELOG_BUILD_BENCH)
    add_executable(elog_bench)
    if (MINGW)
        # since output may go to the same install dir, we need to use a different exe name
        set_target_properties(elog_bench PROPERTIES OUTPUT_NAME "elog_bench_mingw")
    endif()
endif()

# some things are platform dependent so we need a well-defined system name
if (MINGW)
    set(SYSTEM_NAME "${CMAKE_SYSTEM_NAME}_mingw")
else()
    set(SYSTEM_NAME "${CMAKE_SYSTEM_NAME}")
endif()

# fix error "IMPORTED_LOCATION not set" On Windows/vcpkg
if (MSVC)
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel RelWithDebInfo Release Debug)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel Debug)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo MinSizeRel Debug)
endif()

#############################################################
# optional compilation features
#############################################################
option(ELOG_SECURE "Use secure C runtime functions" OFF)
option(ELOG_ENABLE_STACK_TRACE "ELog stack trace" OFF)
option(ELOG_ENABLE_FMT_LIB "ELog fmtlib formatting style support" OFF)
option(ELOG_ENABLE_SQLITE_DB_CONNECTOR "ELog SQLite database connector" OFF)
option(ELOG_ENABLE_PGSQL_DB_CONNECTOR "ELog PostgreSQL database connector" OFF)
option(ELOG_ENABLE_MYSQL_DB_CONNECTOR "ELog MySQL database connector" OFF)
option(ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR "ELog Kafka message queue connector" OFF)
option(ELOG_ENABLE_GRPC_CONNECTOR "ELog gRPC connector" OFF)
option(ELOG_ENABLE_GRAFANA_CONNECTOR "ELog Grafana connector" OFF)
option(ELOG_ENABLE_SENTRY_CONNECTOR "ELog Sentry connector" OFF)
option(ELOG_ENABLE_DATADOG_CONNECTOR "ELog Datadog connector" OFF)

# debug options
option(ELOG_ENABLE_MEM_CHECK "Enables ELog memory check" OFF)
option(ELOG_ENABLE_GROUP_FLUSH_GC_TRACE "Enable debug trace group flush garbage collection" OFF)

# figure out whether HTTP client and JSON support is required
set(ELOG_ENABLE_HTTP OFF)
set(ELOG_ENABLE_JSON OFF)
if (ELOG_ENABLE_GRAFANA_CONNECTOR OR ELOG_ENABLE_DATADOG_CONNECTOR)
    set(ELOG_ENABLE_HTTP ON)
    set(ELOG_ENABLE_JSON ON)
endif()

#############################################################
# secure CRT option
#############################################################
if (ELOG_SECURE)
    target_compile_definitions(elog PRIVATE ELOG_SECURE)
endif()

#############################################################
# memory check option
#############################################################
if (ELOG_ENABLE_MEM_CHECK)
    if (MSVC)
        target_compile_options(elog PRIVATE "/fsanitize=address")
        if (ELOG_BUILD_BENCH)
            target_compile_options(elog_bench PRIVATE "/fsanitize=address")
        endif()
    else()
        target_compile_options(elog PRIVATE "-fsanitize=address")
        if (ELOG_BUILD_BENCH)
            target_compile_options(elog_bench PRIVATE "-fsanitize=address")
        endif()
        target_link_options(elog PRIVATE -fsanitize=address)
        #target_link_options(elog PRIVATE -shared-libasan)
        #target_link_libraries(elog asan)
        #target_link_libraries(elog_bench asan)
    endif()
endif()

#############################################################
# group flush tracing option
#############################################################
if (ELOG_ENABLE_GROUP_FLUSH_GC_TRACE)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_GROUP_FLUSH_GC_TRACE)
endif()

#############################################################
# get fmtlib if needed
#############################################################
if (ELOG_ENABLE_FMT_LIB)
    message(STATUS "Enabling fmtlib")
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_FMT_LIB)
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_FMT_LIB)
    endif()

    FetchContent_Declare(fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG 11.2.0
    )
    FetchContent_MakeAvailable(fmt)

    # although we prefer to use non-header-only version (faster compile, smaller binary files, 
    # but also has runtime dependency), on Linux it is recommended to use header only version, 
    # so to avoid relocation errors during link.
    # see more details here: https://github.com/fmtlib/fmt/issues/548
    if(LINUX)
        target_link_libraries(elog fmt::fmt-header-only)
    else()
        target_link_libraries(elog fmt::fmt)
    endif()
endif()

#############################################################
# get HTTP client if needed
#############################################################
if (ELOG_ENABLE_HTTP)
    message(STATUS "Enabling http")
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_HTTP)
    # add dependency on httplib
    # NOTE: on Windows/MinGW find_package works after vcpkg/pacman package install
    # but on Linux this fails, especially beware of installing libzstd-dev
    if (MSVC1)
        message(STATUS "Searching for httplib")
        find_package(httplib CONFIG REQUIRED)
        target_link_libraries(elog httplib::httplib)
    else()
        # on Linux we need to use fetch content method instead
        message(STATUS "Getting httplib")
        FetchContent_Declare(httplib SYSTEM
            GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
            GIT_TAG v0.20.1
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(httplib)

        target_link_libraries(elog httplib)
    endif()
    
    # get gzip headers path (for HTTP clients requiring compression)
    # NOTE: on Linux/MingW we need to get gzip-hpp explicitly (not sure why)
    if (NOT MSVC)
        message(STATUS "Getting gzip-hpp")
        FetchContent_Declare(gzip-hpp
            SYSTEM 
            EXCLUDE_FROM_ALL # NOTE: without this, gzip-hpp benchmark gets compiled into the project
            GIT_REPOSITORY https://github.com/mapbox/gzip-hpp
            GIT_TAG v0.1.0
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(gzip-hpp)
    endif()

    # now we can get gzip headers path
    find_path(GZIP_HPP_INCLUDE_DIRS gzip/compress.hpp)
    if (NOT GZIP_HPP_INCLUDE_DIRS)
        # This is a temporary workaround, not sure why find_path() fails on Linux/MinGW
        #set(GZIP_HPP_INCLUDE_DIRS ${gzp-hpp_SOURCE_DIR}/include)
        set(GZIP_HPP_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/_deps/gzip-hpp-src/include)
    endif()
    message(STATUS "Adding gzip include path: ${GZIP_HPP_INCLUDE_DIRS}")
    target_include_directories(elog PRIVATE ${GZIP_HPP_INCLUDE_DIRS})

    # gzip requires zlib
    message(STATUS "Searching for required package ZLIB")
    find_package(ZLIB REQUIRED)
    target_link_libraries(elog ZLIB::ZLIB)
    target_compile_definitions(elog PRIVATE ZLIB_CONST)
endif()

#############################################################
# get JSON if needed
#############################################################
if (ELOG_ENABLE_JSON)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_JSON)
    # add dependency on nlohmann_json
    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(elog nlohmann_json::nlohmann_json)
endif()

#############################################################
# stack trace requires dbgutil
#############################################################
if (ELOG_ENABLE_STACK_TRACE)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_STACK_TRACE)
    
    # for local builds we use local dbgutil
    if (ELOG_BUILD_BENCH)
        message(STATUS "Fetching dbgutil from local path: ${CMAKE_SOURCE_DIR}/../dbgutil")
        FetchContent_Declare(dbgutil
            SOURCE_DIR ${CMAKE_SOURCE_DIR}/../dbgutil)
    else()
        message(STATUS "Fetching dbgutil from github")
        FetchContent_Declare(dbgutil
            GIT_REPOSITORY https://github.com/oa-333/dbgutil.git
            GIT_TAG v0.1.0
        )
    endif()
    FetchContent_MakeAvailable(dbgutil)
    target_include_directories(
        elog
        PRIVATE ${dbgutil_SOURCE_DIR}/inc
    )
    target_link_libraries(elog dbgutil)
    
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_STACK_TRACE)
        target_include_directories(
            elog_bench
            PRIVATE ${dbgutil_SOURCE_DIR}/inc
        )
        target_link_libraries(elog_bench dbgutil)
    endif()
endif()

#############################################################
# gRPC Connector
#############################################################
# NOTE: all definitions here are adapted from the gRPC C++ client example in grpc.io
if (ELOG_ENABLE_GRPC_CONNECTOR)
    # enable connector in code
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_GRPC_CONNECTOR)
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_GRPC_CONNECTOR)
    endif()

    # Find Protobuf installation
    option(protobuf_MODULE_COMPATIBLE TRUE)
    find_package(Protobuf CONFIG REQUIRED)
    message(STATUS "Using protobuf ${Protobuf_VERSION}")

    set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
    set(_REFLECTION gRPC::grpc++_reflection)
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)

    # Find gRPC installation
    # Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
    find_package(gRPC CONFIG REQUIRED)
    message(STATUS "Using gRPC ${gRPC_VERSION}")

    set(_GRPC_GRPCPP gRPC::grpc++)
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

    # protocol file
    get_filename_component(elog_proto "${CMAKE_SOURCE_DIR}/src/elog/proto/elog.proto" ABSOLUTE)

    # set path for output files, per platform
    get_filename_component(elog_proto_path "${elog_proto}" PATH)
    set(elog_platform_proto_path "${elog_proto_path}/${SYSTEM_NAME}")
    make_directory(${elog_platform_proto_path})

    # Generated sources path
    set(elog_proto_srcs "${elog_platform_proto_path}/elog.pb.cc")
    set(elog_proto_hdrs "${elog_platform_proto_path}/elog.pb.h")
    set(elog_grpc_srcs "${elog_platform_proto_path}/elog.grpc.pb.cc")
    set(elog_grpc_hdrs "${elog_platform_proto_path}/elog.grpc.pb.h")
    target_include_directories(elog PRIVATE ${elog_platform_proto_path})
    if (ELOG_BUILD_BENCH)
        target_include_directories(elog_bench PRIVATE ${elog_platform_proto_path})
    endif()

    # generate files command

    # NOTE: on Windows/MSVC we have a severe problem.
    # elog_bench fails during link with missing definitions for generated protobuf classes
    # (both protocol and gRPC files, due to missing dll export).
    # Since the protobuf/grpc generated classes do not have DLL export/import annotation, one might 
    # think it suffices to add the generated source files also to the elog_bench project, but that 
    # causes runtime error, since now we have duplicate message definitions, which in turn causes 
    # assert during protobuf static initialization.
    #
    # The correct solution would be to export the generated classes.
    # the recommended workaround by protobuf/protoc community is as follows:
    # use the following command line syntax: --cpp_out=dllexport_decl=<MACRO NAME>:<path>
    # with this ELOG_API can be added to all generated message classes.
    # This causes build to fail though, due to missing definition of ELOG_API.
    # the suggested workaround for that is to force compiler to add include for each file, like this:
    #
    # Windows: /FIproject/Config.h
    # Linux: -include project/Config.h
    #
    # This workaround is good only for protobuf and not for gRPC generated classes (because there
    # is no way to add dll export to gRPC generated files), but that is ok, because we can include 
    # gRPC generated source also in bench_data (no static initialization issue there).
    add_custom_command(
        OUTPUT "${elog_proto_srcs}" "${elog_proto_hdrs}" "${elog_grpc_srcs}" "${elog_grpc_hdrs}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out="${elog_platform_proto_path}"
            --cpp_out=dllexport_decl=ELOG_API:"${elog_platform_proto_path}"
            -I "${elog_proto_path}"
            --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
            "${elog_proto}"
        DEPENDS "${elog_proto}")

    # NOTE: normally we would add generated source to the project on the fly, but for some obscure 
    # reason, when doing so, the generated files fail to get the compiler option to force inclusion 
    # of elog_def.h. This problem exists only on Windows platform.
    #
    # The workaround found for the meantime, is to add these files statically through 
    # add_subdirectory() directive (each platform has its own generated files, due to different 
    # versions of protoc).
    #
    # Therefore, the following target_sources() directives are commented out.

    # add generated headers to public interface (commented out, see comment above)
    # target_sources(elog PUBLIC FILE_SET grpcpublicheaders TYPE HEADERS FILES ${elog_proto_hdrs} ${elog_grpc_hdrs})

    # add generated sources to library (commented out, see comment above)
    # target_sources(elog PRIVATE ${elog_proto_srcs} ${elog_grpc_srcs})

    # add gRPC sources to target elog_bench on Windows (because they cannot be exported, due to 
    # protoc limitation), but do not add protobuf generated message sources (see big comment above)
    if (MSVC AND ELOG_BUILD_BENCH)
        target_sources(elog_bench PRIVATE ${elog_grpc_srcs})
        # NOTE: we do not add protobuf generated message source (because they can be exported)
    endif()

    # force include of elog_def.h so that protobuf generates code can compile (see big comment above)
    if(MSVC)
        message(STATUS "Forcing inclusion of elog_def.h")
        target_compile_options(elog PRIVATE /FI${CMAKE_SOURCE_DIR}\\src\\elog\\inc\\elog_def.h)
        # NOTE: we need to force inclusion of elog_def.h for elog_bench as well, since it compiles
        # gRPC generated sources, and therefore includes generated protobuf message headers, which
        # now contain ELOG_API definition in each class declaration
        if (ELOG_BUILD_BENCH)
            target_compile_options(elog_bench PRIVATE /FI${CMAKE_SOURCE_DIR}\\src\\elog\\inc\\elog_def.h)
        endif()
    else()
        target_compile_options(elog PRIVATE -include ${CMAKE_SOURCE_DIR}/src/elog/inc/elog_def.h)
    endif()

    # make sure elog is linked against required libraries
    target_link_libraries(elog
        absl::absl_log
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF})

    # make sure elog_bench is linked against required libraries
    if (ELOG_BUILD_BENCH)
        target_link_libraries(elog_bench
            absl::absl_log
            absl::flags_parse
            absl::log_initialize
            absl::log_globals
            ${_REFLECTION}
            ${_GRPC_GRPCPP}
            ${_PROTOBUF_LIBPROTOBUF})
    endif()
endif()

#############################################################
# Grafana Loki connector
#############################################################
if (ELOG_ENABLE_GRAFANA_CONNECTOR)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_GRAFANA_CONNECTOR)
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_GRAFANA_CONNECTOR)
    endif()
    # Grafana requires HTTP connection and JSON support, which is already handled above
endif()

#############################################################
# Sentry connector
#############################################################
if (ELOG_ENABLE_SENTRY_CONNECTOR)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_SENTRY_CONNECTOR)
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_SENTRY_CONNECTOR)
    endif()
    if (MSVC)
        find_package(sentry CONFIG REQUIRED)
        target_link_libraries(elog sentry::sentry)
    else()
        message(STATUS "Using SENTRY_SDK_HOME " $ENV{SENTRY_SDK_HOME})
        target_include_directories(elog PRIVATE "$ENV{SENTRY_SDK_HOME}/include")
        target_link_directories(elog PRIVATE "$ENV{SENTRY_SDK_HOME}/lib")
        target_link_libraries(elog sentry)
        if (ELOG_BUILD_BENCH)
            target_link_directories(elog_bench PRIVATE "$ENV{SENTRY_SDK_HOME}/lib")
            target_link_libraries(elog_bench sentry)
        endif()
    endif()
endif()

#############################################################
# Datadog connector
#############################################################
if (ELOG_ENABLE_DATADOG_CONNECTOR)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_DATADOG_CONNECTOR)
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_DATADOG_CONNECTOR)
    endif()
    # Datadog requires HTTP connection and JSON support, which is already handled above
endif()

#############################################################
# SQLite connector - supported on all platforms
#############################################################
if (ELOG_ENABLE_SQLITE_DB_CONNECTOR)
    # NOTE: find_package works only for MSVC/vcpkg
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_SQLITE_DB_CONNECTOR)
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_SQLITE_DB_CONNECTOR)
    endif()
    if (MSVC)
        find_package(unofficial-sqlite3 CONFIG REQUIRED)
        target_link_libraries(elog unofficial::sqlite3::sqlite3)
    else()
        # use normal apt-install/pacman to install sqlite3
        target_link_libraries(elog sqlite3)
    endif()
endif()

#############################################################
# PostgreSQL connector
#############################################################
if (ELOG_ENABLE_PGSQL_DB_CONNECTOR)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_PGSQL_DB_CONNECTOR)
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_PGSQL_DB_CONNECTOR)
    endif()
    # NOTE: find_package works only for Windows/MinGW
    if (MSVC)
        find_package(PostgreSQL REQUIRED)
        target_link_libraries(elog PostgreSQL::PostgreSQL)
        find_package(libpqxx CONFIG REQUIRED)
        target_link_libraries(elog libpqxx::pqxx)
    else()
        target_include_directories(elog PRIVATE /usr/include/postgresql)
        target_link_libraries(elog pq)
    endif()
endif()

#############################################################
# MySQL connector - supported only on Windows
#############################################################
if (MSVC)
    if (ELOG_ENABLE_MYSQL_DB_CONNECTOR)
        # all attempts to use vcpkg failed (no jdbc driver, only xdev for document API), so using old method
        # env var MYSQL_ROOT is required to point to installation dir
        # default value: "C:\\Program Files\\MySQL\\MySQL Connector C++ 9.3"
        message(STATUS "Using MYSQL_ROOT " ${MYSQL_ROOT})
        target_compile_definitions(elog PRIVATE ELOG_ENABLE_MYSQL_DB_CONNECTOR)
        #find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)
        #target_link_libraries(elog unofficial::mysql-connector-cpp::connector)
        target_include_directories(elog PRIVATE "${MYSQL_ROOT}\\include")
        target_link_directories(elog PRIVATE "${MYSQL_ROOT}\\lib64\\vs14")
        target_link_libraries(elog mysqlcppconn)
        if (ELOG_BUILD_BENCH)
            target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_MYSQL_DB_CONNECTOR)
            target_link_directories(elog_bench PRIVATE "${MYSQL_ROOT}\\lib64\\vs14")
            target_link_libraries(elog_bench mysqlcppconn)
        endif()
    endif()
endif()

#############################################################
# Kafka connector - supported on all platforms
#############################################################
if (ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR)
    if (ELOG_BUILD_BENCH)
        target_compile_definitions(elog_bench PRIVATE ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR)
    endif()
    if (MSVC)
        find_package(RdKafka CONFIG REQUIRED)
        target_link_libraries(elog RdKafka::rdkafka RdKafka::rdkafka++)
    else()
        # requires apt-install/pacman
        target_link_libraries(elog rdkafka)
    endif()
endif()

#############################################################
# C++ Version
#############################################################

# Using C++ 20 standard on Windows, and C++ 23 on other platforms
if (MSVC)
    target_compile_features(elog PRIVATE cxx_std_20)
    if (ELOG_BUILD_BENCH)
        target_compile_features(elog_bench PRIVATE cxx_std_20)
    endif()
else()
    target_compile_features(elog PRIVATE cxx_std_23)
    if (ELOG_BUILD_BENCH)
        target_compile_features(elog_bench PRIVATE cxx_std_23)
    endif()
endif()

#############################################################
# Compile Options
#############################################################

#############################################################
# MSVC Release Mode flags
#############################################################
# some optimizations for MSVC release builds that cmake does not by default set
# this options come out of the box in Visual Studio C++ for Release build
# we also add /Ob2 (since cmake uses /Ob1 thus restricting inlining)
if (MSVC)
    if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        # disable security checks
        target_compile_options(elog PRIVATE "/sdl-" )
        # be permissive
        target_compile_options(elog PRIVATE "/permissive-")
        # whole program optimization
        target_compile_options(elog PRIVATE "/GL")
        # use intrinsics
        target_compile_options(elog PRIVATE "/Oi")
        # allowing more inlining
        #target_compile_options(elog PRIVATE "/Ob2")
        # disable security checks
        target_compile_options(elog PRIVATE "/GS-")
        # enable function level linking
        target_compile_options(elog PRIVATE "/Gy")
        # disable runtime type information
        #target_compile_options(elog PRIVATE "/GR-")
        # don't generate manifest
        target_link_options(elog PRIVATE "/MANIFEST:NO")
        # linker optimization (reference=yes)
        target_link_options(elog PRIVATE "/OPT:REF")
        # linker optimization (enable COMDAT Folding)
        target_link_options(elog PRIVATE "/OPT:ICF")
        # disable incremental linking
        target_link_options(elog PRIVATE "/INCREMENTAL:NO")
        
        if (ELOG_BUILD_BENCH)
            target_compile_options(elog_bench PRIVATE "/sdl-" )
            target_compile_options(elog_bench PRIVATE "/permissive-")
            target_compile_options(elog_bench PRIVATE "/GL")
            target_compile_options(elog_bench PRIVATE "/Oi")
            target_compile_options(elog_bench PRIVATE "/Ob2")
            target_compile_options(elog_bench PRIVATE "/GS-")
            target_compile_options(elog_bench PRIVATE "/Gy")
            target_compile_options(elog_bench PRIVATE "/GR-")
            target_link_options(elog_bench PRIVATE "/MANIFEST:NO")
            target_link_options(elog_bench PRIVATE "/OPT:REF")
            target_link_options(elog_bench PRIVATE "/OPT:ICF")
            target_link_options(elog_bench PRIVATE "/INCREMENTAL:NO")
        endif()
    endif()
endif()

#############################################################
# Compiler Warnings
#############################################################
if (MSVC)
    target_compile_options(elog PRIVATE /Wall)
    # disable notorious C4251 (user is required to use the same C++ version)
    # instead user needs to build all modules with same C++ version for ABI safety
    target_compile_options(elog PRIVATE /wd4251)
    target_compile_options(elog PRIVATE /wd5045)
    target_compile_options(elog PRIVATE /wd4100)
    target_compile_options(elog PRIVATE /wd4820)
    target_compile_options(elog PRIVATE /wd4324)
    target_compile_options(elog PRIVATE /wd5039)
    target_compile_options(elog PRIVATE /wd4710)
    if (ELOG_BUILD_BENCH)
        target_compile_options(elog_bench PRIVATE /wd4251)
    endif()
else()
    target_compile_options(elog PRIVATE -Wall)
    # -Wno-interference-size - don't warn about using std::hardware_destructive_interference_size
    add_compile_options(-Wno-interference-size)
endif()

#############################################################
# clang options
#############################################################
# TODO: this requires fixing in code
if ("$ENV{CXX}" MATCHES ".*clang.*")
    target_compile_options(elog PRIVATE "-Wno-final-dtor-non-final-class")
    if (ELOG_BUILD_BENCH)
        target_compile_options(elog_bench PRIVATE "-Wno-final-dtor-non-final-class")
    endif()
endif()

# compile flags for per platform
if (MSVC)
    target_compile_definitions(elog PRIVATE ELOG_DLL)
    # /Zi - generate full debug information
    # /EHsc - catch(..) catches only C++ exceptions, extern "C" functions never throw C++ exception
    add_compile_options(/Zi /EHsc)
    target_link_libraries(elog Ws2_32 Advapi32)
endif()

# include directories
target_include_directories(elog PRIVATE src/elog/inc)
if (ELOG_BUILD_BENCH)
    target_include_directories(elog_bench PRIVATE src/elog/inc)
endif()

# special include directories for MinGW
# TODO: check this again, since we are building from UCRT64 console
if (MINGW)
    target_include_directories(elog PRIVATE /ucrt64/include)
    target_link_directories(elog PRIVATE /ucrt64/lib)
    if (ELOG_BUILD_BENCH)
        target_include_directories(elog_bench PRIVATE /ucrt64/include)
        target_link_directories(elog_bench PRIVATE /ucrt64/lib)
    endif()
elseif(NOT MSVC)
    add_link_options(-rdynamic)
endif()

# link libraries
if (ELOG_BUILD_BENCH)
    target_link_libraries(elog_bench elog)
endif()

# source folders
add_subdirectory(src/elog/src)
add_subdirectory(src/elog/proto)
if (ELOG_BUILD_BENCH)
    add_subdirectory(src/elog_bench/src)
endif()

# public headers
add_subdirectory(src/elog/inc)

#############################################################
# local install for tests
#############################################################
if (ELOG_BUILD_BENCH)
    message(STATUS "Installing at ${CMAKE_INSTALL_PREFIX}")

    # install header in install dir
    install(TARGETS elog FILE_SET publicheaders)
    install(TARGETS elog_bench)

    # common install dir
    set(ELOG_INSTALL_REL_PATH ${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME}-${CMAKE_BUILD_TYPE})
    cmake_path(NORMAL_PATH ELOG_INSTALL_REL_PATH OUTPUT_VARIABLE ELOG_INSTALL_PATH)
    make_directory(${ELOG_INSTALL_PATH})
    make_directory(${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME})
    install(TARGETS elog LIBRARY DESTINATION ${ELOG_INSTALL_PATH})
    install(TARGETS elog RUNTIME DESTINATION ${ELOG_INSTALL_PATH})
    install(TARGETS elog_bench RUNTIME DESTINATION ${ELOG_INSTALL_PATH})
    if (MSVC)
        install(FILES run_bench.bat DESTINATION ${ELOG_INSTALL_PATH})
    else()
        install(FILES run_bench.sh DESTINATION ${ELOG_INSTALL_PATH})
    endif()
endif()