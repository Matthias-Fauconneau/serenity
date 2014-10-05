#pragma once
#include <pthread.h> //pthread
#include "function.h"

/// Logical cores count
static constexpr uint threadCount = 4;

struct thread { uint64 id; uint64* counter; uint64 stop; pthread_t pthread; function<void(uint, uint)>* delegate; uint64 pad[3]; };
inline void* start_routine(thread* t) {
    for(;;) {
        uint64 i=__sync_fetch_and_add(t->counter,1);
        if(i>=t->stop) break;
        (*t->delegate)(t->id, i);
    }
    return 0;
}

/// Runs a loop in parallel
template<Type F> void parallel(uint64 start, uint64 stop, F f) {
#if DEBUG || PROFILE
    for(uint i : range(start, stop)) f(0, i);
#else
    function<void(uint, uint)> delegate = f;
    thread threads[threadCount];
    for(uint i: range(threadCount)) {
        threads[i].id = i;
        threads[i].counter = &start;
        threads[i].stop = stop;
        threads[i].delegate = &delegate;
        pthread_create(&threads[i].pthread,0,(void*(*)(void*))start_routine,&threads[i]);
    }
    for(const thread& t: threads) { uint64 status=-1; pthread_join(t.pthread,(void**)&status); assert(status==0); }
#endif
}
template<Type F> void parallel(uint stop, F f) { parallel(0,stop,f); }

/// Runs a loop in parallel chunks with element-wise functor
template<Type F/*, Type... Args*/> void chunk_parallel(uint64 totalSize, F f/*, Args... args*/) {
    constexpr uint64 chunkCount = threadCount;
    assert(totalSize%chunkCount<chunkCount); //Last chunk might be up to chunkCount smaller
    const uint64 chunkSize = (totalSize+chunkCount-1)/chunkCount;
    parallel(chunkCount, [&](uint id, uint64 chunkIndex) {
        uint64 chunkStart = chunkIndex*chunkSize;
        for(uint64 index: range(chunkStart, min(totalSize, chunkStart+chunkSize))) f(id, index/*, args...*/);
    });
}

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> void parallel_chunk(uint64 totalSize, F f) {
    constexpr uint64 chunkCount = threadCount;
    assert(totalSize%chunkCount<chunkCount); //Last chunk might be up to chunkCount smaller
    const uint64 chunkSize = totalSize/chunkCount;
    parallel(chunkCount, [&](uint id, uint64 chunkIndex) { f(id, chunkIndex*chunkSize, min(totalSize-chunkIndex*chunkSize, chunkSize)); });
}

/// Stores the application of a function to every index up to a size in a mref
template<Type T, Type Function, Type... Args>
void parallel_apply(mref<T> target, size_t size, Function function, Args... args) {
    chunk_parallel(size, [&](uint, uint index) { new (&target[index]) T(function(index, args...)); });
}

/// Stores the application of a function to every elements of two refs in a mref
template<Type T, Type S0, Type S1, Type Function/*, Type... Args*/>
void parallel_apply(mref<T> target, const ref<S0>& source0, const ref<S1>& source1, Function function/*, Args... args*/) {
    chunk_parallel(target.size, [&](uint, uint index) { new (&target[index]) T(function(source0[index], source1[index]/*, args...*/)); });
}

/// Stores the application of a function to every elements of a ref in a mref
template<Type T, Type S, Type Function/*, Type... Args*/>
void parallel_apply(mref<T> target, const ref<S>& source, Function function/*, Args... args*/) {
    chunk_parallel(target.size, [&](uint, uint index) { new (&target[index]) T(function(source[index]/*, args...*/)); });
}

// \file arithmetic.cc Parallel arithmetic operations

/// Minimum number of values to trigger parallel arithmetic operations
static constexpr size_t parallelMinimum = 1<<15;

inline float parallel_sum(ref<float> values) {
    if(values.size < parallelMinimum) return ::sum(values);
    float sums[threadCount];
    parallel_chunk(values.size, [&](uint id, uint start, uint size) {
        float sum = 0;
        for(uint index: range(start, start+size)) sum += values[index];
        sums[id] = sum;
    });
    return sum(sums);
}

inline float minimum(ref<float> values) {
    if(values.size < parallelMinimum) return ::min(values);
    float maximums[threadCount];
    parallel_chunk(values.size, [&](uint id, uint start, uint size) {
        float min = values[0];
        for(uint index: range(start, start+size)) { float v=values[index]; if(v<min) min = v; }
        maximums[id] = min;
    });
    return min(maximums);
}

inline float maximum(ref<float> values) {
    if(values.size < parallelMinimum) return ::max(values);
    float maximums[threadCount];
    parallel_chunk(values.size, [&](uint id, uint start, uint size) {
        float max= -values[0];
        for(uint index: range(start, start+size)) { float v=values[index]; if(v>max) max = v; }
        maximums[id] = max;
    });
    return max(maximums);
}

inline void operator+=(mref<float> values, float offset) {
    if(values.size < parallelMinimum) apply(values, values, [&](float v) {  return v + offset; });
    else parallel_apply(values, values, [&](float v) {  return v + offset; });
}

inline void operator*=(mref<float> values, float factor) {
    if(values.size < parallelMinimum) apply(values, values, [&](float v) {  return factor*v; });
    else parallel_apply(values, values, [&](float v) {  return factor*v; });
}

inline void normalize(mref<float> values) {
    float max = maximum(values);
    assert_(max);
    float scaleFactor = 1./max;
    values *= scaleFactor;
}

inline void add(mref<float> Y, ref<float> A, ref<float> B) {
    if(Y.size < parallelMinimum) apply(Y, A, B, [&](float a, float b) {  return a+b; });
    else parallel_apply(Y, A, B, [&](float a, float b) {  return a+b; });
}

inline void subtract(mref<float> Y, ref<float> A, ref<float> B) {
    if(Y.size < parallelMinimum) apply(Y, A, B, [&](float a, float b) {  return a-b; });
    else parallel_apply(Y, A, B, [&](float a, float b) {  return a-b; });
}
