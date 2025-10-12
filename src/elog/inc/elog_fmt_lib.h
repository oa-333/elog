#ifndef __ELOG_FMT_LIB_H__
#define __ELOG_FMT_LIB_H__

#include "elog_def.h"

// reduce noise coming from fmt lib
#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4582 4623 4625 4626 5027 5026)
#endif

#include <fmt/args.h>
#include <fmt/core.h>
#include <fmt/format.h>

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif

#endif