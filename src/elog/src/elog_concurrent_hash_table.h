#ifndef __ELOG_CONCURRENT_HASH_TABLE_H__
#define __ELOG_CONCURRENT_HASH_TABLE_H__

#include <atomic>
#include <cstdlib>

namespace elog {

// the following implementation is based on ideas from here:
// https://preshing.com/20130605/the-worlds-simplest-lock-free-hash-table/

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
                value = &m_entries[idx].m_value;
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
        char* data = (char*)&key;
        uint32_t length = sizeof(uint64_t);
        uint32_t h = seed ^ length;
        uint32_t length4 = length / 4;

        for (uint32_t i = 0; i < length4; i++) {
            const uint32_t i4 = i * 4;
            uint32_t k = (data[i4 + 0] & 0xff) + ((data[i4 + 1] & 0xff) << 8) +
                         ((data[i4 + 2] & 0xff) << 16) + ((data[i4 + 3] & 0xff) << 24);
            k *= m;
            k ^= k >> r;
            k *= m;
            h *= m;
            h ^= k;
        }

        // Handle the last few bytes of the input array
        switch (length % 4) {
            case 3:
                h ^= (data[(length & ~3) + 2] & 0xff) << 16;
            case 2:
                h ^= (data[(length & ~3) + 1] & 0xff) << 8;
            case 1:
                h ^= (data[length & ~3] & 0xff);
                h *= m;
        }

        h ^= h >> 13;
        h *= m;
        h ^= h >> 15;

        return h;
    }
};

}  // namespace elog

#endif  // __ELOG_CONCURRENT_HASH_TABLE_H__