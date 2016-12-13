#pragma once
#include <cstring>
#include <memory>

template<typename T>
inline std::unique_ptr<T[]> zeroAlloc(size_t size)
{
    std::unique_ptr<T[]> result(new T[size]);
    std::memset(result.get(), 0, size*sizeof(T));
    return std::move(result);
}
