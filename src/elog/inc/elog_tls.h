#ifndef __ELOG_TLS_H__
#define __ELOG_TLS_H__

#include "elog_def.h"

#ifndef ELOG_WINDOWS
#include <pthread.h>
#endif

namespace elog {

/** @typedef Thread local storage key type. */
#ifdef ELOG_WINDOWS
typedef unsigned long ELogTlsKey;
#else
typedef pthread_key_t ELogTlsKey;
#endif

/** @def Invalid TLS key value. */
#define ELOG_INVALID_TLS_KEY ((ELogTlsKey) - 1)

/** @typedef TLS destructor function type. */
typedef void (*elogTlsDestructorFunc)(void*);

/**
 * @brief Creates a thread local storage key.
 * @note It is advised to call this function during process initialization.
 * @param[out] key The resulting key, used as index to a slot where the per-thread value is stored.
 * @param dtor Optional destructor to allow per-thread TLS value cleanup. The destructor function
 * will be executed for each thread during thread exit, but only if the TLS value is not null. The
 * destructor parameter is the TLS value of the specific thread that is going down.
 * @return The operation result.
 */
extern bool elogCreateTls(ELogTlsKey& key, elogTlsDestructorFunc dtor = nullptr);

/**
 * @brief Destroys a thread local storage key.
 * @note It is advised to call this function during process destruction, after all other threads
 * have gone down. This will cause a memory leak for the main thread, in case it has thread local
 * value associated with this key, that requires cleanup. In such a case, make sure to explicitly
 * call the destructor function with the current thread's TLS value associates with the key being
 * destroyed BEFORE calling @ref elogDestroyTls().
 * @param key The thread local storage key.
 * @return The operation result.
 */
extern bool elogDestroyTls(ELogTlsKey key);

/**
 * @brief Retrieves the current thread's TLS value associated with the given key.
 * @note Return value of null does not necessarily mean error, but rather that the current thread
 * has not yet initialized its value associated with this TLS key.
 * @param key The thread local storage key.
 * @return void* The return value.
 */
extern void* elogGetTls(ELogTlsKey key);

/**
 * @brief Sets the current thread's TLS value associated with the given key.
 * @param key The thread local storage key.
 * @param value The thread local storage value.
 * @return The operation result.
 */
extern bool elogSetTls(ELogTlsKey key, void* value);

}  // namespace elog

#endif  // __ELOG_TLS_H__