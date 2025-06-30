#include "elog_gc.h"

#include <bit>

#include "elog_common.h"
#include "elog_error.h"
// #include "elog_logger.h"
#include "elog_system.h"

#define ELOG_INVALID_GC_SLOT_ID ((uint64_t)-1)
#define ELOG_WORD_SIZE sizeof(uint64_t)

// TODO: this GC needs much more testing

namespace elog {

static thread_local uint64_t sCurrentThreadGCSlotId = ELOG_INVALID_GC_SLOT_ID;

void ELogGC::initialize(const char* name, uint32_t maxThreads, uint32_t gcFrequency) {
    m_name = name;
    m_gcFrequency = gcFrequency;
    m_retireCount = 0;
    uint64_t wordCount = (maxThreads + ELOG_WORD_SIZE - 1) / ELOG_WORD_SIZE;
    m_epochSet.resizeRing(wordCount + 1);
    m_objectLists.resize(maxThreads);
    m_activeLists.resize(wordCount);
}

void ELogGC::destroy() {
    // recycle all lists
    for (ManagedObjectList& objectList : m_objectLists) {
        recycleObjectList(objectList.m_head.m_atomicValue.load(std::memory_order_relaxed));
    }
#ifdef ELOG_ENABLE_GROUP_FLUSH_GC_TRACE
    resetGCLogger();
#endif
}

void ELogGC::beginEpoch(uint64_t epoch) { m_epochSet.insert(epoch); }

void ELogGC::endEpoch(uint64_t epoch) {
    // mark a finished transaction epoch
    m_epochSet.remove(epoch);

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
    ManagedObjectItem* listItem = new (std::nothrow) ManagedObjectItem(object, epoch);
    if (m_traceLogger != nullptr) {
        ELOG_INFO_EX(m_traceLogger, "Retiring object %p on epoch %" PRIu64, object, epoch);
    }
    if (listItem == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate %s garbage collection list item, out of memory",
                          m_name.c_str());
        return false;
    }

    // push on head
    ManagedObjectList& objectList = m_objectLists[slotId];
    std::atomic<ManagedObjectItem*>& listHead = objectList.m_head.m_atomicValue;
    ManagedObjectItem* head = listHead.load(std::memory_order_acquire);
    listItem->m_next.store(head, std::memory_order_relaxed);

    // race until we succeed
    while (!listHead.compare_exchange_strong(head, listItem, std::memory_order_seq_cst)) {
        head = listHead.load(std::memory_order_relaxed);
        listItem->m_next.store(head, std::memory_order_release);
    }

    return true;
}

void ELogGC::recycleRetiredObjects() {
    // the minimum value here represents the CONSECUTIVE number of transactions that finished,
    // starting from epoch 0. in effect, the minimum value matches the minimum active transaction
    // epoch, so anything below that can be retired. we moderate GC access as configured.
    uint64_t minActiveEpoch = m_epochSet.getMinValue();
    if (minActiveEpoch == 0) {
        // no transaction finished at all up until now
        return;
    }
    uint64_t epoch = minActiveEpoch - 1;

    // go over all thread slots, and for each group/epoch pair release the group if the epoch is
    // large enough
    // this requires some kind of lock-free list, where we push on head, then we traverse from head
    // until we find the point where the epoch is small enough, so we can detach the list suffix,
    // and then release all suffix groups one by one
    // releasing list head is tricky and could be avoided for safety
    // this kind of GC should actually be implemented in separate and can be used as a private GC
    // the list heads can be freed during GC destruction

    // traverse all lists and search for objects eligible for recycling
    // we use lock-free bit-set to tell quickly which lists are active
    // we do so in 64-bit batches
    if (m_traceLogger != nullptr) {
        ELOG_INFO_EX(m_traceLogger, "Recycling objects by epoch %" PRIu64, epoch);
    }
    uint64_t wordCount = m_activeLists.size();
    uint64_t listCount = m_objectLists.size();
    uint64_t maxActiveWord = m_maxActiveWord.load(std::memory_order_relaxed);
    for (uint64_t wordIndex = 0; wordIndex <= maxActiveWord; ++wordIndex) {
        uint64_t word = m_activeLists[wordIndex].m_atomicValue.load(std::memory_order_relaxed);
        // we use count_rzero to find out first non-zero bit and then reset it and find the next
        while (word != 0) {
            uint64_t bitOffset = std::countr_zero(word);
            uint64_t listIndex = wordIndex * ELOG_WORD_SIZE + bitOffset;
            if (listIndex >= listCount) {
                return;
            }

            // process current list, skip if currently being recycled by another thread
            ManagedObjectList& objectList = m_objectLists[listIndex];
            std::atomic<uint64_t>& recyclingAtomic = objectList.m_recycling.m_atomicValue;
            uint64_t recycling = recyclingAtomic.load(std::memory_order_acquire);
            if (!recycling &&
                recyclingAtomic.compare_exchange_strong(recycling, 1, std::memory_order_seq_cst)) {
                ManagedObjectItem* itr =
                    objectList.m_head.m_atomicValue.load(std::memory_order_relaxed);
                while (itr != nullptr) {
                    // we skip head item to avoid nasty race conditions
                    ManagedObjectItem* next = itr->m_next.load(std::memory_order_relaxed);
                    if (next != nullptr && next->m_retireEpoch <= epoch) {
                        // detach list suffix (save it first, because CAS can change its value)
                        ManagedObjectItem* retireList = next;
                        if (itr->m_next.compare_exchange_strong(next, nullptr,
                                                                std::memory_order_seq_cst)) {
                            // we won the race so now we can detach suffix
                            recycleObjectList(retireList);
                        }
                    } else {
                        itr = next;
                    }
                }
                recyclingAtomic.store(0, std::memory_order_release);
            }

            // reset bit
            word &= ~(1 << bitOffset);
        }
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
                // TODO: register cleanup for current thread, we use TLS dtor for that (dtor check
                // if slot id for current thread is initialized, and then marks the slot as free)
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
    uint64_t newWord = word | (1 << wordOffset);
    while (!wordAtomic.compare_exchange_strong(word, newWord, std::memory_order_seq_cst)) {
        word = wordAtomic.load(std::memory_order_relaxed);
        newWord = word | (1 << wordOffset);
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
}

void ELogGC::setListInactive(uint64_t slotId) {
    uint64_t wordIndex = slotId / ELOG_WORD_SIZE;
    uint64_t wordOffset = slotId % ELOG_WORD_SIZE;
    std::atomic<uint64_t>& wordAtomic = m_activeLists[wordIndex].m_atomicValue;
    uint64_t word = wordAtomic.load(std::memory_order_acquire);
    uint64_t newWord = word & ~(1 << wordOffset);
    while (!wordAtomic.compare_exchange_strong(word, newWord, std::memory_order_seq_cst)) {
        word = wordAtomic.load(std::memory_order_relaxed);
        newWord = word & ~(1 << wordOffset);
    }

    // TODO: update max active word...
}

bool ELogGC::isListActive(uint64_t slotId) {
    uint64_t wordIndex = slotId / ELOG_WORD_SIZE;
    uint64_t wordOffset = slotId % ELOG_WORD_SIZE;
    uint64_t word = m_activeLists[wordIndex].m_atomicValue.load(std::memory_order_acquire);
    return word & (1 << wordOffset);
}

void ELogGC::recycleObjectList(ManagedObjectItem* itr) {
    while (itr != nullptr) {
        ELogManagedObject* object = itr->m_object;
        delete object;
        if (m_traceLogger != nullptr) {
            ELOG_INFO_EX(m_traceLogger, "Recycling object %p", object);
        }
        itr->m_object = nullptr;
        ManagedObjectItem* next = itr->m_next.load(std::memory_order_relaxed);
        delete itr;
        itr = next;
    }
}

}  // namespace elog
