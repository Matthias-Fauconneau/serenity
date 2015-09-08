#pragma once
#include <pthread.h> //pthread
#include "function.h"
#include "math.h"
#include "map.h"
#include "thread.h"
#include "data.h"
#include "file.h"

// -> \file math.h
inline void operator*=(mref<float> values, float factor) { values.apply([factor](float v) { return factor*v; }, values); }

// \file parallel.h
#include "vector.h"

/*void atomic_add(v4sf& a, v4sf b) {
 v4sf expected, desired;
 do {
  expected = a;
  desired = expected+b;
 } while(!__sync_bool_compare_and_swap((__int128*)&a, *(__int128*)&expected,
                *(__int128*)&desired));
}*/

inline void atomic_sub(v4sf& a, v4sf b) {
 v4sf expected, desired;
 do {
  expected = a;
  desired = expected-b;
 } while(!__sync_bool_compare_and_swap((__int128*)&a, *(__int128*)&expected,
                *(__int128*)&desired));
}

inline void atomic_add(float& a, float b) {
 float expected = a;
 float desired;
 do {
  desired = expected+b;
 } while(!__atomic_compare_exchange_n((int*)&a, (int*)&expected,
                                      *(int*)&desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
}

static const size_t maxThreadCount = 1; //32;
static size_t coreCount() {
 TextData s(File("/proc/cpuinfo").readUpToLoop(1<<16));
 assert_(s.data.size<s.buffer.capacity);
 size_t coreCount = 0;
 while(s) { if(s.match("processor")) coreCount++; s.line(); }
 //assert_(coreCount <= maxThreadCount, coreCount, maxThreadCount);
 if(environmentVariable("THREADS"_))
  coreCount = min(coreCount, (size_t)parseInteger(environmentVariable("THREADS"_)));
 return min(coreCount, maxThreadCount);
}
static const int threadCount = coreCount();

struct thread {
 pthread_t pthread = 0;
 int64 id; int64* counter; int64 stop;
 function<void(uint, uint)>* delegate;
};
static Semaphore jobs;
static Semaphore results;
static thread threads[::maxThreadCount];
inline void* start_routine(thread* t) {
 for(;;) {
  jobs.acquire(1);
  for(;;) {
   int64 index = __sync_fetch_and_add(t->counter,1);
   if(index >= t->stop) break;
   (*t->delegate)(t->id, index);
  }
  results.release(1);
 }
 return 0;
}

/// Runs a loop in parallel
template<Type F> void parallel_for(int64 start, int64 stop, F f, const int unused threadCount = ::threadCount) {
#if DEBUG || PROFILE
 for(int64 i : range(start, stop)) f(0, i);
#else
 if(threadCount == 1) {
  for(int64 i : range(start, stop)) f(0, i);
 } else {
  function<void(uint, uint)> delegate = f;
  assert_(threadCount == ::threadCount);
  for(uint index: range(threadCount)) {
   threads[index].id = index;
   threads[index].counter = &start;
   threads[index].stop = stop;
   threads[index].delegate = &delegate;
   if(!threads[index].pthread)
    pthread_create(&threads[index].pthread, 0, (void*(*)(void*))start_routine, &threads[index]);
  }
  jobs.release(threadCount);
  results.acquire(threadCount);
 }
#endif
}
template<Type F> void parallel_for(uint stop, F f, const uint unused threadCount = ::threadCount) {
 parallel_for(0, stop, f, threadCount);
}

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> void parallel_chunk(int64 totalSize, F f, const uint threadCount = ::threadCount) {
 if(totalSize <= threadCount*threadCount || threadCount==1) {
  f(0, 0, totalSize);
  return;
 }
 const int64 chunkSize = (totalSize+threadCount-1)/threadCount;
 const int64 chunkCount = (totalSize+chunkSize-1)/chunkSize; // Last chunk might be smaller
 assert_((chunkCount-1)*chunkSize < totalSize && totalSize <= chunkCount*chunkSize, (chunkCount-1)*chunkSize, totalSize, chunkCount*chunkSize);
 assert_(chunkCount == threadCount, chunkCount, threadCount, chunkSize);
 parallel_for(0, chunkCount, [&](uint id, int64 chunkIndex) {
   f(id, chunkIndex*chunkSize, min(chunkSize, totalSize-chunkIndex*chunkSize));
 }, threadCount);
 /*for(int64 chunkIndex : range(0, chunkCount))
  f(0, chunkIndex*chunkSize, min(chunkSize, totalSize-chunkIndex*chunkSize));*/
}
/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> void parallel_chunk(int64 start, int64 stop, F f, const uint threadCount = ::threadCount) { parallel_chunk(stop-start, [&](uint id, int64 I0, int64 DI) { f(id, start+I0, DI); }, threadCount); }

/// Runs a loop in parallel chunks with element-wise functor
template<Type F> void chunk_parallel(int64 totalSize, F f) {
 if(totalSize <= (threadCount-1)*threadCount) for(int64 index: range(totalSize)) f(0, index);
 else parallel_chunk(totalSize, [&](uint id, int64 start, int64 size) { for(int64 index: range(start, start+size)) f(id, index); });
}

namespace parallel {

/// Minimum number of values to trigger parallel operations
static constexpr size_t minimumSize = 1<<15;

/// Stores the application of a function to every index in a mref
template<Type T, Type Function>
void apply(mref<T> target, Function function) {
 chunk_parallel(target.size, [&](uint, size_t index) { new (&target[index]) T(function(index)); });
}

/*/// Stores the application of a function to every elements of a ref in a mref
template<Type T, Type Function, Type... S>
void apply(mref<T> target, Function function, ref<S>... sources) {
 for(auto size: {sources.size...}) assert_(target.size == size, target.size, sources.size...);
 if(target.size < minimumSize) return target.apply(function, sources...);
 else chunk_parallel(target.size, [&](uint, size_t index) { new (&target[index]) T(function(sources[index]...)); });
}*/

template<Type A, Type T, Type F, Type... Ss> T reduce(size_t size, F fold, A initialValue) {
 assert_(size);
 if(size< minimumSize) return ::reduce(size, fold, initialValue);
 else {
  A accumulators[threadCount];
  mref<A>(accumulators).clear(initialValue); // Some threads may not iterate
  parallel_chunk(size, [&](uint id, size_t start, size_t size) {
   accumulators[id] = fold(accumulators[id], ::reduce(range(start, size), fold, initialValue));
  });
  return ::reduce(accumulators, fold, initialValue);
 }
}

/*template<Type A, Type T, Type F, Type... Ss> T reduce(ref<T> values, F fold, A initialValue) {
 assert_(values);
 if(values.size < minimumSize) return ::reduce(values, fold, initialValue);
 else {
  A accumulators[threadCount];
  mref<A>(accumulators).clear(initialValue); // Some threads may not iterate
  parallel_chunk(values.size, [&](uint id, size_t start, size_t size) {
   accumulators[id] = fold(accumulators[id], ::reduce(values.slice(start, size), fold, initialValue));
  });
  return ::reduce(accumulators, fold, initialValue);
 }
}
template<Type T, Type F> T reduce(ref<T> values, F fold) { return reduce(values, fold, values[0]); }*/

// \file arithmetic.cc Parallel arithmetic operations

// apply

/*inline void plus(mref<float> Y, ref<float> X) { apply(Y, [](float x) { return max(0.f, x); }, X); }
inline void sub(mref<float> Y, ref<float> A, ref<float> B) { apply(Y, [](float a, float b) { return a-b; }, A, B); }
inline void mul(mref<float> Y, ref<float> A, ref<float> B) { apply(Y, [](float a, float b) { return a*b; }, A, B); }
inline void muladd(mref<float> Y, ref<float> A, ref<float> B) { apply(Y, [](float y, float a, float b) { return y + a*b; }, Y, A, B); }
inline void div(mref<float> Y, ref<float> A, ref<float> B) { apply(Y, [](float a, float b) { return a/b; }, A, B); }*/

// reduce

generic T min(ref<T> values) { return reduce(values, [](T accumulator, T value) { return ::min(accumulator, value); }); }
generic T max(ref<T> values) { return reduce(values, [](T accumulator, T value) { return ::max(accumulator, value); }); }

inline double sum(ref<float> values) { return reduce(values, [](double accumulator, float value) { return accumulator + value; }, 0.); }

inline double mean(ref<float> values) { return sum(values)/values.size; }

// apply reduce

// \note Cannot be a generic reduction as the final fold is single source
/*template<Type A, Type T, Type F, Type... Ss> T sum(ref<T> values, F apply, A initialValue, ref<Ss>... sources) {
 assert_(values);
 if(values.size < minimumSize) return ::reduce(values, [&](A a, T v, Ss... s) { return a+apply(v, s...); }, initialValue, sources...);
 else {
  A accumulators[threadCount];
  mref<A>(accumulators).clear(initialValue); // Some threads may not iterate
  parallel_chunk(values.size, [&](uint id, size_t start, size_t size) {
   accumulators[id] += ::reduce(values.slice(start, size),
                                [&](A a, T v, Ss... s) { return a+apply(v, s...); }, initialValue, sources.slice(start, size)...); });
  return ::sum<A>(accumulators);
 }
}*/

//inline double energy(ref<float> values, float offset=0) { return sum(values, [offset](float value) { return sq(value-offset); }, 0.); }

/*inline double SSE(ref<float> A, ref<float> B) {
 assert_(A.size == B.size);
 return sum(A, [](float a, float b) { return sq(a-b); }, 0., B);
}*/

// multiple accumulator reduce

/*/// Double accumulator reduction
template<Type A, Type T, Type F0, Type F1> void reduce(ref<T> values, F0 fold0, F1 fold1, A& accumulator0, A& accumulator1) {
 A accumulators[2][threadCount];
 mref<T>(accumulators[0]).clear(accumulator0), mref<T>(accumulators[1]).clear(accumulator1); // Some threads may not iterate
 parallel_chunk(values.size, [&](uint id, size_t start, size_t size) {
  A a0 = accumulator0, a1 = accumulator1;
  for(T v: values.slice(start, size)) a0=fold0(a0, v), a1=fold1(a1, v);
  accumulators[0][id] = fold0(accumulators[0][id], a0);
  accumulators[1][id] = fold1(accumulators[1][id], a1);
 });
 accumulator0 = ::reduce(accumulators[0], fold0, accumulator0), accumulator1 = ::reduce(accumulators[1], fold1, accumulator1);
}*/

generic void minmax(ref<T> values, T& minimum, T& maximum) {
 return reduce(values, [](T a, T v) { return ::min(a, v); }, [](T a, T v) { return ::max(a, v); }, minimum, maximum);
}

}
