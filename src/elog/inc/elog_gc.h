#ifndef __ELOG_GC_H__
#define __ELOG_GC_H__

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "elog_atomic.h"
#include "elog_def.h"
#include "elog_managed_object.h"
#include "elog_rolling_bitset.h"
#include "elog_tls.h"

namespace elog {

class ELOG_API ELogLogger;

/** @class A private garbage collector. */
class ELOG_API ELogGC {
public:
    ELogGC()
        : m_name("elog-gc"),
          m_id(0),
          m_gcFrequency(0),
          m_gcPeriodMillis(0),
          m_maxThreads(0),
          m_retireCount(0),
          m_done(false),
          m_traceLogger(nullptr),
          m_tlsKey(ELOG_INVALID_TLS_KEY) {}
    ELogGC(const ELogGC&) = delete;
    ELogGC(ELogGC&&) = delete;
    ELogGC& operator=(const ELogGC&) = delete;
    ~ELogGC() {}

    /** @brief Order GC to trace its operation with this logger. */
    inline void setTraceLogger(ELogLogger* traceLogger) {
        m_traceLogger = traceLogger;
        m_epochSet.setTraceLogger(traceLogger);
    }

    /**
     * @brief Initializes the garbage collector.
     * @param name The garbage collector name (user may define several).
     * @param maxThreads The maximum number of threads that can access the garbage collector
     * concurrently. This value cannot exceed the number of threads configured during initialization
     * of ELog (@see ELogParams::m_maxThreads). Specify zero to use the value passed to ELog during
     * library initialization.
     * @param gcFrequency The frequency of running cooperative GC on caller's side when calling @ref
     * endEpoch() (once per each gcFrequency calls to endEpoch()). Zero disables cooperative garbage
     * collection. Instead a positive garbage collection period must be specified.
     * @param gcPeriodMillis Optionally specify the period in milliseconds of each background
     * garbage collection task, which wakes up and recycles all retired objects that are ready for
     * recycling. This can be specified in addition to cooperative garbage collection frequency.
     * @param gcThreadCount Optionally specify the number of background garbage collection tasks.
     * This parameter is ignored when @ref gcPeriodMillis is zero.
     *
     * @note Either one or both of cooperative and background garbage collectioncan can be
     * configured.
     */
    bool initialize(const char* name, uint32_t maxThreads, uint32_t gcFrequency,
                    uint32_t gcPeriodMillis = 0, uint32_t gcThreadCount = 0);

    /** @brief Destroys the garbage collector. */
    bool destroy();

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
    // linked list of retired objects
    struct ManagedObjectList {
        ELogAtomic<uint64_t> m_ownerThreadId;
        ELogAtomic<uint64_t> m_recycling;
        ELogAtomic<ELogManagedObject*> m_head;
        ManagedObjectList() : m_ownerThreadId(0), m_recycling(0), m_head(nullptr) {}
    };

    // the private garbage collector's name
    std::string m_name;
    uint32_t m_id;  // global GC id, unique among all GCs
    uint32_t m_gcFrequency;
    uint32_t m_gcPeriodMillis;
    uint32_t m_maxThreads;
    std::atomic<uint64_t> m_retireCount;
    std::vector<std::thread> m_gcThreads;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_done;

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

    // TLS key used for getting thread going down notification
    ELogTlsKey m_tlsKey;

    // process retire list and check for possible recycling
    void processObjectList(ManagedObjectList& objectList, uint64_t minActiveEpoch);

    // TLS destructor function used as thread exit notification
    static void onThreadExit(void* param);

    // obtains a GC slot for the current thread
    uint64_t obtainSlot();

    void setListActive(uint64_t slotId);
    void setListInactive(uint64_t slotId);
    bool isListActive(uint64_t slotId);

    // recycles a list suffix
    void recycleObjectList(ELogManagedObject* itr);
};

/** @brief Helper class for managing GC epoch. */
class ELOG_API ELogScopedEpoch {
public:
    ELogScopedEpoch(ELogGC& gc, std::atomic<uint64_t>& epoch) : m_gc(gc), m_epoch(epoch) {
        m_currentEpoch = m_epoch.fetch_add(1, std::memory_order_relaxed);
        m_gc.beginEpoch(epoch);
    }
    ELogScopedEpoch(ELogGC* gc, std::atomic<uint64_t>& epoch) : m_gc(*gc), m_epoch(epoch) {
        m_currentEpoch = m_epoch.fetch_add(1, std::memory_order_relaxed);
        m_gc.beginEpoch(epoch);
    }
    ELogScopedEpoch(const ELogScopedEpoch&) = delete;
    ELogScopedEpoch(ELogScopedEpoch&&) = delete;
    ELogScopedEpoch& operator=(const ELogScopedEpoch&) = delete;
    ~ELogScopedEpoch() { m_gc.endEpoch(m_currentEpoch); }

    inline uint64_t getCurrentEpoch() const { return m_currentEpoch; }

private:
    ELogGC& m_gc;
    std::atomic<uint64_t>& m_epoch;
    uint64_t m_currentEpoch;
};

/** @def Helper macro for managing GC epoch */
#define ELOG_SCOPED_EPOCH(gc, epoch) ELogScopedEpoch scopedEpoch(gc, epoch);

/**
 * @def Helper macro for getting the epoch in the current call. @ref ELOG_SCOPED_EPOCH() must have
 * been used prior in the same function call.
 */
#define ELOG_CURRENT_EPOCH scopedEpoch.getCurrentEpoch()

}  // namespace elog

#endif  // __ELOG_GC_H__