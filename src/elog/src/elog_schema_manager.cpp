#include "elog_schema_manager.h"

#include "elog_report.h"

// core packages
#include "async/elog_async_schema_handler.h"
#include "file/elog_file_schema_handler.h"
#include "sys/elog_sys_schema_handler.h"

// optional packages
#ifdef ELOG_ENABLE_DB
#include "db/elog_db_schema_handler.h"
#endif
#ifdef ELOG_ENABLE_MSGQ
#include "msgq/elog_msgq_schema_handler.h"
#endif
#ifdef ELOG_ENABLE_RPC
#include "rpc/elog_rpc_schema_handler.h"
#endif
#ifdef ELOG_ENABLE_MON
#include "mon/elog_mon_schema_handler.h"
#endif
#ifdef ELOG_ENABLE_NET
#include "net/elog_net_schema_handler.h"
#endif
#ifdef ELOG_ENABLE_IPC
#include "ipc/elog_ipc_schema_handler.h"
#endif

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogSchemaManager)

const char* ELogSchemaManager::ELOG_SCHEMA_MARKER = "://";
const uint32_t ELogSchemaManager::ELOG_SCHEMA_LEN = 3;
const uint32_t ELOG_MAX_SCHEMA = 20;

static ELogSchemaHandler* sSchemaHandlers[ELOG_MAX_SCHEMA] = {};
static uint32_t sSchemaHandlerCount = 0;
typedef std::unordered_map<std::string, uint32_t> ELogSchemaHandlerMap;
static ELogSchemaHandlerMap sSchemaHandlerMap;

template <typename T>
static bool initSchemaHandler(const char* name) {
    T* handler = new (std::nothrow) T();
    if (handler == nullptr) {
        ELOG_REPORT_ERROR("Failed to create %s schema handler, out of memory", name);
        return false;
    }
    if (!ELogSchemaManager::registerSchemaHandler(name, handler)) {
        ELOG_REPORT_ERROR("Failed to add %s schema handler", name);
        handler->destroy();
        return false;
    }
    return true;
}

bool ELogSchemaManager::initSchemaHandlers() {
    // core packages
    if (!initSchemaHandler<ELogSysSchemaHandler>("sys") ||
        !initSchemaHandler<ELogFileSchemaHandler>("file") ||
        !initSchemaHandler<ELogAsyncSchemaHandler>("async")) {
        return false;
    }

    // optional packages
#ifdef ELOG_ENABLE_DB
    if (!initSchemaHandler<ELogNetSchemaHandler>("db")) {
        return false;
    }
#endif

#ifdef ELOG_ENABLE_MSGQ
    if (!initSchemaHandler<ELogNetSchemaHandler>("msgq")) {
        return false;
    }
#endif

#ifdef ELOG_ENABLE_RPC
    if (!initSchemaHandler<ELogNetSchemaHandler>("rpc")) {
        return false;
    }
#endif

#ifdef ELOG_ENABLE_MON
    if (!initSchemaHandler<ELogNetSchemaHandler>("mon")) {
        return false;
    }
#endif

#ifdef ELOG_ENABLE_NET
    if (!initSchemaHandler<ELogNetSchemaHandler>("net")) {
        return false;
    }
#endif

#ifdef ELOG_ENABLE_IPC
    if (!initSchemaHandler<ELogIpcSchemaHandler>("ipc")) {
        return false;
    }
#endif
    return true;
}

void ELogSchemaManager::termSchemaHandlers() {
    for (uint32_t i = 0; i < sSchemaHandlerCount; ++i) {
        sSchemaHandlers[i]->destroy();
        sSchemaHandlers[i] = nullptr;
    }
    sSchemaHandlerCount = 0;
    sSchemaHandlerMap.clear();
}

bool ELogSchemaManager::registerSchemaHandler(const char* schemeName,
                                              ELogSchemaHandler* schemaHandler) {
    if (sSchemaHandlerCount == ELOG_MAX_SCHEMA) {
        ELOG_REPORT_ERROR("Cannot initialize %s schema handler, out of space", schemeName);
        return false;
    }
    if (!schemaHandler->registerPredefinedProviders()) {
        ELOG_REPORT_ERROR("Failed to register %s schema handler predefined target providers",
                          schemeName);
        return false;
    }
    uint32_t id = sSchemaHandlerCount;
    if (!sSchemaHandlerMap.insert(ELogSchemaHandlerMap::value_type(schemeName, id)).second) {
        ELOG_REPORT_ERROR("Cannot initialize %s schema handler, duplicate scheme name", schemeName);
        return false;
    }
    sSchemaHandlers[sSchemaHandlerCount++] = schemaHandler;
    return true;
}

ELogSchemaHandler* ELogSchemaManager::getSchemaHandler(const char* schemeName) {
    ELogSchemaHandler* schemaHandler = nullptr;
    ELogSchemaHandlerMap::iterator itr = sSchemaHandlerMap.find(schemeName);
    if (itr != sSchemaHandlerMap.end()) {
        schemaHandler = sSchemaHandlers[itr->second];
    }
    return schemaHandler;
}

}  // namespace elog