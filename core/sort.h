#pragma once
#include "memory.h"

generic inline uint partition(const mref<T>& at, size_t left, size_t right, size_t pivotIndex) {
    swap(at[pivotIndex], at[right]);
    const T& pivot = at[right];
    uint storeIndex = left;
    for(uint i: range(left,right)) {
        if(at[i] < pivot) {
            swap(at[i], at[storeIndex]);
            storeIndex++;
        }
    }
    swap(at[storeIndex], at[right]);
    return storeIndex;
}

generic inline void quicksort(const mref<T>& at, size_t left, size_t right) {
    if(left < right) { // If the list has 2 or more items
        size_t pivotIndex = partition(at, left, right, (left + right)/2);
        if(pivotIndex) quicksort(at, left, pivotIndex-1);
        quicksort(at, pivotIndex+1, right);
    }
}
/// Quicksorts the array in-place
generic inline const mref<T>& sort(const mref<T>& at) { if(at.size) quicksort(at, 0, at.size-1); return at; }

// Split keys and values

template<Type K, Type... V> inline uint partition(const mref<K>& keys, size_t left, size_t right, size_t pivotIndex, const V&... values) {
    swap(keys[pivotIndex], keys[right]);
    (void)(int[sizeof...(V)]){ (swap(values[pivotIndex], values[right]), 0)... };
    const K& pivot = keys[right];
    uint storeIndex = left;
    for(uint i: range(left,right)) {
        if(keys[i] < pivot) {
            swap(keys[i], keys[storeIndex]);
            (void)(int[sizeof...(V)]){(swap(values[i], values[storeIndex]), 0)...};
            storeIndex++;
        }
    }
    swap(keys[storeIndex], keys[right]);
    (void)(int[sizeof...(V)]){(swap(values[storeIndex], values[right]),0)...};
    return storeIndex;
}

template<Type K, Type... V> inline void quicksort(const mref<K>& keys, size_t left, size_t right, const V&... values) {
    if(left < right) { // If the list has 2 or more items
        size_t pivotIndex = partition(keys, left, right, (left + right)/2, values...);
        if(pivotIndex) quicksort(keys, left, pivotIndex-1, values...);
        quicksort(keys, pivotIndex+1, right, values...);
    }
}
/// Quicksorts the arrays in-place
template<Type K, Type... V> inline void sort(const mref<K>& keys, const mref<V>&... values) { if(keys.size) quicksort(keys, 0, keys.size-1, values...); }
