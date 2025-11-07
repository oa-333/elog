#include "db/elog_db_target.h"

#include "elog_field_selector_internal.h"
#include "elog_internal.h"
#include "elog_report.h"

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

ELOG_DECLARE_REPORT_LOGGER(ELogDbTarget)

static const uint32_t ELOG_DB_INVALID_SLOT_ID = 0xFFFFFFFF;

static thread_local uint32_t sThreadSlotId = ELOG_DB_INVALID_SLOT_ID;

ELogDbTarget::ELogDbTarget(const char* dbName, const ELogDbConfig& dbConfig,
                           ELogDbFormatter::QueryStyle queryStyle)
    : ELogTarget("db"),
      m_dbName(dbName),
      m_dbFormatter(nullptr),
      m_rawInsertStatement(dbConfig.m_insertQuery),
      m_queryStyle(queryStyle),
      m_threadModel(dbConfig.m_threadModel),
      m_poolSize(dbConfig.m_poolSize),
      m_reconnectTimeoutMillis(dbConfig.m_reconnectTimeoutMillis),
      m_shouldStop(false),
      m_shouldWakeUp(false) {
    // fix pool size according to thread model
    if (m_threadModel == ELogDbThreadModel::TM_CONN_PER_THREAD) {
        m_poolSize = elog::getMaxThreads();
    } else if (m_threadModel == ELogDbThreadModel::TM_LOCK) {
        m_poolSize = 1;
    }
}

bool ELogDbTarget::startLogTarget() {
    // a log formatter is NOT expected to be installed, since the insert query is used to build the
    // log formatter, and the expected type is ELogDbFormatter
    if (getLogFormatter() != nullptr) {
        ELOG_REPORT_ERROR(
            "Unexpected log format specified for %s DB log target, cannot start log target",
            m_dbName.c_str());
        return false;
    }

    // create the DB formatter and install it
    m_dbFormatter = new (std::nothrow) ELogDbFormatter();
    if (m_dbFormatter == nullptr) {
        ELOG_REPORT_ERROR(
            "Cannot start log target, failed to allocate DB formatter, out of memory");
        return false;
    }
    setLogFormatter(m_dbFormatter);

    // configure the DB formatter
    m_dbFormatter->setQueryStyle(m_queryStyle);

    // parse the statement with log record field selector tokens
    // this builds a processed statement text with questions dollars instead of log record field
    // references, and also prepares the field selector array
    // NOTE: we do initialization here rather than in start(), because the latter is called
    // repeatedly by the reconnect task, and this needs to be done only once
    if (!parseInsertStatement(m_rawInsertStatement)) {
        ELOG_REPORT_ERROR("Failed to parse insert statement: %s", m_rawInsertStatement.c_str());
        return false;
    }
    m_dbFormatter->getParamTypes(m_paramTypes);
    if (!initDbTarget()) {
        ELOG_REPORT_ERROR("Failed to initialize database log target");
        return false;
    }

    // in lock mode and connection pool mode we allocate connections and try to connect
    m_connectionPool.resize(m_poolSize);
    if (m_threadModel != ELogDbThreadModel::TM_CONN_PER_THREAD) {
        if (!initConnectionPool()) {
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
    if (!termConnectionPool()) {
        ELOG_REPORT_ERROR("Failed to terminate the connection pool");
        return false;
    }

    termDbTarget();
    return true;
}

uint32_t ELogDbTarget::writeLogRecord(const ELogRecord& logRecord) {
    uint32_t slotId = ELOG_DB_INVALID_SLOT_ID;
    if (m_threadModel == ELogDbThreadModel::TM_CONN_PER_THREAD) {
        slotId = sThreadSlotId;
        if (slotId == ELOG_DB_INVALID_SLOT_ID) {
            // do a full initialize/connect
            if (!initConnection(slotId)) {
                ELOG_REPORT_ERROR("Failed to initialize DB connection for current thread");
                return 0;
            } else {
                // save slot id
                sThreadSlotId = slotId;
            }
        }
    } else if (m_threadModel == ELogDbThreadModel::TM_CONN_POOL) {
        // grab a free connection or give up
        for (uint32_t i = 0; i < m_connectionPool.size(); ++i) {
            if (m_connectionPool[i].isConnected() && m_connectionPool[i].setExecuting()) {
                slotId = i;
                break;
            }
        }
    } else {
        slotId = 0;
    }

    if (slotId == ELOG_DB_INVALID_SLOT_ID) {
        ELOG_REPORT_TRACE("Failed to obtain valid slot id");
        return 0;
    }

    // check if connected to database, otherwise discard log record
    // (wait until reconnected in the background)
    if (!isConnected(slotId)) {
        ELOG_REPORT_TRACE("Log record dropped, not connected");
        return 0;
    }
    ConnectionData& connData = m_connectionPool[slotId];

    bool res = true;
    if (m_threadModel == ELogDbThreadModel::TM_LOCK) {
        std::unique_lock<std::mutex> lock(m_lock);
        res = execInsert(logRecord, connData.getDbData());
        if (!res) {
            // must be done while lock is still held
            termConnection(slotId);
        }
    } else {
        res = execInsert(logRecord, connData.getDbData());
        if (!res) {
            termConnection(slotId);
        }
    }

    // reset executing flag in connection pool
    if (res && m_threadModel == ELogDbThreadModel::TM_CONN_POOL) {
        connData.setNotExecuting();
    }

    // NOTE: DB log target does not flush, so the byte count is meaningless
    return 0;
}

bool ELogDbTarget::parseInsertStatement(const std::string& insertStatement) {
    if (!m_dbFormatter->initialize(insertStatement.c_str())) {
        ELOG_REPORT_ERROR("Failed to parse insert statement: %s", insertStatement.c_str());
        return false;
    }
    return true;
}

uint32_t ELogDbTarget::allocSlot() {
    for (uint32_t i = 0; i < m_connectionPool.size(); ++i) {
        if (m_connectionPool[i].setUsed()) {
            return i;
        }
    }
    return ELOG_DB_INVALID_SLOT_ID;
}

void ELogDbTarget::freeSlot(uint32_t slot) { m_connectionPool[slot].setUnused(); }

bool ELogDbTarget::initConnection(uint32_t& slotId) {
    if (slotId == ELOG_DB_INVALID_SLOT_ID) {
        slotId = allocSlot();
    }
    if (slotId == ELOG_DB_INVALID_SLOT_ID) {
        ELOG_REPORT_ERROR("No available thread slot");
        return false;
    }

    ConnectionData& connData = m_connectionPool[slotId];

    // NOTE: we have a race with the reconnect task, so be careful
    if (connData.setConnecting()) {
        void* dbData = allocDbData();
        if (dbData == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate DB data, out of memory");
            connData.setDisconnected();
            freeSlot(slotId);
            return false;
        }
        if (!connectDb(dbData)) {
            ELOG_REPORT_ERROR("Failed to connect to %s", m_dbName.c_str());
            freeDbData(dbData);
            connData.setDisconnected();
            freeSlot(slotId);
            return false;
        }
        connData.setDbData(dbData);
        setConnected(slotId);
    } else if (!connData.waitConnect()) {
        ELOG_REPORT_ERROR("Failed to wait for connect to %s", m_dbName.c_str());
        freeSlot(slotId);
        return false;
    }

    return true;
}

void ELogDbTarget::termConnection(uint32_t slotId) {
    // order of operations is important, we do not want the reconnect task to crash, so we first
    // clear the db data, and only after that we announce the connection data is disconnected
    // also in the case of connection pool, we reset the executing flag last, to avoid races
    ConnectionData& connData = m_connectionPool[slotId];
    void* dbData = connData.getDbData();
    disconnectDb(dbData);
    connData.setDisconnected();
    // from this point onward the reconnect task can operate on the connection
    if (m_threadModel == ELogDbThreadModel::TM_CONN_POOL) {
        connData.setNotExecuting();
    }
    // from this point onward, in connection pool mode, other thread can try to grab this connection
    // slot, but only if it got reconnected by the reconnect task
    wakeUpReconnect();
}

bool ELogDbTarget::initConnectionPool() {
    // there is no race here, reconnect task has not started yet
    // we only create the db data object and mark the slot as used but not connected yet
    for (uint32_t i = 0; i < m_connectionPool.size(); ++i) {
        ConnectionData& connData = m_connectionPool[i];
        void* dbData = allocDbData();
        if (dbData == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate DB data, out of memory");
            return false;
        }
        connData.setDbData(dbData);
        connData.setUsed();
    }
    return true;
}

bool ELogDbTarget::termConnectionPool() {
    for (uint32_t i = 0; i < m_connectionPool.size(); ++i) {
        ConnectionData& connData = m_connectionPool[i];
        if (connData.isUsed() && connData.isConnected()) {
            void* dbData = connData.getDbData();
            if (dbData != nullptr) {
                if (!disconnectDb(dbData)) {
                    ELOG_REPORT_ERROR("Failed to cleanup database object at connection %u", i);
                    return false;
                }
                freeDbData(dbData);
                connData.clearDbData();
            }
            connData.setUnused();
        }
    }
    m_connectionPool.clear();
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
    std::string threadName = std::string(getName()) + "-" + m_dbName + "-reconnect-db";
    setCurrentThreadNameField(threadName.c_str());
    while (!shouldStop()) {
        for (uint32_t i = 0; i < m_connectionPool.size(); ++i) {
            ConnectionData& connData = m_connectionPool[i];
            if (connData.isUsed() && connData.isDisconnected()) {
                if (connData.setConnecting()) {
                    if (connectDb(connData.getDbData())) {
                        connData.setConnected();
                    } else {
                        connData.setDisconnected();
                    }
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
