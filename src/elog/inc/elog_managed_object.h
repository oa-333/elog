#ifndef __ELOG_MANAGED_OBJECT_H__
#define __ELOG_MANAGED_OBJECT_H__

#include <atomic>
#include <cstdint>

#include "elog_def.h"

namespace elog {

/** @class Parent class for all GC-managed objects. */
class ELOG_API ELogManagedObject {
public:
    /** @brief Virtual destructor. */
    virtual ~ELogManagedObject() {}

    /** @brief Sets the retire epoch of this managed object. */
    inline void setRetireEpoch(uint64_t epoch) { m_retireEpoch = epoch; }

    /** @brief Retrieves the retire epoch. */
    inline uint64_t getRetireEpoch() { return m_retireEpoch; }

    /** @brief Sets the next managed object in a linked list. */
    inline void setNext(ELogManagedObject* next) { m_next.store(next, std::memory_order_relaxed); }

    /** @brief Retrieves the next managed object in a linked list. */
    inline ELogManagedObject* getNext() { return m_next.load(std::memory_order_relaxed); }

    /**
     * @brief Detach list suffix.
     * @param next The next list item, previously obtained by a call to @ref getNext().
     * @return true If the CAS operation for detaching the suffix succeeded, otherwise false.
     */
    inline bool detachSuffix(ELogManagedObject* next) {
        return m_next.compare_exchange_strong(next, nullptr, std::memory_order_seq_cst);
    }

protected:
    /** @brief Disallow copy constructor. */
    ELogManagedObject(const ELogManagedObject&) = delete;

    /** @brief Disallow move constructor.*/
    ELogManagedObject(ELogManagedObject&&) = delete;

    /** @brief Disallow assignment operator.*/
    ELogManagedObject& operator=(const ELogManagedObject&) = delete;

    ELogManagedObject(uint64_t retireEpoch = 0, ELogManagedObject* next = nullptr)
        : m_retireEpoch(retireEpoch), m_next(next) {}

private:
    uint64_t m_retireEpoch;
    std::atomic<ELogManagedObject*> m_next;
};

}  // namespace elog

#endif  // __ELOG_MANAGED_OBJECT_H__