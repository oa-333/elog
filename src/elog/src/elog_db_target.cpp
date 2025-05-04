#include "elog_db_target.h"

#include "elog_system.h"

namespace elog {

bool ELogDbTarget::parseInsertStatement(const std::string& insertStatement) {
    if (!m_formatter.initialize(insertStatement.c_str())) {
        ELogSystem::reportError("Failed to parse insert statement: %s", insertStatement.c_str());
        return false;
    }
    return true;
}

}  // namespace elog
