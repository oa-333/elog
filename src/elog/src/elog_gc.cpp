#include "elog_gc.h"

#include <bit>

#include "elog_common.h"
#include "elog_error.h"
// #include "elog_logger.h"
#include "elog_system.h"

#define ELOG_INVALID_GC_SLOT_ID ((uint64_t)-1)
#define ELOG_WORD_SIZE sizeof(uint64_t)
#define ELOG_MAX_THREADS 8192

// TODO: this GC needs much more testing

namespace elog {

// TODO: using global slot id per-thread does not allow to use more than one GC, so either make the
// GC a singleton, or use some per-thread array for that, so each GC can manage its own per-thread
// slot id. Currently we don't have use for GC except for group flush, so this is not fixed for now
static thread_local uint64_t sCurrentThreadGCSlotId = ELOG_INVALID_GC_SLOT_ID;

bool ELogGC::initialize(const char* name, uint32_t maxThreads, uint32_t gcFrequency) {
    if (maxThreads > ELOG_MAX_THREADS) {
        ELOG_REPORT_ERROR(
            "Cannot initialize %s garbage collector, maximum number of threads %u exceeds allowed "
            "limit %u",
            name, maxThreads, (unsigned)ELOG_MAX_THREADS);
        return false;
    }
    m_name = name;
    m_gcFrequency = gcFrequency;
    m_retireCount = 0;
    uint32_t wordCount = (maxThreads + ELOG_WORD_SIZE - 1) / ELOG_WORD_SIZE;
    m_epochSet.resizeRing(wordCount + 1);
    m_objectLists.resize(maxThreads);
    m_activeLists.resize(wordCount);
    if (!elogCreateTls(m_tlsKey, onThreadExit)) {
        ELOG_REPORT_ERROR("Failed to create TLS key used for GC thread exit notification");
        return false;
    }
    return true;
}

bool ELogGC::destroy() {
    // destroy TLS key
    if (!elogDestroyTls(m_tlsKey)) {
        ELOG_REPORT_ERROR("Failed to destroy TLS key used for GC thread exit notification");
        return false;
    }

    // recycle all lists
    for (ManagedObjectList& objectList : m_objectLists) {
        recycleObjectList(objectList.m_head.m_atomicValue.load(std::memory_order_relaxed));
    }
    return true;
}

void ELogGC::beginEpoch(uint64_t epoch) {
    // there is no need to record transaction start, right?
}

void ELogGC::endEpoch(uint64_t epoch) {
    // mark a finished transaction epoch
    m_epochSet.insert(epoch);

    uint64_t retireCount = m_retireCount.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((retireCount % m_gcFrequency) == 0) {
        recycleRetiredObjects();
    }
}

bool ELogGC::retire(ELogManagedObject* object, uint64_t epoch) {
    // obtain current thead slot on-demand
    // consider release slot if we can get thread-finish event (we have already win32 support for
    // that, we can use special TLS destructor for that on Linux)
    // then just add the group to an internal data structure
    uint64_t slotId = sCurrentThreadGCSlotId;
    if (slotId == ELOG_INVALID_GC_SLOT_ID) {
        slotId = obtainSlot();
        if (slotId == ELOG_INVALID_GC_SLOT_ID) {
            ELOG_REPORT_WARN("Could not allocate thread slot for %s garbage collection",
                             m_name.c_str());
            return false;
        }
        sCurrentThreadGCSlotId = slotId;
    }

    // allocate new list item to push on head
    if (m_traceLogger != nullptr) {
        ELOG_INFO_EX(m_traceLogger, "Retiring object %p on epoch %" PRIu64, object, epoch);
    }

    // push on head
    object->setRetireEpoch(epoch);
    ManagedObjectList& objectList = m_objectLists[slotId];
    std::atomic<ELogManagedObject*>& listHead = objectList.m_head.m_atomicValue;
    ELogManagedObject* head = listHead.load(std::memory_order_acquire);
    object->setNext(head);

    // race until we succeed
    while (!listHead.compare_exchange_strong(head, object, std::memory_order_seq_cst)) {
        head = listHead.load(std::memory_order_relaxed);
        object->setNext(head);
    }

    return true;
}

void ELogGC::recycleRetiredObjects() {
    // the minimum value here represents the CONSECUTIVE number of transactions that finished,
    // starting from epoch 0. in effect, the minimum value matches the minimum active transaction
    // epoch, so anything below that can be retired. we moderate GC access as configured.
    uint64_t minActiveEpoch = m_epochSet.queryFullPrefix();
    if (minActiveEpoch == 0) {
        // no transaction finished at all up until now
        return;
    }

    // traverse all lists and search for objects eligible for recycling
    // we use lock-free bit-set to tell quickly which lists are active
    // we do so in 64-bit batches
    if (m_traceLogger != nullptr) {
        ELOG_INFO_EX(m_traceLogger, "Recycling objects by min-active-epoch %" PRIu64,
                     minActiveEpoch);
    }
    uint64_t listCount = m_objectLists.size();
    uint64_t maxActiveWord = m_maxActiveWord.load(std::memory_order_relaxed);
    for (uint64_t wordIndex = 0; wordIndex <= maxActiveWord; ++wordIndex) {
        // each bitset word has several bits raised, each for one thread
        // we use countr_zero to find out first non-zero bit and then reset it and find the next
        uint64_t word = m_activeLists[wordIndex].m_atomicValue.load(std::memory_order_relaxed);
        while (word != 0) {
            int bitOffset = std::countr_zero(word);
            uint64_t listIndex = wordIndex * ELOG_WORD_SIZE + bitOffset;
            if (listIndex >= listCount) {
                // be careful with last word not to exceed list count
                return;
            }

            // process current list, skip if currently being recycled by another thread
            processObjectList(m_objectLists[listIndex], minActiveEpoch);

            // reset bit
            word &= ~(1ull << bitOffset);
        }
    }
}

void ELogGC::processObjectList(ManagedObjectList& objectList, uint64_t minActiveEpoch) {
    std::atomic<uint64_t>& recyclingAtomic = objectList.m_recycling.m_atomicValue;
    uint64_t recycling = recyclingAtomic.load(std::memory_order_acquire);
    if (recycling ||
        !recyclingAtomic.compare_exchange_strong(recycling, 1, std::memory_order_seq_cst)) {
        // another thread is already recycling this list, so we back off
        return;
    }

    // at this point we may have race here with another thread trying to retire objects
    ELogManagedObject* itr = objectList.m_head.m_atomicValue.load(std::memory_order_relaxed);

    // NOTE: tackle list head case separately
    bool recycled = false;
    if (itr != nullptr && itr->getRetireEpoch() < minActiveEpoch) {
        // entire list can be recycled, but we need to be careful due to race
        ELogManagedObject* retireList = itr;
        if (objectList.m_head.m_atomicValue.compare_exchange_strong(itr, nullptr,
                                                                    std::memory_order_seq_cst)) {
            recycleObjectList(retireList);
            recycled = true;
        }
    }

    if (!recycled) {
        while (itr != nullptr) {
            // skip head item
            ELogManagedObject* next = itr->getNext();
            if (next != nullptr && next->getRetireEpoch() < minActiveEpoch) {
                // detach list suffix (save it first, because CAS can change its value)
                // NOTE: there is no race here at all, because no other thread is recycling the
                // list, and the thread that retires object to this list can only push objects on
                // list head, so the suffix is not affected
                ELogManagedObject* retireList = next;
                if (itr->detachSuffix(next)) {
                    recycleObjectList(retireList);
                }
                // however things turned out, we are done with this list
                break;
            } else {
                itr = next;
            }
        }
    }

    // reset recycling flag
    recyclingAtomic.store(0, std::memory_order_release);
}

void ELogGC::onThreadExit(void* param) {
    if (sCurrentThreadGCSlotId != ELOG_INVALID_GC_SLOT_ID) {
        ELogGC* gc = (ELogGC*)param;
        gc->setListInactive(sCurrentThreadGCSlotId);
    }
}

uint64_t ELogGC::obtainSlot() {
    uint64_t currentThreadId = getCurrentThreadId();
    for (uint64_t i = 0; i < m_objectLists.size(); ++i) {
        ManagedObjectList& objectList = m_objectLists[i];
        std::atomic<uint64_t>& ownerThreadIdAtomic = objectList.m_ownerThreadId.m_atomicValue;
        uint64_t ownerThreadId = ownerThreadIdAtomic.load(std::memory_order_relaxed);
        if (ownerThreadId == 0) {
            if (ownerThreadIdAtomic.compare_exchange_strong(ownerThreadId, currentThreadId,
                                                            std::memory_order_seq_cst)) {
                // mark list as active
                setListActive(i);
                return i;
            }
        }
    }
    return ELOG_INVALID_GC_SLOT_ID;
}

void ELogGC::setListActive(uint64_t slotId) {
    uint64_t wordIndex = slotId / ELOG_WORD_SIZE;
    uint64_t wordOffset = slotId % ELOG_WORD_SIZE;
    std::atomic<uint64_t>& wordAtomic = m_activeLists[wordIndex].m_atomicValue;
    uint64_t word = wordAtomic.load(std::memory_order_acquire);
    uint64_t newWord = word | (1ull << wordOffset);
    while (!wordAtomic.compare_exchange_strong(word, newWord, std::memory_order_seq_cst)) {
        word = wordAtomic.load(std::memory_order_relaxed);
        newWord = word | (1ull << wordOffset);
    }

    // we might be racing with someone else, we do so as long as we have a higher index
    uint64_t maxActiveWord = m_maxActiveWord.load(std::memory_order_relaxed);
    while (wordIndex > maxActiveWord) {
        if (m_maxActiveWord.compare_exchange_strong(maxActiveWord, wordIndex,
                                                    std::memory_order_seq_cst)) {
            break;
        }
        maxActiveWord = m_maxActiveWord.load(std::memory_order_relaxed);
    }

    // register cleanup for current thread, so we could set the current thread is not active
    // anymore. we use TLS dtor for that, and set the TLS value as the this pointer, since there
    // could be many GC instances. the destructor function checks if slot id for current thread is
    // initialized, and then marks the slot as free
    (void)elogSetTls(m_tlsKey, this);
}

void ELogGC::setListInactive(uint64_t slotId) {
    uint64_t wordIndex = slotId / ELOG_WORD_SIZE;
    uint64_t wordOffset = slotId % ELOG_WORD_SIZE;
    std::atomic<uint64_t>& wordAtomic = m_activeLists[wordIndex].m_atomicValue;
    uint64_t word = wordAtomic.load(std::memory_order_acquire);
    uint64_t newWord = word & ~(1ull << wordOffset);
    while (!wordAtomic.compare_exchange_strong(word, newWord, std::memory_order_seq_cst)) {
        word = wordAtomic.load(std::memory_order_relaxed);
        newWord = word & ~(1ull << wordOffset);
    }

    // if word is non-zero, or word index is zero we are done
    if (newWord != 0 || wordIndex == 0) {
        return;
    }

    // update max active word
    uint64_t maxActiveWord = m_maxActiveWord.load(std::memory_order_acquire);
    if (wordIndex == maxActiveWord) {
        // move backwords until we find a non-zero word
        uint64_t maxWordIndex = wordIndex - 1;
        bool done = false;
        while (!done) {
            if (m_activeLists[maxWordIndex].m_atomicValue.load(std::memory_order_relaxed) != 0ull) {
                // try CAS
                m_maxActiveWord.compare_exchange_strong(maxActiveWord, maxWordIndex,
                                                        std::memory_order_seq_cst);
                // whatever happened we don't really care
                // if we succeeded then we were able to update the max word
                // if we failed, then someone else succeeded, so we don't need to do that
                break;
            } else if (maxWordIndex == 0) {
                done = true;
            } else {
                --maxWordIndex;
            }
        }
    }
}

bool ELogGC::isListActive(uint64_t slotId) {
    uint64_t wordIndex = slotId / ELOG_WORD_SIZE;
    uint64_t wordOffset = slotId % ELOG_WORD_SIZE;
    uint64_t word = m_activeLists[wordIndex].m_atomicValue.load(std::memory_order_acquire);
    return word & (1ull << wordOffset);
}

void ELogGC::recycleObjectList(ELogManagedObject* itr) {
    while (itr != nullptr) {
        ELogManagedObject* next = itr->getNext();
        if (m_traceLogger != nullptr) {
            ELOG_INFO_EX(m_traceLogger, "Recycling object %p", itr);
        }
        delete itr;
        itr = next;
    }
}

}  // namespace elog
