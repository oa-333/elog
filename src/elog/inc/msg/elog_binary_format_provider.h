#ifndef __ELOG_BINARY_FORMAT_PROVIDER_H__
#define __ELOG_BINARY_FORMAT_PROVIDER_H__

#ifdef ELOG_ENABLE_MSG

#include <list>
#include <string>
#include <vector>

#include "elog.pb.h"
#include "elog_def.h"
#include "elog_formatter.h"
#include "elog_record.h"
#include "msg/elog_msg.h"

namespace elog {

/** @typedef Message buffer type. */
typedef std::vector<char> ELogMsgBuffer;

/**
 * @class Base class of all binary format provider with default implementation of ELog's internal
 * binary format.
 */
class ELOG_API ELogBinaryFormatProvider {
public:
    virtual ~ELogBinaryFormatProvider() {}

    /**
     * @brief Convert a log records into binary data.
     * @param logRecord The record.
     * @param formatter Log formatter to select fields.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    virtual bool logRecordToBuffer(const ELogRecord& logRecord, ELogFormatter* formatter,
                                   ELogMsgBuffer& buffer) = 0;

    /**
     * @brief Converts log status to binary data.
     * @param statusMsg The status message to serialize.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    virtual bool logStatusToBuffer(const ELogStatusMsg& statusMsg, ELogMsgBuffer& buffer) = 0;

    /**
     * @brief Converts log status from binary data.
     * @param statusMsg The resulting status message.
     * @param buffer The input binary data.
     * @param length The length of the input buffer.
     * @return The operation result.
     */
    virtual bool logStatusFromBuffer(ELogStatusMsg& statusMsg, const char* buffer,
                                     uint32_t length) = 0;

protected:
    ELogBinaryFormatProvider(commutil::ByteOrder byteOrder) : m_byteOrder(byteOrder) {}
    ELogBinaryFormatProvider(ELogBinaryFormatProvider&) = delete;
    ELogBinaryFormatProvider(ELogBinaryFormatProvider&&) = delete;
    ELogBinaryFormatProvider& operator=(const ELogBinaryFormatProvider&) = delete;

    inline commutil::ByteOrder getByteOrder() const { return m_byteOrder; }

private:
    commutil::ByteOrder m_byteOrder;
};

// forward declaration
class ELOG_API ELogBinaryFormatProviderConstructor;

/**
 * @brief Binary format provider constructor registration helper.
 * @param name The binary format provider identifier.
 * @param constructor The binary format provider constructor.
 */
extern ELOG_API void registerBinaryFormatProviderConstructor(
    const char* name, ELogBinaryFormatProviderConstructor* constructor);

/**
 * @brief Utility helper for constructing a binary format provider from type name identifier.
 * @param name The binary format provider identifier.
 * @param byteOrder The byte order to use in binary formatting.
 * @return ELogBinaryFormatProvider* The resulting binary format provider, or null if failed.
 */
extern ELOG_API ELogBinaryFormatProvider* constructBinaryFormatProvider(
    const char* name, commutil::ByteOrder byteOrder);

/** @brief Retrieves the name list of all registered binary format providers. */
extern ELOG_API void getBinaryFormatProviderNameList(std::list<std::string>& nameList);

/** @brief Utility helper class for binary format provider construction. */
class ELOG_API ELogBinaryFormatProviderConstructor {
public:
    virtual ~ELogBinaryFormatProviderConstructor() {}

    /**
     * @brief Constructs a binary format provider.
     * @return ELogBinaryFormatProvider* The resulting binary format provider, or null if failed.
     */
    virtual ELogBinaryFormatProvider* constructBinaryFormatProvider(
        commutil::ByteOrder byteOrder) = 0;

protected:
    /** @brief Constructor. */
    ELogBinaryFormatProviderConstructor(const char* name) {
        registerBinaryFormatProviderConstructor(name, this);
    }
    ELogBinaryFormatProviderConstructor(const ELogBinaryFormatProviderConstructor&) = delete;
    ELogBinaryFormatProviderConstructor(ELogBinaryFormatProviderConstructor&&) = delete;
    ELogBinaryFormatProviderConstructor& operator=(const ELogBinaryFormatProviderConstructor&) =
        delete;
};

// TODO: for sake of being able to externally extend elog, the ELOG_API should be replaced with
// macro parameter, so it can be set to dll export, or to nothing

/** @def Utility macro for declaring binary format provider factory method registration. */
#define ELOG_DECLARE_BINARY_FORMAT_PROVIDER(BinaryFormatProviderType, Name)                      \
    class ELOG_API BinaryFormatProviderType##Constructor final                                   \
        : public elog::ELogBinaryFormatProviderConstructor {                                     \
    public:                                                                                      \
        BinaryFormatProviderType##Constructor()                                                  \
            : elog::ELogBinaryFormatProviderConstructor(#Name) {}                                \
        elog::ELogBinaryFormatProvider* constructBinaryFormatProvider(                           \
            commutil::ByteOrder byteOrder) final {                                               \
            return new (std::nothrow) BinaryFormatProviderType(byteOrder);                       \
        }                                                                                        \
        ~BinaryFormatProviderType##Constructor() final {}                                        \
        BinaryFormatProviderType##Constructor(const BinaryFormatProviderType##Constructor&) =    \
            delete;                                                                              \
        BinaryFormatProviderType##Constructor(BinaryFormatProviderType##Constructor&&) = delete; \
        BinaryFormatProviderType##Constructor& operator=(                                        \
            const BinaryFormatProviderType##Constructor&) = delete;                              \
    };                                                                                           \
    static BinaryFormatProviderType##Constructor sConstructor;

/** @def Utility macro for implementing binary format provider factory method registration. */
#define ELOG_IMPLEMENT_BINARY_FORMAT_PROVIDER(BinaryFormatProviderType) \
    BinaryFormatProviderType::BinaryFormatProviderType##Constructor     \
        BinaryFormatProviderType::sConstructor;

/** @brief Binary format provider for elog internal based messages. */
class ELOG_API ELogInternalBinaryFormatProvider : public ELogBinaryFormatProvider {
public:
    ELogInternalBinaryFormatProvider(commutil::ByteOrder byteOrder)
        : ELogBinaryFormatProvider(byteOrder) {}
    ELogInternalBinaryFormatProvider(ELogInternalBinaryFormatProvider&) = delete;
    ELogInternalBinaryFormatProvider(ELogInternalBinaryFormatProvider&&) = delete;
    ELogInternalBinaryFormatProvider& operator=(const ELogInternalBinaryFormatProvider&) = delete;
    ~ELogInternalBinaryFormatProvider() override {}

    /**
     * @brief Convert a log records into binary data.
     * @param logRecord The record.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    bool logRecordToBuffer(const ELogRecord& logRecord, ELogFormatter* formatter,
                           ELogMsgBuffer& buffer) override;

    /**
     * @brief Converts log status to binary data.
     * @param statusMsg The status message to serialize.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    bool logStatusToBuffer(const ELogStatusMsg& statusMsg, ELogMsgBuffer& buffer) override;

    /**
     * @brief Converts log status from binary data.
     * @param statusMsg The resulting status message.
     * @param buffer The input binary data.
     * @param length The length of the input buffer.
     * @return The operation result.
     */
    bool logStatusFromBuffer(ELogStatusMsg& statusMsg, const char* buffer,
                             uint32_t length) override;

private:
    ELOG_DECLARE_BINARY_FORMAT_PROVIDER(ELogInternalBinaryFormatProvider, elog)
};

/** @brief Binary format provider for protobuf based messages. */
class ELOG_API ELogProtobufBinaryFormatProvider : public ELogBinaryFormatProvider {
public:
    ELogProtobufBinaryFormatProvider(commutil::ByteOrder byteOrder)
        : ELogBinaryFormatProvider(byteOrder) {}
    ELogProtobufBinaryFormatProvider(ELogProtobufBinaryFormatProvider&) = delete;
    ELogProtobufBinaryFormatProvider(ELogProtobufBinaryFormatProvider&&) = delete;
    ELogProtobufBinaryFormatProvider& operator=(const ELogProtobufBinaryFormatProvider&) = delete;
    ~ELogProtobufBinaryFormatProvider() override {}

    /**
     * @brief Convert a log records into binary data.
     * @param logRecord The record.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    bool logRecordToBuffer(const ELogRecord& logRecord, ELogFormatter* formatter,
                           ELogMsgBuffer& buffer) override;

    /**
     * @brief Converts log status to binary data.
     * @param statusMsg The status message to serialize.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    bool logStatusToBuffer(const ELogStatusMsg& statusMsg, ELogMsgBuffer& buffer) override;

    /**
     * @brief Converts log status from binary data.
     * @param statusMsg The resulting status message.
     * @param buffer The input binary data.
     * @param length The length of the input buffer.
     * @return The operation result.
     */
    bool logStatusFromBuffer(ELogStatusMsg& statusMsg, const char* buffer,
                             uint32_t length) override;

private:
    ELOG_DECLARE_BINARY_FORMAT_PROVIDER(ELogProtobufBinaryFormatProvider, protobuf)
};

/** @brief Binary format provider for Thrift based messages. */
class ELOG_API ELogThriftBinaryFormatProvider : public ELogBinaryFormatProvider {
public:
    ELogThriftBinaryFormatProvider(commutil::ByteOrder byteOrder)
        : ELogBinaryFormatProvider(byteOrder) {}
    ELogThriftBinaryFormatProvider(ELogThriftBinaryFormatProvider&) = delete;
    ELogThriftBinaryFormatProvider(ELogThriftBinaryFormatProvider&&) = delete;
    ELogThriftBinaryFormatProvider& operator=(const ELogThriftBinaryFormatProvider&) = delete;
    ~ELogThriftBinaryFormatProvider() override {}

    /**
     * @brief Convert a log records into binary data.
     * @param logRecord The record.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    bool logRecordToBuffer(const ELogRecord& logRecord, ELogFormatter* formatter,
                           ELogMsgBuffer& buffer) override;

    /**
     * @brief Converts log status to binary data.
     * @param statusMsg The status message to serialize.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    bool logStatusToBuffer(const ELogStatusMsg& statusMsg, ELogMsgBuffer& buffer) override;

    /**
     * @brief Converts log status from binary data.
     * @param statusMsg The resulting status message.
     * @param buffer The input binary data.
     * @param length The length of the input buffer.
     * @return The operation result.
     */
    bool logStatusFromBuffer(ELogStatusMsg& statusMsg, const char* buffer,
                             uint32_t length) override;

private:
    ELOG_DECLARE_BINARY_FORMAT_PROVIDER(ELogThriftBinaryFormatProvider, thrift)
};

/** @brief Binary format provider for Avro based messages. */
class ELOG_API ELogAvroBinaryFormatProvider : public ELogBinaryFormatProvider {
public:
    ELogAvroBinaryFormatProvider(commutil::ByteOrder byteOrder)
        : ELogBinaryFormatProvider(byteOrder) {}
    ELogAvroBinaryFormatProvider(ELogAvroBinaryFormatProvider&) = delete;
    ELogAvroBinaryFormatProvider(ELogAvroBinaryFormatProvider&&) = delete;
    ELogAvroBinaryFormatProvider& operator=(const ELogAvroBinaryFormatProvider&) = delete;
    ~ELogAvroBinaryFormatProvider() override {}

    /**
     * @brief Convert a log records into binary data.
     * @param logRecord The record.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    bool logRecordToBuffer(const ELogRecord& logRecord, ELogFormatter* formatter,
                           ELogMsgBuffer& buffer) override;

    /**
     * @brief Converts log status to binary data.
     * @param statusMsg The status message to serialize.
     * @param buffer The resulting binary data.
     * @return The operation result.
     */
    bool logStatusToBuffer(const ELogStatusMsg& statusMsg, ELogMsgBuffer& buffer) override;

    /**
     * @brief Converts log status from binary data.
     * @param statusMsg The resulting status message.
     * @param buffer The input binary data.
     * @param length The length of the input buffer.
     * @return The operation result.
     */
    bool logStatusFromBuffer(ELogStatusMsg& statusMsg, const char* buffer,
                             uint32_t length) override;

private:
    ELOG_DECLARE_BINARY_FORMAT_PROVIDER(ELogAvroBinaryFormatProvider, avro)
};

}  // namespace elog

#endif  // ELOG_ENABLE_MSG

#endif  // __ELOG_BINARY_FORMAT_PROVIDER_H__