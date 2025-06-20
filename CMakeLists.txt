cmake_minimum_required(VERSION 3.23)
include(FetchContent)
project(ELog VERSION 1.0 DESCRIPTION "ELog Error Logging Library" LANGUAGES CXX)

# build shared library
add_library(elog SHARED)
set_target_properties(elog PROPERTIES POSITION_INDEPENDENT_CODE ON)

# build executable benchmark
add_executable(elog_bench)

if (MINGW)
    set(SYSTEM_NAME "${CMAKE_SYSTEM_NAME}_mingw")
else()
    set(SYSTEM_NAME "${CMAKE_SYSTEM_NAME}")
endif()

# optional compilation features
option(ELOG_ENABLE_STACK_TRACE "ELog stack trace" OFF)
option(ELOG_ENABLE_SQLITE_DB_CONNECTOR "ELog SQLite database connector" OFF)
option(ELOG_ENABLE_PGSQL_DB_CONNECTOR "ELog PostgreSQL database connector" OFF)
option(ELOG_ENABLE_MYSQL_DB_CONNECTOR "ELog MySQL database connector" OFF)
option(ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR "ELog Kafka message queue connector" OFF)
option(ELOG_ENABLE_GRPC_CONNECTOR "ELog gRPC connector" OFF)
option(ELOG_ENABLE_GRAFANA_CONNECTOR "ELog Grafana connector" OFF)
option(ELOG_ENABLE_SENTRY_CONNECTOR "ELog Sentry connector" OFF)

# fix error "IMPORTED_LOCATION not set" On Windows/vcpkg
if (MSVC)
    set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL MinSizeRel RelWithDebInfo Release Debug)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO RelWithDebInfo Release MinSizeRel Debug)
    set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Release RelWithDebInfo MinSizeRel Debug)
endif()

# stack trace requires dbgutil
# TODO: this should change after moving to public repo
if (ELOG_ENABLE_STACK_TRACE)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_STACK_TRACE)
    # add dbg util dependency
    if (MSVC OR MINGW)
        target_include_directories(elog PRIVATE "C:\\install\\dbgutil\\include")
        target_include_directories(elog_bench PRIVATE "C:\\install\\dbgutil\\include")
        target_link_directories(elog PRIVATE "C:\\install\\dbgutil\\lib")
        target_link_directories(elog_bench PRIVATE "C:\\install\\dbgutil\\lib")
    else()
        target_include_directories(elog PRIVATE ~/install/dbgutil/include)
        target_include_directories(elog_bench PRIVATE ~/install/dbgutil/include)
        target_link_directories(elog PRIVATE ~/install/dbgutil/lib)
        target_link_directories(elog_bench PRIVATE ~/install/dbgutil/lib)
    endif()
    target_link_libraries(elog dbgutil)
endif()

# if gRPC is enabled we need to generate protocol headers Proto file
# NOTE: all definitions here are adapted from the gRPC C++ client example in grpc.io
if (ELOG_ENABLE_GRPC_CONNECTOR)
    # enable connector in code
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_GRPC_CONNECTOR)

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

    # Generated sources
    set(elog_proto_srcs "${elog_platform_proto_path}/elog.pb.cc")
    set(elog_proto_hdrs "${elog_platform_proto_path}/elog.pb.h")
    set(elog_grpc_srcs "${elog_platform_proto_path}/elog.grpc.pb.cc")
    set(elog_grpc_hdrs "${elog_platform_proto_path}/elog.grpc.pb.h")
    target_include_directories(elog PRIVATE ${elog_platform_proto_path})
    target_include_directories(elog_bench PRIVATE ${elog_platform_proto_path})

    # generate files command
    add_custom_command(
        OUTPUT "${elog_proto_srcs}" "${elog_proto_hdrs}" "${elog_grpc_srcs}" "${elog_grpc_hdrs}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out="${elog_platform_proto_path}"
            --cpp_out="${elog_platform_proto_path}"
            -I "${elog_proto_path}"
            --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
            "${elog_proto}"
        DEPENDS "${elog_proto}")

    # add generated headers to public interface
    target_sources(elog PUBLIC FILE_SET grpcpublicheaders TYPE HEADERS FILES ${elog_proto_hdrs} ${elog_grpc_hdrs})

    # add generated sources to library and test bench
    target_sources(elog PRIVATE ${elog_proto_srcs} ${elog_grpc_srcs})
    if (MSVC)
        target_sources(elog_bench PRIVATE ${elog_proto_srcs} ${elog_grpc_srcs})
    endif()

    # make sure elog is linked against required libraries
    target_link_libraries(elog
        absl::absl_log
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF})

    # make sure elog_bench is linked against required libraries
    target_link_libraries(elog_bench
        absl::absl_log
        absl::flags_parse
        absl::log_initialize
        absl::log_globals
        ${_REFLECTION}
        ${_GRPC_GRPCPP}
        ${_PROTOBUF_LIBPROTOBUF})
endif()

if (ELOG_ENABLE_GRAFANA_CONNECTOR)
    # Grafana requires HTTP connection
    # add dependency on httplib
    # NOTE: on Windows/MinGW find_package works after vcpkg/pacman package install
    # but on Linux this fails, especially beware of installing libzstd-dev
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_GRAFANA_CONNECTOR)
    if (MSVC1)
        find_package(httplib CONFIG REQUIRED)
        target_link_libraries(elog httplib::httplib)
    else()
        # on Linux we need to use fetch content method instead
        message("Getting httplib")
        FetchContent_Declare(httplib SYSTEM
            GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
            GIT_TAG 3af7f2c16147f3fbc6e4d717032daf505dc1652c # v0.20.1
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(httplib)

        target_link_libraries(elog httplib)
    endif()

    # add dependency on nlohmann_json
    find_package(nlohmann_json CONFIG REQUIRED)
    target_link_libraries(elog nlohmann_json::nlohmann_json)
endif()

if (ELOG_ENABLE_SENTRY_CONNECTOR)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_SENTRY_CONNECTOR)
    if (MSVC)
        find_package(sentry CONFIG REQUIRED)
        target_link_libraries(elog sentry::sentry)
    else()
        message(STATUS "Using SENTRY_SDK_HOME " $ENV{SENTRY_SDK_HOME})
        target_include_directories(elog PRIVATE "$ENV{SENTRY_SDK_HOME}/include")
        target_link_directories(elog PRIVATE "$ENV{SENTRY_SDK_HOME}/lib")
        target_link_directories(elog_bench PRIVATE "$ENV{SENTRY_SDK_HOME}/lib")
        target_link_libraries(elog sentry)
        target_link_libraries(elog_bench sentry)
    endif()
endif()

# set different name on MinGW
if (MINGW)
    # since output may go to the same install dir, we need to use a different exe name
    set_target_properties(elog_bench PROPERTIES OUTPUT_NAME "elog_bench_mingw")
endif()

# Using C++ 20 standard on Windows, and C++ 23 on other platforms
if (MSVC)
    target_compile_features(elog PRIVATE cxx_std_20)
    target_compile_features(elog_bench PRIVATE cxx_std_20)
else()
    target_compile_features(elog PRIVATE cxx_std_23)
    target_compile_features(elog_bench PRIVATE cxx_std_23)
endif()

# some optimizations for MSVC release builds that cmake does not by default set
# this options come out of the box in Visual Studio C++ for Release build
# we also add /Ob2 (since cmake uses /Ob1 thus restricting inlining)
if (MSVC)
    if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        # disable security checks
        target_compile_options(elog PRIVATE "/sdl-" )
        target_compile_options(elog_bench PRIVATE "/sdl-" )
        # be permissive
        target_compile_options(elog PRIVATE "/permissive-")
        target_compile_options(elog_bench PRIVATE "/permissive-")
        # whole program optimization
        target_compile_options(elog PRIVATE "/GL")
        target_compile_options(elog_bench PRIVATE "/GL")
        # use intrinsics
        target_compile_options(elog PRIVATE "/Oi")
        target_compile_options(elog_bench PRIVATE "/Oi")
        # allowing more inlining
        #target_compile_options(elog PRIVATE "/Ob2")
        target_compile_options(elog_bench PRIVATE "/Ob2")
        # disable security checks
        target_compile_options(elog PRIVATE "/GS-")
        target_compile_options(elog_bench PRIVATE "/GS-")
        # enable function level linking
        target_compile_options(elog PRIVATE "/Gy")
        target_compile_options(elog_bench PRIVATE "/Gy")
        # disable runtime type information
        #target_compile_options(elog PRIVATE "/GR-")
        target_compile_options(elog_bench PRIVATE "/GR-")
        # don't generate manifest
        target_link_options(elog PRIVATE "/MANIFEST:NO")
        target_link_options(elog_bench PRIVATE "/MANIFEST:NO")
        # linker optimization (reference=yes)
        target_link_options(elog PRIVATE "/OPT:REF")
        target_link_options(elog_bench PRIVATE "/OPT:REF")
        # linker optimization (enable COMDAT Folding)
        target_link_options(elog PRIVATE "/OPT:ICF")
        target_link_options(elog_bench PRIVATE "/OPT:ICF")
        # disable incremental linking
        target_link_options(elog PRIVATE "/INCREMENTAL:NO")
        target_link_options(elog_bench PRIVATE "/INCREMENTAL:NO")
    endif()
endif()

# -Wno-interference-size - don't warn about using std::hardware_destructive_interference_size
if (NOT MSVC)
    add_compile_options(-Wno-interference-size)
endif()

# compile flags for per platform
if (MSVC)
    # /Zi - generate full debug information
    # /EHsc - catch(..) catches only C++ exceptions, extern "C" functions never throw C++ exception
    target_compile_definitions(elog PRIVATE ELOG_DLL)
    # TODO: need to remove /Zi and check it gets added with Debug and 
    add_compile_options(/Zi /EHsc)
    # disable notorious C4251 (user is required to use the same C++ version)
    target_compile_options(elog PRIVATE /wd4251)
    target_compile_options(elog_bench PRIVATE /wd4251)
    target_link_libraries(elog Ws2_32 Advapi32)
endif()

# include directories
target_include_directories(elog PRIVATE src/elog/inc)
target_include_directories(elog_bench PRIVATE src/elog/inc)

# special include directories for MinGW
if (MINGW)
    target_include_directories(elog PRIVATE /ucrt64/include)
    target_include_directories(elog_bench PRIVATE /ucrt64/include)
    target_link_directories(elog PRIVATE /ucrt64/lib)
    target_link_directories(elog_bench PRIVATE /ucrt64/lib)
elseif(NOT MSVC)
    add_link_options(-rdynamic)
endif()

# link libraries
target_link_libraries(elog_bench elog)

# SQLite - supported on all platforms
if (ELOG_ENABLE_SQLITE_DB_CONNECTOR)
    # NOTE: find_package works only for MSVC/vcpkg
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_SQLITE_DB_CONNECTOR)
    if (MSVC)
        find_package(unofficial-sqlite3 CONFIG REQUIRED)
        target_link_libraries(elog unofficial::sqlite3::sqlite3)
    else()
        # use normal apt-install/pacman to install sqlite3
        target_link_libraries(elog sqlite3)
    endif()
endif()

# PostgreSQL
if (ELOG_ENABLE_PGSQL_DB_CONNECTOR)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_PGSQL_DB_CONNECTOR)
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

# MySQL - supported only on Windows
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
        target_link_directories(elog_bench PRIVATE "${MYSQL_ROOT}\\lib64\\vs14")
        target_link_libraries(elog mysqlcppconn)
        target_link_libraries(elog_bench mysqlcppconn)
    endif()
endif()

# Kafka - supported on all platforms
if (ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR)
    target_compile_definitions(elog PRIVATE ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR)
    if (MSVC)
        find_package(RdKafka CONFIG REQUIRED)
        target_link_libraries(elog RdKafka::rdkafka RdKafka::rdkafka++)
    else()
        # requires apt-install/pacman
        target_link_libraries(elog rdkafka)
    endif()
endif()

# source folders
add_subdirectory(src/elog/src)
add_subdirectory(src/elog_bench/src)

# public headers
add_subdirectory(src/elog/inc)

# install header in install dir
install(TARGETS elog FILE_SET publicheaders)
install(TARGETS elog_bench)

# common install dir
make_directory(${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME})
install(TARGETS elog LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME}-${CMAKE_BUILD_TYPE})
install(TARGETS elog RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME}-${CMAKE_BUILD_TYPE})
install(TARGETS elog_bench RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME}-${CMAKE_BUILD_TYPE})
if (MSVC)
    install(FILES run_bench.bat DESTINATION ${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME}-${CMAKE_BUILD_TYPE})
else()
    install(FILES run_bench.sh DESTINATION ${CMAKE_INSTALL_PREFIX}/../bin/${SYSTEM_NAME}-${CMAKE_BUILD_TYPE})
endif()