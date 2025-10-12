#include "msg/elog_binary_format_provider.h"

#ifdef ELOG_ENABLE_MSG

#include <io/fixed_input_stream.h>
#include <io/fixed_output_stream.h>

#include <unordered_map>

#include "elog_field_selector_internal.h"
#include "elog_logger.h"
#include "elog_report.h"
#include "msg/elog_msg_internal.h"
#include "msg/elog_proto_receptor.h"

namespace elog {

ELOG_DECLARE_REPORT_LOGGER(ELogBinaryFormatProvider)

ELOG_IMPLEMENT_BINARY_FORMAT_PROVIDER(ELogInternalBinaryFormatProvider)
ELOG_IMPLEMENT_BINARY_FORMAT_PROVIDER(ELogProtobufBinaryFormatProvider)
ELOG_IMPLEMENT_BINARY_FORMAT_PROVIDER(ELogThriftBinaryFormatProvider)
ELOG_IMPLEMENT_BINARY_FORMAT_PROVIDER(ELogAvroBinaryFormatProvider)

/** @def The maximum number of binary format provider types that can be defined in the system. */
#define ELOG_MAX_BINARY_FORMAT_PROVIDER_COUNT 32

// implement binary format provider factory by name with static registration
struct ELogBinaryFormatProviderNameConstructor {
    const char* m_name;
    ELogBinaryFormatProviderConstructor* m_ctor;
};

static ELogBinaryFormatProviderNameConstructor
    sBinaryFormatProviderConstructors[ELOG_MAX_BINARY_FORMAT_PROVIDER_COUNT] = {};
static uint32_t sBinaryFormatProviderConstructorsCount = 0;

typedef std::unordered_map<std::string, ELogBinaryFormatProviderConstructor*>
    ELogBinaryFormatProviderConstructorMap;

static ELogBinaryFormatProviderConstructorMap sBinaryFormatProviderConstructorMap;

void registerBinaryFormatProviderConstructor(const char* name,
                                             ELogBinaryFormatProviderConstructor* constructor) {
    // due to c runtime issues we delay access to unordered map
    if (sBinaryFormatProviderConstructorsCount >= ELOG_MAX_BINARY_FORMAT_PROVIDER_COUNT) {
        ELOG_REPORT_ERROR("Cannot register binary format provider constructor, no space: %s", name);
        exit(1);
    } else {
        sBinaryFormatProviderConstructors[sBinaryFormatProviderConstructorsCount++] = {name,
                                                                                       constructor};
    }
}

static bool applyBinaryFormatProviderConstructorRegistration() {
    for (uint32_t i = 0; i < sBinaryFormatProviderConstructorsCount; ++i) {
        ELogBinaryFormatProviderNameConstructor& nameCtorPair =
            sBinaryFormatProviderConstructors[i];
        if (!sBinaryFormatProviderConstructorMap
                 .insert(ELogBinaryFormatProviderConstructorMap::value_type(nameCtorPair.m_name,
                                                                            nameCtorPair.m_ctor))
                 .second) {
            ELOG_REPORT_ERROR("Duplicate binary format provider identifier: %s",
                              nameCtorPair.m_name);
            return false;
        }
    }
    return true;
}

bool initBinaryFormatProviders() { return applyBinaryFormatProviderConstructorRegistration(); }

void termBinaryFormatProviders() { sBinaryFormatProviderConstructorMap.clear(); }

ELogBinaryFormatProvider* constructBinaryFormatProvider(const char* name,
                                                        commutil::ByteOrder byteOrder) {
    ELogBinaryFormatProviderConstructorMap::iterator itr =
        sBinaryFormatProviderConstructorMap.find(name);
    if (itr == sBinaryFormatProviderConstructorMap.end()) {
        ELOG_REPORT_ERROR("Invalid binary format provider %s: not found", name);
        return nullptr;
    }

    ELogBinaryFormatProviderConstructor* constructor = itr->second;
    ELogBinaryFormatProvider* flushPolicy = constructor->constructBinaryFormatProvider(byteOrder);
    if (flushPolicy == nullptr) {
        ELOG_REPORT_ERROR("Failed to create binary format provider, out of memory");
    }
    return flushPolicy;
}

void getBinaryFormatProviderNameList(std::list<std::string>& nameList) {
    for (const auto& entry : sBinaryFormatProviderConstructorMap) {
        nameList.push_back(entry.first);
    }
}

bool ELogInternalBinaryFormatProvider::logRecordToBuffer(const ELogRecord& logRecord,
                                                         ELogFormatter* formatter,
                                                         ELogMsgBuffer& buffer) {
    // TODO: implement
    return false;
}

bool ELogInternalBinaryFormatProvider::logStatusToBuffer(const ELogStatusMsg& statusMsg,
                                                         ELogMsgBuffer& buffer) {
    // TODO: we actually need here an output stream over an expanding buffer
    const uint32_t BUFFER_SIZE_BYTES = 1024;  // 1k is more than enough
    commutil::FixedOutputStream os(BUFFER_SIZE_BYTES, getByteOrder());
    commutil::ErrorCode rc = statusMsg.serialize(os);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to deserialize status message: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    return true;
}

bool ELogInternalBinaryFormatProvider::logStatusFromBuffer(ELogStatusMsg& statusMsg,
                                                           const char* buffer, uint32_t length) {
    commutil::FixedInputStream is(buffer, length, true, getByteOrder());
    commutil::ErrorCode rc = statusMsg.deserialize(is);
    if (rc != commutil::ErrorCode::E_OK) {
        ELOG_REPORT_ERROR("Failed to deserialize status message: %s",
                          commutil::errorCodeToString(rc));
        return false;
    }
    return true;
}

bool ELogProtobufBinaryFormatProvider::logRecordToBuffer(const ELogRecord& logRecord,
                                                         ELogFormatter* formatter,
                                                         ELogMsgBuffer& buffer) {
    elog_grpc::ELogRecordMsg recordMsg;
    ELogProtoReceptor receptor;
    receptor.setLogRecordMsg(&recordMsg);
    formatter->applyFieldSelectors(logRecord, &receptor);

    // serialize
    size_t size = recordMsg.ByteSizeLong();
    buffer.resize(size);
    return recordMsg.SerializeToArray(&buffer[0], (int)size);
}

bool ELogProtobufBinaryFormatProvider::logStatusToBuffer(const ELogStatusMsg& statusMsg,
                                                         ELogMsgBuffer& buffer) {
    elog_grpc::ELogStatusMsg statusMsgProto;
    statusMsgProto.set_status(statusMsg.getStatus());
    statusMsgProto.set_recordsprocessed(statusMsg.getRecordsProcessed());

    // serialize
    size_t size = statusMsgProto.ByteSizeLong();
    buffer.resize(size);
    return statusMsgProto.SerializeToArray(&buffer[0], (int)size);
}

bool ELogProtobufBinaryFormatProvider::logStatusFromBuffer(ELogStatusMsg& statusMsg,
                                                           const char* buffer, uint32_t length) {
    elog_grpc::ELogStatusMsg protoStatusMsg;
    if (!protoStatusMsg.ParseFromArray(buffer, (int)length)) {
        ELOG_REPORT_ERROR("Failed to deserialize log status message (protobuf)");
        return false;
    }
    if (protoStatusMsg.has_status()) {
        statusMsg.setStatus(protoStatusMsg.status());
    }
    if (protoStatusMsg.has_recordsprocessed()) {
        statusMsg.setRecordsProcessed(protoStatusMsg.recordsprocessed());
    }

    return true;
}

bool ELogThriftBinaryFormatProvider::logRecordToBuffer(const ELogRecord& logRecord,
                                                       ELogFormatter* formatter,
                                                       ELogMsgBuffer& buffer) {
    // TODO: implement
    return false;
}

bool ELogThriftBinaryFormatProvider::logStatusToBuffer(const ELogStatusMsg& statusMsg,
                                                       ELogMsgBuffer& buffer) {
    // TODO: implement
    return false;
}

bool ELogThriftBinaryFormatProvider::logStatusFromBuffer(ELogStatusMsg& statusMsg,
                                                         const char* buffer, uint32_t length) {
    // TODO: implement
    return false;
}

bool ELogAvroBinaryFormatProvider::logRecordToBuffer(const ELogRecord& logRecord,
                                                     ELogFormatter* formatter,
                                                     ELogMsgBuffer& buffer) {
    // TODO: implement
    return false;
}

bool ELogAvroBinaryFormatProvider::logStatusToBuffer(const ELogStatusMsg& statusMsg,
                                                     ELogMsgBuffer& buffer) {
    // TODO: implement
    return false;
}

bool ELogAvroBinaryFormatProvider::logStatusFromBuffer(ELogStatusMsg& statusMsg, const char* buffer,
                                                       uint32_t length) {
    // TODO: implement
    return false;
}

}  // namespace elog

#endif  // ELOG_ENABLE_MSG