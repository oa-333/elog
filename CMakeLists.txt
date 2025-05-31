cmake_minimum_required(VERSION 3.23)
include(FetchContent)
project(ELog VERSION 1.0 DESCRIPTION "ELog Error Logging Library" LANGUAGES CXX)
#CMAKE_CXX_STANDARD=23

# build shared library
add_library(elog SHARED)
set_target_properties(elog PROPERTIES POSITION_INDEPENDENT_CODE ON)

# build executable benchmark
add_executable(elog_bench)

# optional compilation features
option(ELOG_ENABLE_SQLITE_DB_CONNECTOR "ELog SQLite database connector" OFF)
option(ELOG_ENABLE_POSTGRESQL_DB_CONNECTOR "ELog PostgreSQL database connector" OFF)
option(ELOG_ENABLE_MYSQL_DB_CONNECTOR "ELog MySQL database connector" OFF)
option(ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR "ELog Kafka message queue connector" OFF)
option(ELOG_ENABLE_GRPC_CONNECTOR "ELog gRPC connector" OFF)
option(ELOG_ENABLE_GRAFANA_CONNECTOR "ELog Grafana connector" OFF)
option(ELOG_ENABLE_STACK_TRACE "ELog stack trace" OFF)

if (ELOG_ENABLE_STACK_TRACE)
    add_compile_definitions(-DELOG_ENABLE_STACK_TRACE)
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

# if gRPC is enabled we need to generate protocol headers# Proto file
# NOTE: all definitions here are adapted from the gRPC C++ client example in grpc.io
if (ELOG_ENABLE_GRPC_CONNECTOR)
    # enable connector in code
    add_compile_definitions(-DELOG_ENABLE_GRPC_CONNECTOR)

    # Find Protobuf installation
    # Looks for protobuf-config.cmake file installed by Protobuf's cmake installation.
    option(protobuf_MODULE_COMPATIBLE TRUE)
    find_package(Protobuf CONFIG REQUIRED)
    message(STATUS "Using protobuf ${Protobuf_VERSION}")
    message(STATUS "Using protobuf include dir at ${Protobuf_INCLUDE_DIR}")

    set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
    set(_REFLECTION gRPC::grpc++_reflection)
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
    message(STATUS "Using protoc at ${_PROTOBUF_PROTOC}")

    # Find gRPC installation
    # Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
    find_package(gRPC CONFIG REQUIRED)
    message(STATUS "Using gRPC ${gRPC_VERSION}")

    set(_GRPC_GRPCPP gRPC::grpc++)
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)

    # Find absl package
    #find_package(absl CONFIG REQUIRED)

    # protocol file
    get_filename_component(elog_proto "${CMAKE_SOURCE_DIR}/src/elog/proto/elog.proto" ABSOLUTE)
    get_filename_component(elog_proto_path "${elog_proto}" PATH)

    # Generated sources
    set(elog_proto_srcs "${elog_proto_path}/elog.pb.cc")
    set(elog_proto_hdrs "${elog_proto_path}/elog.pb.h")
    set(elog_grpc_srcs "${elog_proto_path}/elog.grpc.pb.cc")
    set(elog_grpc_hdrs "${elog_proto_path}/elog.grpc.pb.h")

    # generate files command
    add_custom_command(
        OUTPUT "${elog_proto_srcs}" "${elog_proto_hdrs}" "${elog_grpc_srcs}" "${elog_grpc_hdrs}"
        COMMAND ${_PROTOBUF_PROTOC}
        ARGS --grpc_out="${elog_proto_path}"
            --cpp_out="${elog_proto_path}"
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
    add_compile_definitions(-DELOG_ENABLE_GRAFANA_CONNECTOR)
    message("Getting httplib")
    FetchContent_Declare(httplib SYSTEM
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
        GIT_TAG 3af7f2c16147f3fbc6e4d717032daf505dc1652c # v0.20.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(httplib)

    target_link_libraries(elog httplib)
endif()

# add dependency from elog_bench to elog to enforce correct build order
#add_dependencies(elog_bench elog)

# set different name on MinGW
if (MINGW)
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
# TODO: for some reason these optimization are disregarded by cmake, also performance on MSVC is not good
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
    target_compile_definitions(elog PRIVATE -DELOG_DLL)
    # TODO: need to remove /Zi and check it gets added with Debug and 
    add_compile_options(/Zi /EHsc)
    # disable notorious C4251
    target_compile_options(elog PRIVATE /wd4251)
    target_compile_options(elog_bench PRIVATE /wd4251)
    target_link_libraries(elog Ws2_32 Advapi32)
endif()

# include directories
target_include_directories(elog PRIVATE src/elog/inc)
target_include_directories(elog_bench PRIVATE src/elog/inc)
if(ELOG_ENABLE_GRPC_CONNECTOR)
    target_include_directories(elog PRIVATE src/elog/proto)
    target_include_directories(elog_bench PRIVATE src/elog/proto)
endif()

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
# NOTE: no need to specify the directory of elog's output
target_link_libraries(elog_bench elog)

# SQLite - supported on all platforms
if (ELOG_ENABLE_SQLITE_DB_CONNECTOR)
    add_compile_definitions(-DELOG_ENABLE_SQLITE_DB_CONNECTOR)
    if (MSVC)
        # TODO: use env var instead of hard coded path, also document it
        target_include_directories(elog PRIVATE "C:\\Program Files\\SQLite\\3.49.1\\inc")
    endif()
    target_link_libraries(elog sqlite3)
endif()

# PostgreSQL - supported on all platforms except for Windows
if (NOT MSVC)
    if (ELOG_ENABLE_POSTGRESQL_DB_CONNECTOR)
        add_compile_definitions(-DELOG_ENABLE_POSTGRESQL_DB_CONNECTOR)
        target_include_directories(elog PRIVATE /usr/include/postgresql)
        target_link_libraries(elog pq)
    endif()
endif()

# MySQL - supported only on Windows
if (MSVC)
    if (ELOG_ENABLE_MYSQL_DB_CONNECTOR)
        add_compile_definitions(-DELOG_ENABLE_MYSQL_DB_CONNECTOR)
        target_include_directories(elog PRIVATE "C:\\Program Files\\MySQL\\MySQL Connector C++ 9.3\\include")
        target_link_directories(elog PRIVATE "C:\\Program Files\\MySQL\\MySQL Connector C++ 9.3\\lib64\\vs14")
        target_link_libraries(elog mysqlcppconn)
    endif()
endif()

# Kafka - supported on all platforms
if (ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR)
    add_compile_definitions(-DELOG_ENABLE_KAFKA_MSGQ_CONNECTOR)
    if (MSVC)
        target_include_directories(elog PRIVATE "C:\\Program Files\\librdkafka\\librdkafka.redist.2.10.0\\build\\native\\include")
        target_link_directories(elog PRIVATE "C:\\Program Files\\librdkafka\\librdkafka.redist.2.10.0\\build\\native\\lib\\win\\x64\\win-x64-Release\\v142")
    endif()
    target_link_libraries(elog rdkafka)
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
if (MSVC OR MINGW)
    install(TARGETS elog RUNTIME DESTINATION "C:\\install\\bin")
else()
    install(TARGETS elog RUNTIME DESTINATION "/home/oren/install/bin")
endif()