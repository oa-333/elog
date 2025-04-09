#ifndef __ELOG_DEF_H__
#define __ELOG_DEF_H__

#ifdef _MSC_VER
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define ELOG_WINDOWS
#define ELOG_MSVC
#define DLL_EXPORT __declspec(dllexport)
#elif defined(__MINGW32__) || defined(__MINGW64__)
#define ELOG_WINDOWS
#define ELOG_MINGW
#define ELOG_GCC
#define DLL_EXPORT
#elif defined(__linux__)
#define ELOG_LINUX
#define ELOG_GCC
#define DLL_EXPORT
#else
#error "Unsupported platform"
#endif

#endif  // __ELOG_DEF_H__