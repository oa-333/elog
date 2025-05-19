cmake_minimum_required(VERSION 3.13)
project(ELog VERSION 1.0 DESCRIPTION "ELog Error Logging Library" LANGUAGES CXX)
#CMAKE_CXX_STANDARD=23

# build shared library
add_library(elog SHARED)
set_target_properties(elog PROPERTIES POSITION_INDEPENDENT_CODE ON)

# build executable benchmark
add_executable(elog_bench)
if (MINGW)
    # set different name on MinGW
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
        target_compile_options(elog PRIVATE "/Ob2")
        target_compile_options(elog_bench PRIVATE "/Ob2")
        # disable security checks
        target_compile_options(elog PRIVATE "/GS-")
        target_compile_options(elog_bench PRIVATE "/GS-")
        # enable function level linking
        target_compile_options(elog PRIVATE "/Gy")
        target_compile_options(elog_bench PRIVATE "/Gy")
        # disable runtime type information
        target_compile_options(elog PRIVATE "/GR-")
        target_compile_options(elog_bench PRIVATE "/GR-")
        # disable overriding compiler option warning (since we override /Ob1 with /Ob2 and /GR with /GR-)
        target_compile_options(elog PRIVATE /wd9025)
        target_compile_options(elog_bench PRIVATE /wd9025)
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
    target_link_libraries(elog Ws2_32 Advapi32)
    target_include_directories(elog_bench PRIVATE C:\\install\\elog\\include)
    target_link_directories(elog_bench PRIVATE C:\\install\\elog\\lib)
elseif(MINGW)
    target_include_directories(elog_bench PRIVATE C:\\install\\elog\\include)
    target_link_directories(elog_bench PRIVATE C:\\install\\elog\\lib)
else()
    target_include_directories(elog_bench PRIVATE ~/install/elog/include)
    target_link_directories(elog_bench PRIVATE ~/install/elog/lib)
endif()

if (MINGW)
    target_include_directories(elog PRIVATE /ucrt64/include)
    target_include_directories(elog_bench PRIVATE /ucrt64/include)
    target_link_directories(elog PRIVATE /ucrt64/lib)
    target_link_directories(elog_bench PRIVATE /ucrt64/lib)
elseif(NOT MSVC)
    add_link_options(-rdynamic)
endif()

target_link_libraries(elog_bench elog)

# optional compilation features
option(ELOG_ENABLE_SQLITE_DB_CONNECTOR "ELog SQLite database connector" OFF)
option(ELOG_ENABLE_POSTGRESQL_DB_CONNECTOR "ELog PostgreSQL database connector" OFF)
option(ELOG_ENABLE_MYSQL_DB_CONNECTOR "ELog MySQL database connector" OFF)
option(ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR "ELog Kafka message queue connector" OFF)

# SQLite - supported on all platforms
if (ELOG_ENABLE_SQLITE_DB_CONNECTOR)
    add_compile_definitions(-DELOG_ENABLE_SQLITE_DB_CONNECTOR)
    if (MSVC)
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

# include path
target_include_directories(elog PRIVATE src/elog/inc)

# source folders
add_subdirectory(src/elog/src)
add_subdirectory(src/elog_bench/src)

# public headers
add_subdirectory(src/elog/inc)

# install header in install dir
install(TARGETS elog FILE_SET publicheaders)
install(TARGETS elog_bench)