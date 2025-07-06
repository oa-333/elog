#ifndef __ELOG_CONCURRENT_RING_BUFFER_H__
#define __ELOG_CONCURRENT_RING_BUFFER_H__

#include <atomic>
#include <cassert>
#include <cstdint>

#include "elog_aligned_alloc.h"
#include "elog_def.h"

namespace elog {

template <typename T>
class ELogConcurrentRingBuffer {
public:
    ELogConcurrentRingBuffer() : m_ringBuffer(nullptr), m_ringBufferSize(0) {}
    ~ELogConcurrentRingBuffer() {}

    bool initialize(uint32_t ringBufferSize) {
        m_ringBuffer = elogAlignedAllocObjectArray<EntryData>(ELOG_CACHE_LINE, ringBufferSize);
        if (m_ringBuffer == nullptr) {
            return false;
        }
        m_ringBufferSize = ringBufferSize;
        return true;
    }

    void terminate() {
        if (m_ringBuffer != nullptr) {
            elogAlignedFreeObjectArray<EntryData>(m_ringBuffer, m_ringBufferSize);
            m_ringBuffer = nullptr;
        }
    }

    inline size_t size() const {
        uint64_t writePos = m_writePos.load(std::memory_order_relaxed);
        uint64_t readPos = m_readPos.load(std::memory_order_relaxed);
        return writePos - readPos;
    }

    inline bool empty() const { return size() == 0; }

    inline const T& front() const {
        uint64_t readPos = m_readPos.load(std::memory_order_relaxed);
        uint64_t index = readPos % m_ringBufferSize;
        EntryData& entryData = m_ringBuffer[index];
        return entryData.m_data;
    }

    inline T& front() {
        uint64_t readPos = m_readPos.load(std::memory_order_relaxed);
        uint64_t index = readPos % m_ringBufferSize;
        EntryData& entryData = m_ringBuffer[index];
        return entryData.m_data;
    }

    inline const T& back() const {
        uint64_t writePos = m_writePos.load(std::memory_order_relaxed);
        uint64_t index = (writePos + m_ringBufferSize - 1) % m_ringBufferSize;
        EntryData& entryData = m_ringBuffer[index];
        return entryData.m_data;
    }

    inline T& back() {
        uint64_t writePos = m_writePos.load(std::memory_order_relaxed);
        uint64_t index = (writePos + m_ringBufferSize - 1) % m_ringBufferSize;
        EntryData& entryData = m_ringBuffer[index];
        return entryData.m_data;
    }

    void push(const T& data) {
        uint64_t writePos = m_writePos.fetch_add(1, std::memory_order_acquire);
        uint64_t readPos = m_readPos.load(std::memory_order_relaxed);

        // wait until there is no other writer contending for the same entry
        while (writePos - readPos >= m_ringBufferSize) {
            CPU_RELAX;
            readPos = m_readPos.load(std::memory_order_relaxed);
        }
        EntryData& entryData = m_ringBuffer[writePos % m_ringBufferSize];
        EntryState entryState = entryData.m_entryState.load(std::memory_order_seq_cst);

        // now wait for entry to become vacant
        while (entryState != ES_VACANT) {
            CPU_RELAX;
            entryState = entryData.m_entryState.load(std::memory_order_relaxed);
        }
        assert(entryState == ES_VACANT);

        entryData.m_entryState.store(ES_WRITING, std::memory_order_seq_cst);
        entryData.m_data = data;
        entryData.m_entryState.store(ES_READY, std::memory_order_release);
    }

    void pop() {
        // get read/write pos
        uint64_t writePos = m_writePos.load(std::memory_order_relaxed);
        uint64_t readPos = m_readPos.load(std::memory_order_relaxed);

        // check if there is a new item
        if (writePos == readPos) {
            // silently ignore request, but issue assert on debug builds
            assert(false);
            return;
        }

        // wait until record is ready for reading
        EntryData& entryData = m_ringBuffer[readPos % m_ringBufferSize];
        EntryState entryState = entryData.m_entryState.load(std::memory_order_relaxed);
        // uint32_t localSpinCount = SPIN_COUNT_INIT;
        while (entryState != ES_READY) {
            // cpu relax then try again
            // NOTE: this degrades performance, not clear yet why
            // CPU_RELAX;
            entryState = entryData.m_entryState.load(std::memory_order_relaxed);
            // we don't spin/back-off here since the state change is expected to happen
            // immediately

            // spin and exponential backoff
            // for (uint32_t spin = 0; spin < localSpinCount; ++spin);
            // localSpinCount *= 2;
        }

        // set record in reading state
        assert(entryState == ES_READY);
        if (!entryData.m_entryState.compare_exchange_strong(entryState, ES_READING,
                                                            std::memory_order_relaxed)) {
            assert(false);
        }

        // change state back to vacant and update read pos
        entryData.m_entryState.store(ES_VACANT, std::memory_order_relaxed);
        m_readPos.fetch_add(1, std::memory_order_relaxed);
    }

    inline T& operator[](uint64_t index) {
        EntryData& entryData = m_ringBuffer[index];
        return entryData.m_data;
    }

    inline const T& operator[](uint64_t index) const {
        const EntryData& entryData = m_ringBuffer[index];
        return entryData.m_data;
    }

private:
    enum EntryState : uint64_t { ES_VACANT, ES_WRITING, ES_READY, ES_READING };
    struct EntryData {
        std::atomic<EntryState> m_entryState;
        T m_data;
        // TODO: what about struct alignment?

        EntryData() : m_entryState(ES_VACANT) {}
        ~EntryData() {}

        inline void setData(const T& data) { m_data = data; }
    };

    ELOG_CACHE_ALIGN EntryData* m_ringBuffer;
    uint64_t m_ringBufferSize;

    // NOTE: write pos is usually very noisy, so we don't want it to affect read pos, which usually
    // is much slower, therefore, we put read pos in a separate cache line
    // in addition, ring buffer ptr, buffer array ptr and size are not changing, so they are good
    // cache candidates, so we would like to keep them unaffected as well, so we put write pos also
    // in it sown cache line
    ELOG_CACHE_ALIGN std::atomic<uint64_t> m_writePos;
    ELOG_CACHE_ALIGN std::atomic<uint64_t> m_readPos;
};

}  // namespace elog

#endif