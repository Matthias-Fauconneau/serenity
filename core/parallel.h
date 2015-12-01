#pragma once
#include <pthread.h> //pthread
#include "data.h"

// \file parallel.h

static const size_t maxThreadCount = 12; // 4..32

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
 uint64 time;
};
static Semaphore jobs;
static Semaphore results;
static thread threads[::maxThreadCount];
inline void* start_routine(thread* t) {
 for(;;) {
  jobs.acquire(1);
  tsc time; time.start();
  for(;;) {
   int64 index = __sync_fetch_and_add(t->counter,1);
   if(index >= t->stop) break;
   (*t->delegate)(t->id, index);
  }
  t->time = time.cycleCount();
  results.release(1);
 }
 return 0;
}

/// Runs a loop in parallel
template<Type F> uint64 parallel_for(int64 start, int64 stop, F f, const int unused threadCount = ::threadCount) {
#if DEBUG || PROFILE
 for(int64 i : range(start, stop)) f(0, i);
#else
 if(threadCount == 1) {
  tsc time; time.start();
  for(int64 i : range(start, stop)) f(0, i);
  return time.cycleCount();
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
  uint64 time = 0;
  for(uint index: range(threadCount)) time += threads[index].time;
  return time;
 }
#endif
}
template<Type F> uint64 parallel_for(uint stop, F f, const uint unused threadCount = ::threadCount) {
 return parallel_for(0, stop, f, threadCount);
}

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> uint64 parallel_chunk(int64 totalSize, F f, const uint threadCount = ::threadCount) {
 if(totalSize <= threadCount*threadCount || threadCount==1) {
  tsc time; time.start();
  f(0, 0, totalSize);
  return time.cycleCount();
 }
 const int64 chunkSize = (totalSize+threadCount-1)/threadCount;
 const int64 chunkCount = (totalSize+chunkSize-1)/chunkSize; // Last chunk might be smaller
 assert_((chunkCount-1)*chunkSize < totalSize && totalSize <= chunkCount*chunkSize, (chunkCount-1)*chunkSize, totalSize, chunkCount*chunkSize);
 assert_(chunkCount == threadCount, chunkCount, threadCount, chunkSize);
 return parallel_for(0, chunkCount, [&](uint id, int64 chunkIndex) {
   f(id, chunkIndex*chunkSize, min(chunkSize, totalSize-chunkIndex*chunkSize));
 }, threadCount);
}
/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> uint64 parallel_chunk(int64 start, int64 stop, F f, const uint threadCount = ::threadCount) { return parallel_chunk(stop-start, [&](uint id, int64 I0, int64 DI) { f(id, start+I0, DI); }, threadCount); }
