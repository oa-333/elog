#include "elog_record_builder.h"

#include <cstring>

namespace elog {

uint32_t ELogRecordBuilder::elog_strncpy(char* dest, const char* src, uint32_t dest_len) {
    uint32_t src_len = strlen(src);
    if (src_len + 1 < dest_len) {
        // copy terminating null as well
        strncpy(dest, src, src_len + 1);
        return src_len;
    }
    // reserve one char for terminating null
    int copy_len = dest_len - 1;
    strncpy(dest, src, copy_len);

    // add terminating null
    dest[copy_len] = 0;

    // return number of bytes copied, excluding terminating null
    return copy_len;
}

}  // namespace elog
