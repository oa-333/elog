#include "elog_rolling_bitset.h"

#include "elog_spin_ebo.h"
#include "elog_system.h"

namespace elog {

const uint64_t ELogRollingBitset::WORD_SIZE = sizeof(uint64_t) * 8;
const uint64_t ELogRollingBitset::EMPTY_WORD = (uint64_t)0;

void ELogRollingBitset::markPrefix(uint64_t value) {
    // mark full words
    m_emptiedWordCount.store(value / WORD_SIZE, std::memory_order_relaxed);

    // mark suffix within the ring buffer
    uint64_t rem = value % WORD_SIZE;
    if (rem > 0) {
        uint64_t bitPattern = (1 << rem) - 1;
        ELogAtomic<uint64_t>& word = m_ring[0];
        word.m_atomicValue.store(bitPattern, std::memory_order_relaxed);
    }
}

void ELogRollingBitset::insert(uint64_t value) {
    // get global position of the word and the bit offset within the target word
    uint64_t wordId = value / WORD_SIZE;
    uint64_t wordBitOffset = value % WORD_SIZE;

    // wait until ring catches up
    // NOTE: it is possible that due to some race conditions, the minimum is not fully up to date,
    // so we try here to increment it as well
    ELogSpinEbo se;
    uint64_t baseId = m_emptiedWordCount.load(std::memory_order_acquire);
    while (wordId - baseId >= m_ringSize) {
        uint64_t baseIndex = baseId % m_ringSize;
        std::atomic<uint64_t>& word = m_ring[baseIndex].m_atomicValue;
        uint64_t baseIndexValue = word.load(std::memory_order_relaxed);
        if (baseIndexValue == 0) {
            // NOTE: unlike code in remove(), here we may face race condition, so we must use CAS
            if (word.compare_exchange_strong(baseIndexValue, baseIndexValue + 1,
                                             std::memory_order_seq_cst)) {
                break;
            }
        }
        // first spin, then do exponential backoff
        se.spinOrBackoff();
        baseId = m_emptiedWordCount.load(std::memory_order_relaxed);
    }

    // compute the cyclic index of the word and get it
    uint64_t wordRingIndex = wordId % m_ringSize;
    ELogAtomic<uint64_t>& word = m_ring[wordRingIndex];

    // set the correct bit up in lock-free manner (some race is expected for a short while)
    uint64_t wordValue = word.m_atomicValue.load(std::memory_order_acquire);
    uint64_t newWordValue = wordValue | (1ull << wordBitOffset);
    while (!word.m_atomicValue.compare_exchange_weak(wordValue, newWordValue,
                                                     std::memory_order_release)) {
        wordValue = word.m_atomicValue.load(std::memory_order_relaxed);
        newWordValue = wordValue | (1ull << wordBitOffset);
    }

    // update max seen value (race a bit)
    uint64_t maxSeenValue = m_maxSeenValue.load(std::memory_order_acquire);
    while (value > maxSeenValue && !m_maxSeenValue.compare_exchange_strong(
                                       maxSeenValue, value, std::memory_order_seq_cst)) {
        maxSeenValue = m_maxSeenValue.load(std::memory_order_acquire);
    }
}

void ELogRollingBitset::remove(uint64_t value) {
    // get global position of the word and the bit offset within the target word
    uint64_t wordId = value / WORD_SIZE;
    uint64_t wordBitOffset = value % WORD_SIZE;

    // compute the cyclic index of the word and get it
    uint64_t wordRingIndex = wordId % m_ringSize;
    ELogAtomic<uint64_t>& word = m_ring[wordRingIndex];

    /// clear the bit in lock-free manner (some race is expected for a short while)
    uint64_t wordValue = word.m_atomicValue.load(std::memory_order_acquire);
    uint64_t newWordValue = wordValue & ~(1ull << wordBitOffset);
    while (!word.m_atomicValue.compare_exchange_weak(wordValue, newWordValue,
                                                     std::memory_order_release)) {
        wordValue = word.m_atomicValue.load(std::memory_order_relaxed);
        newWordValue = wordValue & ~(1ull << wordBitOffset);
    }

    // check if first word can be collapsed and begin domino effect, but don't surpass max value
    uint64_t baseIndex = m_emptiedWordCount.load(std::memory_order_acquire);
    if (wordId == baseIndex && newWordValue == EMPTY_WORD) {
        if (m_traceLogger != nullptr) {
            ELOG_INFO_EX(m_traceLogger, "Word %" PRIu64 " became empty", baseIndex);
        }
        uint32_t maxWordValue = (wordId + 1) * WORD_SIZE - 1;
        uint64_t maxSeenValue = m_maxSeenValue.load(std::memory_order_relaxed);
        while ((maxWordValue <= maxSeenValue) && (newWordValue == EMPTY_WORD)) {
            // NOTE: there is no race here, since only one thread can reach an empty word
            uint64_t wordCount = m_emptiedWordCount.fetch_add(1, std::memory_order_relaxed);
            if (m_traceLogger != nullptr) {
                ELOG_INFO_EX(m_traceLogger, "Emptied word count advanced to %" PRIu64,
                             (wordCount + 1));
            }
            ++wordId;
            newWordValue =
                m_ring[wordId % m_ringSize].m_atomicValue.load(std::memory_order_relaxed);
            maxWordValue = (wordId + 1) * WORD_SIZE - 1;
        }
    }
}

bool ELogRollingBitset::contains(uint64_t value) const {
    // check if found in previous full words
    uint64_t wordId = value / WORD_SIZE;
    uint64_t baseIndex = m_emptiedWordCount.load(std::memory_order_relaxed);
    if (wordId < baseIndex) {
        return true;
    }

    // get word in the ring
    uint64_t wordRingIndex = wordId % m_ringSize;
    const ELogAtomic<uint64_t>& word = m_ring[wordRingIndex];

    // check if the bit is set
    uint64_t wordBitOffset = value % WORD_SIZE;
    uint64_t wordValue = word.m_atomicValue.load(std::memory_order_relaxed);
    return (wordValue & (1 << wordBitOffset)) != 0;
}

}  // namespace elog