#include "elog_db_target.h"

#include "elog_error.h"

// Threading model design
// ======================
// with no-lock thread model all calls are direct: start, stop, log
// no special object management is required.
// with lock thread model all calls are inside a lock: start, stop, log
// no special object management is required
// with conn-per-thread model, start becomes some empty initialization, and each thread calling
// **log** (not start) needs to check if it has been initialized and if not then initialize
// on-demand. no locking is involved, only thread local data. When calling stop during app
// terminate, there should be an ability to close all open connections, so they must exist on some
// array. For this reason, each thread allocates its db objects on some allocated slot. Also, when a
// thread terminates (we can get a hook for that via TLS destructor), we should free its database
// objects automatically, and release its slot. no safety guards are placed to avoid calls after
// terminate() is called. It is the user code's responsibility. in order to have a unified model,
// all derived classes should simply implement working with an opaque database object container.
// They should support allocate-mem, free-mem, init-db-objects, destroy-db-objects. The actual use
// will be according to the threading model. another call is exec-insert that operates on the opaque
// object.

// DB Reconnect Design
// ===================
// Whenever error is encountered a back-ground thread is launched to do reconnect. This is simple
// with a single connection. But with connection-per-thread model it is more complex. First, it is
// quite possible that not all threads have encountered error, but only part of them. Second, the
// reconnect thread should attempt reconnect for all faulty connections. Third, we would like to
// avoid launching a reconnect thread per connection. So one reconnect thread for all connections,
// but only for those that are known to be faulty. Therefore, each slot should contain connection
// state, and the reconnect thread should check this state. Also each slot has a "used" flag.
// Another point is that the reconnect thread should not be launched each time whenever there is a
// disconnect, as that might happen several times (once for each logging thread) during bad db
// communication period. So instead, the reconnect thread should be dormant, and notified to wake up
// each time some connection gets faulty. The reconnect period should be controlled by the caller
// and also be configurable. When the reconnect thread wakes up, it goes through all slots, and for
// each used slot it checks the connection state. If it is disconnected it attempts to reconnect it.
// If succeeded, then the connected flag (atomic) is set to connected state.
// This way, when logging thread attempt to log, then see the connection state in their slot and
// continue to logging. The reconnect thread is woken up via condition variable, so it is fully
// dormant if everything is ok.

// Summary
// =======
// start() --> on none and locked model allocate slot if needed and establish db connection
//             on conn-per-thread nothing happens except for reconnect thread launch
// log() --> on none first check if should log and if connected then just log message
//           on locked, first lock then do like none
//           on conn-per-thread allocate slot if needed, db conn if needed (essentially execute
//           start() logic), then do like none
//           on failure, mark self slot as disconnected and trigger reconnect thread
// stop() --> on none and locked cleanup db objects and deallocate slot
//            on conn-per-thread cleanup all slots
//
// reconnect() --> go through all slots, for each disconnected slot call connect with db object, it
//                 connect succeeded then mark as connected
//
// reusable operations:
//  slot-id alloc-slot()
//  void free-slot(slot-id)
//  void* alloc-db-object()
//  void destroy-db-object(void*)
//  bool connectDb(void*)
//  void cleanupDb(void*)

namespace elog {

static thread_local int sThreadSlotId = -1;

bool ELogDbTarget::startLogTarget() {
    if (m_threadModel == ThreadModel::TM_CONN_PER_THREAD) {
        m_threadSlots.resize(m_maxThreads);
    } else {
        m_threadSlots.resize(1);
    }

    // parse the statement with log record field selector tokens
    // this builds a processed statement text with questions dollars instead of log record field
    // references, and also prepares the field selector array
    // NOTE: we do initialization here rather than in start(), because the latter is called
    // repeatedly by the reconnect task, and this needs to be done only once
    if (!parseInsertStatement(m_rawInsertStatement)) {
        ELOG_REPORT_ERROR("Failed to parse insert statement: %s", m_rawInsertStatement.c_str());
    }
    m_formatter.getParamTypes(m_paramTypes);
    initDbTarget();

    // in single slot mode we allocate slot, try to connect
    if (m_threadModel != ThreadModel::TM_CONN_PER_THREAD) {
        int slotId = -1;
        if (!initConnection(slotId)) {
            return false;
        }
        if (slotId != 0) {
            ELOG_REPORT_ERROR("Internal error, expected slot id 0, got instead %d", slotId);
            return false;
        }
    }
    // NOTE: on conn-per-thread mode initialization is postponed to log()

    startReconnect();
    return true;
}

bool ELogDbTarget::stopLogTarget() {
    // first stop reconnect thread
    stopReconnect();

    // now disconnect all clients and cleanup
    for (uint32_t i = 0; i < m_threadSlots.size(); ++i) {
        ThreadSlot& slot = m_threadSlots[i];
        if (slot.m_isUsed.load(std::memory_order_relaxed)) {
            if (slot.m_isConnected.load(std::memory_order_relaxed)) {
                if (slot.m_dbData != nullptr) {
                    if (!disconnectDb(slot.m_dbData)) {
                        ELOG_REPORT_ERROR("Failed to cleanup database object at slot %u", i);
                    } else {
                        freeDbData(slot.m_dbData);
                        slot.m_dbData = nullptr;
                    }
                }
            }
        }
    }
    m_threadSlots.clear();
    return true;
}

void ELogDbTarget::log(const ELogRecord& logRecord) {
    if (!shouldLog(logRecord)) {
        return;
    }

    int slotId = 0;
    if (m_threadModel == ThreadModel::TM_CONN_PER_THREAD) {
        slotId = sThreadSlotId;
        if (slotId == -1) {
            if (!initConnection(slotId)) {
                ELOG_REPORT_ERROR("Failed to initialize DB connection for current thread");
                return;
            }
        }
    }

    // check if connected to database, otherwise discard log record
    // (wait until reconnected in the background)
    if (!isConnected(slotId)) {
        return;
    }
    ThreadSlot& slot = m_threadSlots[slotId];

    bool res = true;
    if (m_threadModel == ThreadModel::TM_LOCK) {
        std::unique_lock<std::mutex> lock(m_lock);
        res = execInsert(logRecord, slot.m_dbData);
    } else {
        res = execInsert(logRecord, slot.m_dbData);
    }

    if (!res) {
        disconnectDb(slot.m_dbData);
        wakeUpReconnect();
    }
}

bool ELogDbTarget::parseInsertStatement(const std::string& insertStatement) {
    if (!m_formatter.initialize(insertStatement.c_str())) {
        ELOG_REPORT_ERROR("Failed to parse insert statement: %s", insertStatement.c_str());
        return false;
    }
    return true;
}

int ELogDbTarget::allocSlot() {
    for (uint32_t i = 0; i < m_threadSlots.size(); ++i) {
        bool isUsed = m_threadSlots[i].m_isUsed.load(std::memory_order_relaxed);
        if (!isUsed && m_threadSlots[i].m_isUsed.compare_exchange_strong(
                           isUsed, true, std::memory_order_seq_cst)) {
            return i;
        }
    }
    return -1;
}

void ELogDbTarget::freeSlot(int slot) {
    m_threadSlots[slot].m_isUsed.store(false, std::memory_order_relaxed);
}

bool ELogDbTarget::initConnection(int& slotId) {
    slotId = allocSlot();
    if (slotId == -1) {
        ELOG_REPORT_ERROR("No available thread slot");
        return false;
    }
    // assert(slotId == 0);

    ThreadSlot& slot = m_threadSlots[slotId];
    slot.m_dbData = allocDbData();
    if (slot.m_dbData == nullptr) {
        ELOG_REPORT_ERROR("Failed to allocate DB data, out of memory");
        freeSlot(slotId);
        return false;
    }

    if (!connectDb(slot.m_dbData)) {
        ELOG_REPORT_ERROR("Failed to connect to %s", m_name.c_str());
        freeDbData(slot.m_dbData);
        slot.m_dbData = nullptr;
        freeSlot(slotId);
        return false;
    }

    setConnected(slotId);
    return true;
}

void ELogDbTarget::startReconnect() {
    m_reconnectDbThread = std::thread(&ELogDbTarget::reconnectTask, this);
}

void ELogDbTarget::stopReconnect() {
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_shouldStop = true;
        m_cv.notify_one();
    }
    m_reconnectDbThread.join();
}

void ELogDbTarget::reconnectTask() {
    // now start reconnect attempt until success or ordered to stop
    while (!shouldStop()) {
        for (uint32_t i = 0; i < m_threadSlots.size(); ++i) {
            ThreadSlot& slot = m_threadSlots[i];
            if (slot.m_isUsed.load(std::memory_order_relaxed) &&
                !slot.m_isConnected.load(std::memory_order_relaxed)) {
                if (connectDb(slot.m_dbData)) {
                    slot.m_isConnected.store(true, std::memory_order_relaxed);
                }
            }
        }

        // otherwise wait (interruptible by early wakeup or early stop)
        std::unique_lock<std::mutex> lock(m_lock);
        m_shouldWakeUp = false;
        m_cv.wait_for(lock, std::chrono::milliseconds(m_reconnectTimeoutMillis),
                      [this]() { return m_shouldStop || m_shouldWakeUp; });
    }
}

void ELogDbTarget::wakeUpReconnect() {
    std::unique_lock<std::mutex> lock(m_lock);
    m_shouldWakeUp = true;
    m_cv.notify_one();
}

bool ELogDbTarget::shouldStop() {
    std::unique_lock<std::mutex> lock(m_lock);
    return m_shouldStop;
}

}  // namespace elog
