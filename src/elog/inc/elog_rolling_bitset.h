#ifndef __ELOG_ROLLING_BITSET_H__
#define __ELOG_ROLLING_BITSET_H__

#include <bit>
#include <vector>

#include "elog_atomic.h"
#include "elog_def.h"

namespace elog {

class ELOG_API ELogLogger;

/**
 * @brief A lock free rolling bit-set, designed for mostly monotonic increasing values that are
 * inserted then removed. This is mostly used in the context of the minimum epoch problem.
 */
class ELOG_API ELogRollingBitset {
public:
    ELogRollingBitset(uint64_t ringSizeWords = 0)
        : m_ringSize(ringSizeWords), m_fullWordCount(0), m_traceLogger(nullptr) {
        m_ring.resize(m_ringSize, 0);
    }
    ELogRollingBitset(const ELogRollingBitset&) = delete;
    ELogRollingBitset(ELogRollingBitset&&) = delete;
    ELogRollingBitset& operator=(const ELogRollingBitset&) = delete;
    ~ELogRollingBitset() {}

    /** @brief Single word size used by the ring buffer */
    static const uint64_t WORD_SIZE;

    /** @brief Computes the number of words required to contain the given number of bits. */
    static uint64_t computeWordCount(uint64_t bitCount) {
        return (bitCount + WORD_SIZE - 1) / WORD_SIZE * WORD_SIZE;
    }

    /** @brief Order rolling bitset to trace its operation with this logger. */
    void setTraceLogger(ELogLogger* logger) { m_traceLogger = logger; }

    /**
     * @brief Resizes the rolling bit-set's word ring.
     * @param ringSizeWords The desired number of words in the ring.
     */
    inline void resizeRing(uint32_t ringSizeWords) {
        m_ringSize = ringSizeWords;
        m_ring.resize(m_ringSize, 0);
    }

    /**
     * @brief Marks all values, from zero up to and including the given value, as inserted.
     * @note This is NOT a thread-safe call. It should be usually made before starting to use the
     * rolling bit set.
     * @param value The upper bound of the value range to insert.
     */
    void markPrefix(uint64_t value);

    /**
     * @brief Inserts a value into the rolling bit-set. This call may block if the ring buffer is
     * full. In such case busy-wait and exponential back-off is used.
     * @note It is the caller's responsibility to make sure each value, starting from zero, is
     * inserted exactly once.
     * @param value The value to insert.
     */
    void insert(uint64_t value);

    /**
     * @brief Queries whether a value was inserted into the rolling bit-set.
     * @param value The value to query.
     * @return true if the value is found, otherwise false.
     * @note It may be due to race conditions, that the value is concurrently being set, and the
     * call to @ref contains() still returns false. The caller is advised to make the call again in
     * that case, until a true result is returned (assuming values are being inserted continuously).
     */
    bool contains(uint64_t value) const;

    /** @brief Queries the full prefix of inserted values starting from zero. */
    inline uint64_t queryFullPrefix() const {
        uint64_t wordId = m_fullWordCount.load(std::memory_order_relaxed);
        uint64_t wordRingIndex = wordId % m_ringSize;
        uint64_t word = m_ring[wordRingIndex].m_atomicValue.load(std::memory_order_relaxed);
        return wordId * WORD_SIZE + (uint64_t)std::countr_one(word);
    }

private:
    uint64_t m_ringSize;
    std::vector<ELogAtomic<uint64_t>> m_ring;
    std::atomic<uint64_t> m_fullWordCount;
    ELogLogger* m_traceLogger;

    static const uint64_t FULL_WORD;
    static const uint64_t EMPTY_WORD;
};

}  // namespace elog

#endif  // __ELOG_ROLLING_BITSET_H__