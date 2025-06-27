#ifndef __ELOG_DEF_H__
#define __ELOG_DEF_H__

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#define ELOG_WINDOWS
#define ELOG_MSVC
#define DLL_EXPORT __declspec(dllexport)
#define DLL_IMPORT __declspec(dllimport)
#ifdef ELOG_DLL
#define ELOG_API DLL_EXPORT
#else
#define ELOG_API DLL_IMPORT
#endif
#elif defined(__MINGW32__) || defined(__MINGW64__)
#define ELOG_WINDOWS
#define ELOG_MINGW
#define ELOG_GCC
#define ELOG_API
#elif defined(__linux__)
#define ELOG_LINUX
#define ELOG_GCC
#define ELOG_API
#else
#error "Unsupported platform"
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

#endif  // __ELOG_DEF_H__