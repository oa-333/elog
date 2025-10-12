#ifndef __ELOG_GZIP_H__
#define __ELOG_GZIP_H__

#include "elog_def.h"

#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4068)
#endif

#include <gzip/compress.hpp>

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif

#endif  // __ELOG_GZIP_H__
