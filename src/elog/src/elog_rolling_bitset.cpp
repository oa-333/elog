#include "elog_rolling_bitset.h"

#include <cassert>
#include <cinttypes>

#include "elog_api.h"
#include "elog_spin_ebo.h"

// TODO: check how to avoid including elog.h when using trace logger (also in GC)

namespace elog {

const uint64_t ELogRollingBitset::WORD_SIZE = sizeof(uint64_t) * 8;
const uint64_t ELogRollingBitset::FULL_WORD = (uint64_t)0xFFFFFFFFFFFFFFFF;
const uint64_t ELogRollingBitset::EMPTY_WORD = (uint64_t)0;

void ELogRollingBitset::markPrefix(uint64_t value) {
    // mark full words
    m_fullWordCount.store(value / WORD_SIZE, std::memory_order_relaxed);

    // mark suffix within the ring buffer
    uint64_t rem = value % WORD_SIZE;
    if (rem > 0) {
        uint64_t bitPattern = (1ull << rem) - 1;
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
    uint64_t fullWordCount = m_fullWordCount.load(std::memory_order_acquire);
    assert(wordId >= fullWordCount);
    while (wordId - fullWordCount >= m_ringSize) {
        // first spin, then do exponential backoff
        se.spinOrBackoff();
        fullWordCount = m_fullWordCount.load(std::memory_order_relaxed);
    }

    // compute the cyclic index of the word and get it
    uint64_t wordRingIndex = wordId % m_ringSize;
    ELogAtomic<uint64_t>& word = m_ring[wordRingIndex];

    // set the correct bit up in lock-free manner (some race is expected for a short while)
    uint64_t wordValue = word.m_atomicValue.load(std::memory_order_acquire);
    uint64_t newWordValue = wordValue | (1ull << wordBitOffset);
    while (!word.m_atomicValue.compare_exchange_strong(wordValue, newWordValue,
                                                       std::memory_order_seq_cst)) {
        wordValue = word.m_atomicValue.load(std::memory_order_relaxed);
        newWordValue = wordValue | (1ull << wordBitOffset);
    }

    // at this point we should check whether the word became full and whether it is the lowest
    // word, as indicated by full word count. If so, then the word must be set back to zero, and
    // only after that the full word count should be incremented (because other threads might be
    // waiting for the word to be released, and that happens when then increment take place, so
    // zeroing the word must happen before that). at this point, we would also like to start a
    // domino effect, since higher words may have already been made full.

    // NOTE: it is wrong to assume that if the current word became full, and that it is the lowest
    // word, then there is no race at all, because of the following scenario:
    // - thread 1 sees that word at absolute index x became full
    // - thread 2 sees that word at absolute index x+1 became full
    // - thread 1 checks full word count and sees it matches it X, so it proceeds to zero word x,
    // and increment full word count to x + 1
    // - thread 2 now sees that full word count equals x + 1, so it proceeds to to zero word x + 1
    // - thread 1 now also sees word x + 1 is full and that full word count is also x + 1
    // - both thread 1 and thread 2 proceed to zero word x + 1

    // this race condition shows that zeroing a word is susceptible to race condition, and therefore
    // CAS is required. So the thread that was able to zero a word through CAS, can safely proceed
    // to normal atomic fetch-add of full word count, because at this point no other thread will be
    // able to CAS that word from full to empty.

    // finish early if possible
    if (newWordValue != FULL_WORD) {
        return;
    }
    if (m_traceLogger != nullptr) {
        ELOG_INFO_EX(m_traceLogger, "Word %" PRIu64 " became full", wordId);
    }

    // check if first word can be collapsed and begin domino effect, but don't surpass max value
    // NOTE: we must load full word count again before making a decision, otherwise we might see a
    // stale value, and we miss incrementing full word count when we should have (e.g. another
    // thread stopped incrementing because it did not see a full word yet, and this thread did not
    // see full word count reaching a higher value, so both abort and we get stuck forever with
    // constant full word count)
    fullWordCount = m_fullWordCount.load(std::memory_order_acquire);
    if (wordId == fullWordCount) {
        while (newWordValue == FULL_WORD) {
            if (m_traceLogger != nullptr) {
                ELOG_INFO_EX(m_traceLogger, "Lowest word %" PRIu64 " became full", fullWordCount);
            }
            // do not forget to first set the word to zero BEFORE advancing full word count, because
            // advancing the full word count releases pending threads that want to insert values
            if (m_ring[fullWordCount % m_ringSize].m_atomicValue.compare_exchange_strong(
                    newWordValue, EMPTY_WORD, std::memory_order_seq_cst)) {
                // we won in the race, so we can safely increment full word count
                m_fullWordCount.fetch_add(1, std::memory_order_relaxed);
            }

            // whether won or lost the race, we try again until lowest word is not full
            fullWordCount = m_fullWordCount.load(std::memory_order_relaxed);
            newWordValue =
                m_ring[fullWordCount % m_ringSize].m_atomicValue.load(std::memory_order_relaxed);
        }
        if (m_traceLogger != nullptr) {
            ELOG_INFO_EX(m_traceLogger, "Domino effect stopped at word %" PRIu64 ": %" PRIx64,
                         fullWordCount, newWordValue);
        }
    }
}

bool ELogRollingBitset::contains(uint64_t value) const {
    // check if found in previous full words
    uint64_t wordId = value / WORD_SIZE;
    uint64_t baseIndex = m_fullWordCount.load(std::memory_order_relaxed);
    if (wordId < baseIndex) {
        return true;
    }

    // get word in the ring
    uint64_t wordRingIndex = wordId % m_ringSize;
    const ELogAtomic<uint64_t>& word = m_ring[wordRingIndex];

    // check if the bit is set
    uint64_t wordBitOffset = value % WORD_SIZE;
    uint64_t wordValue = word.m_atomicValue.load(std::memory_order_relaxed);
    return (wordValue & (1ull << wordBitOffset)) != 0;
}

}  // namespace elog