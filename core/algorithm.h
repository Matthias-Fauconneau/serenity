#pragma once
#include "core.h"

template<Type A, Type T, Type F, Type... Ss> A reduce(ref<T> values, F fold, A accumulator, ref<Ss>... sources) {
 for(size_t index: range(values.size)) accumulator = fold(accumulator, values[index], sources[index]...);
 return accumulator;
}

generic T min(ref<T> values) { return reduce(values, [](T accumulator, T value) { return min(accumulator, value); }, values[0]); }
generic T max(ref<T> values) { return reduce(values, [](T accumulator, T value) { return max(accumulator, value); }, values[0]); }

template<Type A, Type T> A sum(ref<T> values, A initialValue) {
 return reduce(values, [](A accumulator, T value) { return accumulator + value; }, initialValue);
}
generic decltype(T()+T()) sum(ref<T> values) { return sum(values, decltype(T()+T())()); }

generic uint argmax(const ref<T>& a) { uint max=0; for(uint i: range(a.size)) if(a[i] > a[max]) max=i; return max; }
