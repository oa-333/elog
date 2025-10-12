#ifndef __ELOG_PROTO_H__
#define __ELOG_PROTO_H__

#include "elog_def.h"

#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4267 4365 4625 4626 4868 5026 5027)
#endif

#include "elog.grpc.pb.h"
#include "elog.pb.h"

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif

#endif