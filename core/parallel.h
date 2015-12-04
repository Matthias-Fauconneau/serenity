#pragma once
/// \file parallel.h
#include <pthread.h> //pthread
#include "data.h"
#include "file.h"
#include "function.h"
#include "thread.h"
#include "time.h"

struct thread {
 pthread_t pthread = 0;
 int64 id; int64* counter; int64 stop;
 function<void(uint, uint)>* delegate;
 uint64 time = 0;
};

static constexpr size_t maxThreadCount = 12; // 4..32

extern thread threads[::maxThreadCount];

extern Semaphore jobs;
extern Semaphore results;

inline void* start_routine(thread* t) {
 for(;;) {
  jobs.acquire(1);
  tsc time; time.start();
  for(;;) {
   int64 index = __sync_fetch_and_add(t->counter,1);
   if(index >= t->stop) break;
   (*t->delegate)(t->id, index);
  }
  t->time += time.cycleCount();
  results.release(1);
 }
 return 0;
}

extern const int threadCount;



/// Runs a loop in parallel
template<Type F> uint64 parallel_for(int64 start, int64 stop, F f, const int unused threadCount = ::threadCount) {
#if DEBUG || PROFILE
 tsc time; time.start();
 for(int64 i : range(start, stop)) f(0, i);
 return time.cycleCount();
#else
 if(threadCount == 1) {
  tsc time; time.start();
  for(int64 i : range(start, stop)) f(0, i);
  return time.cycleCount();
 } else {
  function<void(uint, uint)> delegate = f;
  assert_(threadCount == ::threadCount);
  for(uint index: range(threadCount)) {
   threads[index].counter = &start;
   threads[index].stop = stop;
   threads[index].delegate = &delegate;
   threads[index].time = 0;
  }
  jobs.release(threadCount);
  results.acquire(threadCount);
  uint64 time = 0;
  for(uint index: range(threadCount)) time += threads[index].time;
  return time;
 }
#endif
}

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> uint64 parallel_chunk(int64 totalSize, F f, const uint threadCount = ::threadCount) {
 if(totalSize <= threadCount/**threadCount*/ || threadCount==1) {
  tsc time; time.start();
  f(0, 0, totalSize);
  return time.cycleCount();
 }
 const int64 chunkSize = (totalSize+threadCount-1)/threadCount;
 const int64 chunkCount = (totalSize+chunkSize-1)/chunkSize; // Last chunk might be smaller
 assert_((chunkCount-1)*chunkSize < totalSize && totalSize <= chunkCount*chunkSize, (chunkCount-1)*chunkSize, totalSize, chunkCount*chunkSize);
 //assert_(chunkCount == threadCount, chunkCount, threadCount, chunkSize);
 return parallel_for(0, chunkCount, [&](uint id, int64 chunkIndex) {
  f(id, chunkIndex*chunkSize, min(chunkSize, totalSize-chunkIndex*chunkSize));
 }, threadCount);
}

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> uint64 parallel_chunk(int64 start, int64 stop, F f, const uint threadCount = ::threadCount) {
 return parallel_chunk(stop-start, [&](uint id, int64 I0, int64 DI) {
  f(id, start+I0, DI);
 }, threadCount);
}
