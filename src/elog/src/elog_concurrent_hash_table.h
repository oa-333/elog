#ifndef __ELOG_CONCURRENT_HASH_TABLE_H__
#define __ELOG_CONCURRENT_HASH_TABLE_H__

#include <atomic>
#include <cstdlib>

namespace elog {

// the following implementation is based on ideas from here:
// https://preshing.com/20130605/the-worlds-simplest-lock-free-hash-table/

// disable important but annoying warnings on clang
#ifdef ELOG_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
#endif

template <typename ValueType>
class ELogConcurrentHashTable {
public:
    ELogConcurrentHashTable() : m_entries(nullptr), m_size(0) {}
    ELogConcurrentHashTable(const ELogConcurrentHashTable&) = delete;
    ELogConcurrentHashTable(ELogConcurrentHashTable&&) = delete;
    ELogConcurrentHashTable& operator=(ELogConcurrentHashTable&) = delete;
    ~ELogConcurrentHashTable() {}

    inline bool initialize(size_t size) {
        if (m_size != 0 || m_entries != nullptr) {
            return false;
        }
        // size must be a power of 2, so we make sure
        size_t size2pow = 1;
        while (size2pow < size) {
            size2pow <<= 1;
        }
        m_entries = new (std::nothrow) Entry[size2pow];
        if (m_entries == nullptr) {
            return false;
        }
        m_size = size2pow;
        return true;
    }

    inline void destroy() {
        if (m_entries != nullptr) {
            delete[] m_entries;
            m_entries = nullptr;
            m_size = 0;
        }
    }

    uint32_t setItem(uint64_t key, const ValueType& value) {
        // this ensures any code prior to this is not moved after the fence
        std::atomic_thread_fence(std::memory_order_release);

        for (uint32_t idx = hash64(key);; idx++) {
            idx &= m_size - 1;

            // load key in entry and compare to inserted key
            uint64_t probedKey = m_entries[idx].m_key.load(std::memory_order_relaxed);
            if (probedKey != key) {
                // if key is not zero it means it is occupied by someone else, so we keep going
                if (probedKey != 0) {
                    continue;
                }

                // entry is vacant, but we might be racing with other inserting threads
                if (!m_entries[idx].m_key.compare_exchange_strong(probedKey, key,
                                                                  std::memory_order_relaxed)) {
                    continue;
                }

                // at this point we were able to grab the entry
            }

            // Store the value in this array entry.
            m_entries[idx].m_value = value;
            return idx;
        }
    }

    bool getItem(uint64_t key, ValueType& value) const {
        for (uint32_t idx = hash64(key);; idx++) {
            idx &= m_size - 1;

            // check next entry
            uint64_t probedKey = m_entries[idx].m_key.load(std::memory_order_relaxed);
            if (probedKey == key) {
                // key matches, return value
                value = m_entries[idx].m_value;
                return true;
            }

            // hit empty entry, so linear probing ends here
            if (probedKey == 0) {
                return false;
            }
        }
        // should not reach here, but just for safety
        return false;
    }

    // set items without overriding old value (i.e. returns old value if it exists)
    uint32_t getOrSetItem(uint64_t key, const ValueType& value, bool* found = nullptr) {
        // this ensures any code prior to this is not moved after the fence
        std::atomic_thread_fence(std::memory_order_release);

        for (uint32_t idx = hash64(key);; idx++) {
            idx &= m_size - 1;

            // load key in entry and compare to inserted key
            uint64_t probedKey = m_entries[idx].m_key.load(std::memory_order_relaxed);
            if (probedKey == key) {
                if (found != nullptr) {
                    *found = true;
                }
                return idx;
            }

            // if key is not zero it means it is occupied by someone else, so we keep going
            if (probedKey != 0) {
                continue;
            }

            // entry is vacant, but we might be racing with other inserting threads
            if (!m_entries[idx].m_key.compare_exchange_strong(probedKey, key,
                                                              std::memory_order_relaxed)) {
                continue;
            }

            // at this point we were able to grab the entry

            // Store the value in this array entry.
            m_entries[idx].m_value = value;
            return idx;
        }
    }

    inline const ValueType& getAt(uint32_t idx) const { return m_entries[idx].m_value; }

private:
    struct Entry {
        std::atomic<uint64_t> m_key;
        ValueType m_value;

        Entry() : m_key(0) {}
        Entry(const Entry&) = delete;
        Entry(Entry&&) = delete;
        Entry& operator=(const Entry&) = delete;
        ~Entry() {}
    };
    Entry* m_entries;
    size_t m_size;

    // murmur hash
    inline static uint32_t integerHash(uint32_t h) {
        h ^= h >> 16;
        h *= 0x85ebca6b;
        h ^= h >> 13;
        h *= 0xc2b2ae35;
        h ^= h >> 16;
        return h;
    }

    inline static uint32_t hash64(uint64_t key, uint32_t seed = 0xd92e493e) {
        // 'm' and 'r' are mixing constants generated offline.
        // They're not really 'magic', they just happen to work well.
        const uint32_t m = 0x5bd1e995;
        const uint32_t r = 24;

        // Initialize the hash to a random value
        uint8_t* data = (uint8_t*)&key;
        uint32_t length = sizeof(uint64_t);
        uint32_t h = seed ^ length;
        uint32_t length4 = length / 4;

        const uint32_t mask = 0xFFu;
        for (uint32_t i = 0; i < length4; i++) {
            const uint32_t i4 = i * 4;
            uint32_t k = (((uint32_t)data[i4 + 0u]) & mask) +
                         ((((uint32_t)data[i4 + 1u]) & mask) << 8u) +
                         ((((uint32_t)data[i4 + 2u]) & mask) << 16u) +
                         ((((uint32_t)data[i4 + 3u]) & mask) << 24u);
            k *= m;
            k ^= k >> r;
            k *= m;
            h *= m;
            h ^= k;
        }

        // Handle the last few bytes of the input array
        uint32_t rem = length % 4;
        if (rem == 3) {
            h ^= (((uint32_t)data[(length & ~3u) + 2u]) & mask) << 16u;
        }
        if (rem >= 2) {
            h ^= (((uint32_t)data[(length & ~3u) + 1u]) & mask) << 8u;
        }
        if (rem >= 1) {
            h ^= (((uint32_t)data[length & ~3u]) & mask);
            h *= m;
        }

        h ^= h >> 13;
        h *= m;
        h ^= h >> 15;

        return h;
    }
};

#ifdef ELOG_CLANG
#pragma clang diagnostic pop
#endif

}  // namespace elog

#endif  // __ELOG_CONCURRENT_HASH_TABLE_H__