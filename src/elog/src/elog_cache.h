#ifndef __ELOG_CACHE_H__
#define __ELOG_CACHE_H__

#include <cstdint>

#include "elog_common_def.h"
#include "elog_def.h"

namespace elog {

// use 16K entries hash table size for all format messages
// if this is not enough it can be controlled during initialization
#define ELOG_DEFAULT_CACHE_SIZE (16 * 1024)

class ELogCache {
public:
    static ELogCacheEntryId cacheFormatMsg(const char* fmt);
    static const char* getCachedFormatMsg(ELogCacheEntryId entryId);
    static ELogCacheEntryId getOrCacheFormatMsg(const char* fmt);

private:
    static bool initCache(uint32_t cacheSize);
    static void destroyCache();

    // allow special access to these two
    friend bool initGlobals();
    friend void termGlobals();
};

}  // namespace elog

#endif  // __ELOG_CACHE_H__