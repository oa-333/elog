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

/**
 * @brief Utility helper class for assisting in recycling objects that are not derived from
 * @ref ELogManagedObject.
 */
template <typename T>
class ELogManagedObjectWrapper : public ELogManagedObject {
public:
    ELogManagedObjectWrapper(T* object) : m_object(object) {}
    ~ELogManagedObjectWrapper() override { destroyObject(m_object); }

protected:
    /** @brief Disallow copy constructor. */
    ELogManagedObjectWrapper(const ELogManagedObjectWrapper&) = delete;

    /** @brief Disallow move constructor.*/
    ELogManagedObjectWrapper(ELogManagedObjectWrapper&&) = delete;

    /** @brief Disallow assignment operator.*/
    ELogManagedObjectWrapper& operator=(const ELogManagedObjectWrapper&) = delete;

    /** @brief Destroys object. By default deletes it. */
    virtual void destroyObject(T* object) { delete object; }

private:
    T* m_object;
};

/**
 * @def Helper macro for implementing recycling of object not derived from @ref ELogManagedObject.
 * Should be used as follows:
 *
 *      ELOG_IMPLEMENT_RECYCLE(ClassName) {
 *          // the parameter name to recycle is object, and it has type ClassName*
 *          delete object;
 *      }
 */
#define ELOG_IMPLEMENT_RECYCLE(ClassName) \
    template <>                           \
    void ELogManagedObjectWrapper<ClassName>::destroyObject(ClassName* object)

/**
 * @def Helper macro: retires an object not derived from @ref ELogManagedObject, for asynchronous
 * reclamation.
 */
#define ELOG_RETIRE(gc, ClassName, object, epoch)                           \
    {                                                                       \
        ELogManagedObjectWrapper<ClassName>* managedObject =                \
            new (std::nothrow) ELogManagedObjectWrapper<ClassName>(object); \
        if (managedObject != nullptr) {                                     \
            gc->retire(managedObject, epoch);                               \
        }                                                                   \
    }

}  // namespace elog

#endif  // __ELOG_MANAGED_OBJECT_H__