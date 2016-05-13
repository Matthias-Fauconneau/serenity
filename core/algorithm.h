#pragma once
#include "core.h"

template<Type A, Type F> A reduce(range range, F fold, A accumulator) {
    for(size_t index: range) accumulator = fold(accumulator, index);
    return accumulator;
}

template<Type A, Type T, Type F, Type... Ss> T reduce(ref<T> values, F fold, A accumulator, ref<Ss>... sources) {
    for(size_t index: range(values.size)) accumulator = fold(accumulator, values[index], sources[index]...);
    return accumulator;
}
template<Type A, Type T, Type F, size_t N> T reduce(const T (&values)[N], F fold, A initialValue) {
    return reduce(ref<T>(values), fold, initialValue);
}

template<Type A, Type T> T sum(ref<T> values, A initialValue) {
    return reduce(values, [](A accumulator, T value) { return accumulator + value; }, initialValue);
}
template<Type T> T sum(ref<T> values) { return sum(values, T()); }
template<Type T, size_t N> T sum(const T (&values)[N]) { return sum(ref<T>(values)); }

template<Type T> T min(ref<T> values) { return reduce(values, [](T accumulator, T value) { return min(accumulator, value); }, values[0]); }
template<Type T, size_t N> T min(const T (&a)[N]) { return min(ref<T>(a)); }

template<Type T> T max(ref<T> values) { return reduce(values, [](T accumulator, T value) { return max(accumulator, value); }, values[0]); }
template<Type T, size_t N> T max(const T (&a)[N]) { return max(ref<T>(a)); }

generic uint argmax(const ref<T>& a) { uint max=0; for(uint i: range(a.size)) if(a[i] > a[max]) max=i; return max; }

inline float mean(const ref<float> v) { return sum(v, 0.)/v.size; }
