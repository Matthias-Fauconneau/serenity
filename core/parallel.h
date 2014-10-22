#pragma once
#include <pthread.h> //pthread
#include "function.h"
#include "math.h"
#include "map.h"

// -> \file math.h
inline void operator*=(mref<float> values, float factor) { values.apply([&](float v) { return factor*v; }, values); }

// -> \file algorithm.h

template<Type A, Type F> A reduce(range range, F fold, A accumulator) {
    for(size_t index: range) accumulator = fold(accumulator, index);
    return accumulator;
}

template<Type A, Type T, Type F, Type... Ss> T reduce(ref<T> values, F fold, A accumulator, ref<Ss>... sources) {
    for(size_t index: range(values.size)) accumulator = fold(accumulator, values[index], sources[index]...);
    return accumulator;
}
template<Type T, Type A, Type F, size_t N> T reduce(const T (&values)[N], F fold, A initialValue) {
    return reduce(ref<T>(values), fold, initialValue);
}

generic T sum(ref<T> values) { return reduce(values, [](T accumulator, T value) { return accumulator + value; }, T()); }
template<Type T, size_t N> T sum(const T (&values)[N]) { return sum(ref<T>(values)); }

generic T min(ref<T> values) { return reduce(values, [](T accumulator, T value) { return min(accumulator, value); }, values[0]); }
template<Type T, size_t N> T min(const T (&a)[N]) { return min(ref<T>(a)); }

generic T max(ref<T> values) { return reduce(values, [](T accumulator, T value) { return max(accumulator, value); }, values[0]); }
template<Type T, size_t N> T max(const T (&a)[N]) { return max(ref<T>(a)); }

// \file parallel.h

static constexpr uint threadCount = 4;

struct thread { uint64 id; uint64* counter; uint64 stop; pthread_t pthread; function<void(uint, uint)>* delegate; uint64 pad[3]; };
inline void* start_routine(thread* t) {
    for(;;) {
        uint64 index = __sync_fetch_and_add(t->counter,1);
        if(index >= t->stop) break;
        (*t->delegate)(t->id, index);
    }
    return 0;
}

/// Runs a loop in parallel
template<Type F> void parallel_for(uint64 start, uint64 stop, F f) {
#if DEBUG || PROFILE
    for(uint i : range(start, stop)) f(0, i);
#else
    function<void(uint, uint)> delegate = f;
    thread threads[threadCount];
    for(uint index: range(threadCount)) {
        threads[index].id = index;
        threads[index].counter = &start;
        threads[index].stop = stop;
        threads[index].delegate = &delegate;
        pthread_create(&threads[index].pthread, 0, (void*(*)(void*))start_routine, &threads[index]);
    }
    for(const thread& thread: threads) { uint64 status=-1; pthread_join(thread.pthread, (void**)&status); assert(status==0); }
#endif
}
template<Type F> void parallel_for(uint stop, F f) { parallel_for(0,stop,f); }

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> void parallel_chunk(uint64 totalSize, F f) {
    constexpr uint64 chunkCount = threadCount;
    assert(totalSize%chunkCount<chunkCount); //Last chunk might be up to chunkCount smaller
    const uint64 chunkSize = (totalSize+chunkCount-1)/chunkCount;
    assert_(totalSize-(chunkCount-1)*chunkSize > 0);
    parallel_for(chunkCount, [&](uint id, uint64 chunkIndex) { f(id, chunkIndex*chunkSize, min(chunkSize, totalSize-chunkIndex*chunkSize)); });
}

/// Runs a loop in parallel chunks with element-wise functor
template<Type F> void chunk_parallel(uint64 totalSize, F f) {
    parallel_chunk(totalSize, [&](uint id, uint64 start, uint64 size) { for(uint64 index: range(start, start+size)) f(id, index); });
}

namespace parallel {

/// Minimum number of values to trigger parallel operations
static constexpr size_t minimumSize = 1<<15;

/// Stores the application of a function to every index in a mref
template<Type T, Type Function>
void apply(mref<T> target, Function function) {
    chunk_parallel(target.size, [&](uint, size_t index) { new (&target[index]) T(function(index)); });
}

/// Stores the application of a function to every elements of a ref in a mref
template<Type T, Type Function, Type... S>
void apply(mref<T> target, Function function, ref<S>... sources) {
    for(auto size: {sources.size...}) assert_(target.size == size, target.size, sources.size...);
    if(target.size < minimumSize) return target.apply(function, sources...);
    else chunk_parallel(target.size, [&](uint, size_t index) { new (&target[index]) T(function(sources[index]...)); });
}

template<Type A, Type T, Type F, Type... Ss> T reduce(size_t size, F fold, A initial_value) {
	assert_(size);
	if(size< minimumSize) return ::reduce(size, fold, initial_value);
	else {
		A accumulators[threadCount];
		mref<A>(accumulators).clear(initial_value); // Some threads may not iterate
		parallel_chunk(size, [&](uint id, size_t start, size_t size) { accumulators[id] = ::reduce(range(start, size), fold, initial_value); });
		return ::reduce(accumulators, fold, initial_value);
	}
}

template<Type A, Type T, Type F, Type... Ss> T reduce(ref<T> values, F fold, A initial_value) {
    assert_(values);
    if(values.size < minimumSize) return ::reduce(values, fold, initial_value);
    else {
        A accumulators[threadCount];
        mref<A>(accumulators).clear(initial_value); // Some threads may not iterate
        parallel_chunk(values.size, [&](uint id, size_t start, size_t size) { accumulators[id] = ::reduce(values.slice(start, size), fold, initial_value); });
		return ::reduce(accumulators, fold, initial_value);
    }
}
template<Type T, Type F> T reduce(ref<T> values, F fold) { return reduce(values, fold, values[0]); }

// \file arithmetic.cc Parallel arithmetic operations

// apply

inline void sub(mref<float> Y, ref<float> A, ref<float> B) { apply(Y, [&](float a, float b) {  return a-b; }, A, B); }
inline void mul(mref<float> Y, ref<float> A, ref<float> B) { apply(Y, [&](float a, float b) {  return a*b; }, A, B); }
inline void div(mref<float> Y, ref<float> A, ref<float> B) { apply(Y, [&](float a, float b) {  return a/b; }, A, B); }

// reduce

generic T min(ref<T> values) { return reduce(values, [](T accumulator, T value) { return ::min(accumulator, value); }); }
generic T max(ref<T> values) { return reduce(values, [](T accumulator, T value) { return ::max(accumulator, value); }); }

inline real sum(ref<float> values) { return reduce(values, [](real accumulator, float value) { return accumulator + value; }, 0.); }

inline real mean(ref<float> values) { return sum(values)/values.size; }

// apply reduce

// \note Cannot be a generic reduction as the final fold is single source
template<Type T, Type F, Type A, Type... Ss> T sum(ref<T> values, F apply, A initial_value, ref<Ss>... sources) {
    assert_(values);
    if(values.size < minimumSize) return ::reduce(values, [&](T a, T v, Ss... s) { return a+apply(v, s...); }, initial_value, sources...);
    else {
		A accumulators[threadCount];
		mref<A>(accumulators).clear(initial_value); // Some threads may not iterate
        parallel_chunk(values.size, [&](uint id, size_t start, size_t size) {
            accumulators[id] = ::reduce(values.slice(start, size),
										[&](A a, T v, Ss... s) { return a+apply(v, s...); }, initial_value, sources.slice(start, size)...); });
        return ::sum(accumulators);
    }
}

inline real energy(ref<float> values, float offset=0) { return sum(values, [offset](float value) { return sq(value-offset); }, 0.); }

inline double SSE(ref<float> A, ref<float> B) {
	assert_(A.size == B.size);
	return sum(A, [](float a, float b) { return sq(a-b); }, 0., B);
}

// multiple accumulator reduce

/// Double accumulator reduction
template<Type A, Type T, Type F0, Type F1> void reduce(ref<T> values, F0 fold0, F1 fold1, A& accumulator0, A& accumulator1) {
    T accumulators[2][threadCount];
    mref<T>(accumulators[0]).clear(accumulator0), mref<T>(accumulators[1]).clear(accumulator1); // Some threads may not iterate
    parallel_chunk(values.size, [&](uint id, size_t start, size_t size) {
        A a0 = accumulator0, a1 = accumulator1;
        for(T v: values.slice(start, size)) a0=fold0(a0, v), a1=fold1(a1, v);
        accumulators[0][id] = a0;
        accumulators[1][id] = a1;
    });
    accumulator0 = ::reduce(accumulators[0], fold0, accumulator0), accumulator1 = ::reduce(accumulators[1], fold1, accumulator1);
}

generic void minmax(ref<T> values, T& minimum, T& maximum) {
    return reduce(values, [](T a, T v) { return ::min(a, v); }, [](T a, T v) { return ::max(a, v); }, minimum, maximum);
}

}
