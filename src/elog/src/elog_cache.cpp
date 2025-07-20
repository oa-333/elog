#include "elog_cache.h"

#include "elog_concurrent_hash_table.h"

namespace elog {

typedef ELogConcurrentHashTable<const char*> ELogFormatMsgCache;
static ELogFormatMsgCache sFormatMsgCache;

ELogCacheEntryId ELogCache::cacheFormatMsg(const char* fmt) {
    return sFormatMsgCache.setItem((uint64_t)fmt, fmt);
}

const char* ELogCache::getCachedFormatMsg(ELogCacheEntryId entryId) {
    return (const char*)sFormatMsgCache.getAt(entryId);
}

ELogCacheEntryId ELogCache::getOrCacheFormatMsg(const char* fmt) {
    return sFormatMsgCache.getOrSetItem((uint64_t)fmt, fmt);
}

bool ELogCache::initCache(uint32_t cacheSize) { return sFormatMsgCache.initialize(cacheSize); }

void ELogCache::destroyCache() { sFormatMsgCache.destroy(); }

}  // namespace elog
