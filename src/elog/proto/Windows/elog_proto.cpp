#include "elog_proto.h"

#ifdef ELOG_MSVC
#pragma warning(push)
#pragma warning(disable : 4267 4365 4625 4626 4868 5026 5027)
#endif

#include "elog.grpc.pb.cc"
#include "elog.pb.cc"

#ifdef ELOG_MSVC
#pragma warning(pop)
#endif
