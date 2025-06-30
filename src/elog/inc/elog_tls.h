#ifndef __ELOG_TLS_H__
#define __ELOG_TLS_H__

#include "elog_def.h"

#ifndef ELOG_WINDOWS
#include <pthread.h>
#endif

namespace elog {
#ifdef ELOG_WINDOWS
typedef unsigned long ELogTlsKey;
#else
typedef pthread_key_t ELogTlsKey;
#endif

#define ELOG_INVALID_TLS_KEY ((ELogTlsKey) - 1)

typedef void (*elogTlsDestructorFunc)(void*);

extern bool elogCreateTls(ELogTlsKey& key, elogTlsDestructorFunc dtor = nullptr);
extern bool elogDestroyTls(ELogTlsKey key);
extern void* elogGetTls(ELogTlsKey key);
extern bool elogSetTls(ELogTlsKey key, void* value);

}  // namespace elog

#endif  // __ELOG_TLS_H__