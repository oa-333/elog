#ifndef __ELOG_MYSQL_DB_HANDLER_H__
#define __ELOG_MYSQL_DB_HANDLER_H__

// #define ELOG_ENABLE_MYSQL_DB_CONNECTOR
#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR

#include "elog_common.h"
#include "elog_target.h"

namespace elog {

class ELogMySqlDbHandler {
public:
    static ELogTarget* loadTarget(const std::string& logTargetCfg, const ELogTargetSpec& targetSpec,
                                  const std::string& connString, const std::string& insertQuery);
};

}  // namespace elog

#endif  // ELOG_ENABLE_MYSQL_DB_CONNECTOR

#endif  // __ELOG_MYSQL_DB_HANDLER_H__