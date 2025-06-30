#ifndef __ELOG_ATOMIC_H__
#define __ELOG_ATOMIC_H__

#include <atomic>

namespace elog {

/** @brief A copy-able assignable atomic value template class. */
template <typename T>
struct ELogAtomic {
    std::atomic<T> m_atomicValue;
    ELogAtomic() : m_atomicValue() {}
    ELogAtomic(const T& value) : m_atomicValue(value) {}
    ELogAtomic(const std::atomic<T>& atomicValue)
        : m_atomicValue(atomicValue.load(std::memory_order_relaxed)) {}
    ELogAtomic(const ELogAtomic& other)
        : m_atomicValue(other.m_atomicValue.load(std::memory_order_relaxed)) {}
    ELogAtomic& operator=(const ELogAtomic& other) {
        m_atomicValue.store(other.m_atomicValue.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
        return *this;
    }
};

}  // namespace elog

#endif  // __ELOG_ATOMIC_H__