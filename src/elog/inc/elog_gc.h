#ifndef __ELOG_GC_H__
#define __ELOG_GC_H__

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "elog_atomic.h"
#include "elog_def.h"
#include "elog_rolling_bitset.h"

namespace elog {

class ELOG_API ELogLogger;

/** @class A GC managed parent class. */
class ELOG_API ELogManagedObject {
public:
    virtual ~ELogManagedObject() {}
};

/** @class A private garbage collector. */
class ELOG_API ELogGC {
public:
    ELogGC() : m_gcFrequency(0), m_retireCount(0), m_traceLogger(nullptr) {}
    ~ELogGC() {}

    /** @brief Order GC to trace its operation with this logger. */
    inline void setTraceLogger(ELogLogger* traceLogger) {
        m_traceLogger = traceLogger;
        m_epochSet.setTraceLogger(traceLogger);
    }

    /**
     * @brief Initializes the garbage collector.
     * @param name The garbage collector name (user may define several).
     * @param maxThreads The maximum number of threads supported by the garbage collector.
     * @param gcFrequency The frequency of running GC (once per each gcFrequency calls to retire()).
     */
    void initialize(const char* name, uint32_t maxThreads, uint32_t gcFrequency);

    /** @brief Destroys the garbage collector. */
    void destroy();

    /** @brief Let GC know that a transaction began at the given epoch. */
    void beginEpoch(uint64_t epoch);

    /** @brief Let GC know that a transaction with the given epoch just ended. */
    void endEpoch(uint64_t epoch);

    /**
     * @brief Retires an object to the garbage collector, to be recycled on a safe point in the
     * future.
     * @param object The object to retire.
     * @param epoch The epoch at which it will be safe to delete the object.
     * @return The operation result.
     */
    bool retire(ELogManagedObject* object, uint64_t epoch);

    /**
     * @brief Triggers the garbage collector to recycle all object eligible for recycling on all
     * threads (thread-safe).
     */
    void recycleRetiredObjects();

private:
    // A single list item for a retired object
    struct ManagedObjectItem {
        ELogManagedObject* m_object;
        uint64_t m_retireEpoch;
        std::atomic<ManagedObjectItem*> m_next;

        ManagedObjectItem(ELogManagedObject* object = nullptr, uint64_t retireEpoch = 0)
            : m_object(object), m_retireEpoch(retireEpoch) {}
    };

    // linked list of retired objects
    struct ManagedObjectList {
        ELogAtomic<uint64_t> m_ownerThreadId;
        ELogAtomic<uint64_t> m_recycling;
        ELogAtomic<ManagedObjectItem*> m_head;
        ManagedObjectList() : m_ownerThreadId(0), m_recycling(0), m_head(nullptr) {}
    };

    // the private garbage collector's name
    std::string m_name;
    uint32_t m_gcFrequency;
    uint32_t m_padding;
    std::atomic<uint64_t> m_retireCount;

    // manage minimum active epoch
    ELogRollingBitset m_epochSet;

    // per-thread lists of retired objects
    std::vector<ManagedObjectList> m_objectLists;

    // bit-set for quickly telling which lists have items
    std::vector<ELogAtomic<uint64_t>> m_activeLists;

    // remember the maximum word used
    std::atomic<uint64_t> m_maxActiveWord;

    // optional trace logger
    ELogLogger* m_traceLogger;

    // obtains a GC slot for the current thread
    uint64_t obtainSlot();

    void setListActive(uint64_t slotId);
    void setListInactive(uint64_t slotId);
    bool isListActive(uint64_t slotId);

    // recycles a list suffix
    void recycleObjectList(ManagedObjectItem* itr);
};

}  // namespace elog

#endif  // __ELOG_GC_H__