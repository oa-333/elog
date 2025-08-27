#ifndef __ELOG_DEF_H__
#define __ELOG_DEF_H__

// clang settings
#ifdef __clang__
#define ELOG_CLANG
#if defined(__MINGW32__) || defined(__MINGW64__)
// #pragma message "ELog Detected MinGW/clang toolchain"
#define ELOG_WINDOWS
#define ELOG_MINGW
#define ELOG_API
#elif defined(_WIN32) || defined(_WIN64)
// #pragma message("ELog Detected Windows/clang toolchain")
// NOTE: on Windows platform we treat clang compiler as MSVC compiler due to clang compatibility
// frontend clang-cl
#define ELOG_MSVC
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define ELOG_WINDOWS
#define DLL_EXPORT __declspec(dllexport)
#define DLL_IMPORT __declspec(dllimport)
#ifdef ELOG_DLL
#define ELOG_API DLL_EXPORT
#else
#define ELOG_API DLL_IMPORT
#endif
#elif defined(__linux__)
// #pragma message "ELog Detected Linux/clang toolchain"
#define ELOG_LINUX
#define ELOG_API
#endif

// Windows/MSVC settings
#elif defined(_MSC_VER)
// #pragma message("ELog Detected Windows/MSVC toolchain")
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#define ELOG_WINDOWS
#define ELOG_MSVC
#define DLL_EXPORT __declspec(dllexport)
#define DLL_IMPORT __declspec(dllimport)
#ifdef ELOG_DLL
#define ELOG_API DLL_EXPORT
#else
#define ELOG_API DLL_IMPORT
#endif

// MinGW settings
#elif defined(__MINGW32__) || defined(__MINGW64__)
// #pragma message "ELog Detected MinGW/gcc toolchain"
#define ELOG_WINDOWS
#define ELOG_MINGW
#define ELOG_GCC
#define ELOG_API

// Linux settings
#elif defined(__linux__)
// #pragma message "ELog Detected Linux/gcc toolchain"
#define ELOG_LINUX
#define ELOG_GCC
#define ELOG_API
#else
#error "Unsupported platform"
#endif

#ifdef ELOG_GCC
#define CPU_RELAX asm volatile("pause\n" : : : "memory")
#elif defined(ELOG_MSVC)
#define CPU_RELAX YieldProcessor()
#else
#define CPU_RELAX
#endif

// define incorrect __cplusplus on MSVC
#if defined(ELOG_WINDOWS) && defined(_MSVC_LANG)
#define ELOG_CPP_VER _MSVC_LANG
#else
#define ELOG_CPP_VER __cplusplus
#endif

// define fallthrough attribute
#if (ELOG_CPP_VER >= 201703L)
#define ELOG_FALLTHROUGH [[fallthrough]]
#else
#define ELOG_FALLTHROUGH
#endif

// define strcasecmp for MSVC
#ifdef ELOG_MSVC
#ifdef strncasecmp
#define strncasecmp _strnicmp
#endif
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#endif

// #include <new>

// #define ELOG_CACHE_LINE std::hardware_destructive_interference_size
#define ELOG_CACHE_LINE 64
#define ELOG_CACHE_ALIGN alignas(ELOG_CACHE_LINE)

/** @def Define a unified function name macro */
#ifdef ELOG_GCC
#define ELOG_FUNCTION __PRETTY_FUNCTION__
#elif defined(ELOG_MSVC) && !defined(ELOG_CLANG)
#define ELOG_FUNCTION __FUNCSIG__
#else
#define ELOG_FUNCTION __func__
#endif

#endif  // __ELOG_DEF_H__