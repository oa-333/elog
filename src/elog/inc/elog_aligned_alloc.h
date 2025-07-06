#ifndef __ELOG_ALIGNED_ALLOC_H__
#define __ELOG_ALIGNED_ALLOC_H__

#include <cstdint>
#include <cstdlib>

#include "elog_def.h"

namespace elog {

// due to problems in aligned allocation, we do it ourselves

inline void* elogAlignedAlloc(size_t size, size_t align) {
#ifdef ELOG_WINDOWS
    return _aligned_malloc(size, align);
#else
#if _POSIX_C_SOURCE >= 200112L || _XOPEN_SOURCE >= 600
    void* buf = nullptr;
    int res = posix_memalign(&buf, align, size);
    if (res != 0) {
        return nullptr;
    }
    return buf;
#elif defined(_ISOC11_SOURCE)
    return std::aligned_alloc(align, size);
#else
    return null;
#endif
#endif
}

inline void elogAlignedFree(void* buf) {
#ifdef ELOG_WINDOWS
    _aligned_free(buf);
#else
    free(buf);
#endif
}

template <typename T, typename... Args>
inline T* elogAlignedAllocObject(size_t align, const Args&... args) {
    void* buf = elogAlignedAlloc(sizeof(T), align);
    if (buf == nullptr) {
        return nullptr;
    }
    // call normal placement new
    return new (buf) T(args...);
}

template <typename T>
inline void elogAlignedFreeObject(T* object) {
    // call destructor
    object->~T();

    // free aligned
    elogAlignedFree(object);
}

template <typename T, typename... Args>
inline T* elogAlignedAllocObjectArray(size_t align, size_t count, const Args&... args) {
    size_t totalSize = sizeof(T) * count;
    if ((totalSize % align) != 0) {
        // must be a multiple of align
        return nullptr;
    }
    void* buf = elogAlignedAlloc(sizeof(T) * count, align);
    if (buf == nullptr) {
        return nullptr;
    }
    // call normal placement new for each array member
    T* array = (T*)buf;
    for (size_t i = 0; i < count; ++i) {
        new (&array[i]) T(args...);
    }
    return array;
}

template <typename T>
inline void elogAlignedFreeObjectArray(T* array, size_t count) {
    // call destructor for each array member
    for (size_t i = 0; i < count; ++i) {
        array[i].~T();
    }

    // free aligned
    elogAlignedFree(array);
}

}  // namespace elog

#endif  // __ELOG_ALIGNED_ALLOC_H__
