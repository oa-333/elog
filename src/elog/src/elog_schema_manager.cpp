#include "elog_schema_manager.h"

#include "elog_async_schema_handler.h"
#include "elog_db_schema_handler.h"
#include "elog_error.h"
#include "elog_file_schema_handler.h"
#include "elog_msgq_schema_handler.h"
#include "elog_rpc_schema_handler.h"
#include "elog_sys_schema_handler.h"

namespace elog {

const char* ELogSchemaManager::ELOG_SCHEMA_MARKER = "://";
const uint32_t ELogSchemaManager::ELOG_SCHEMA_LEN = 3;
const uint32_t ELOG_MAX_SCHEMA = 20;

static ELogSchemaHandler* sSchemaHandlers[ELOG_MAX_SCHEMA] = {};
static uint32_t sSchemaHandlerCount = 0;
typedef std::unordered_map<std::string, int> ELogSchemaHandlerMap;
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
        delete handler;
        return false;
    }
    return true;
}

bool ELogSchemaManager::initSchemaHandlers() {
    if (!initSchemaHandler<ELogSysSchemaHandler>("sys") ||
        !initSchemaHandler<ELogFileSchemaHandler>("file") ||
        !initSchemaHandler<ELogDbSchemaHandler>("db") ||
        !initSchemaHandler<ELogMsgQSchemaHandler>("msgq") ||
        !initSchemaHandler<ELogAsyncSchemaHandler>("async") ||
        !initSchemaHandler<ELogRpcSchemaHandler>("rpc")) {
        return false;
    }
    return true;
}

void ELogSchemaManager::termSchemaHandlers() {
    for (uint32_t i = 0; i < sSchemaHandlerCount; ++i) {
        delete sSchemaHandlers[i];
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