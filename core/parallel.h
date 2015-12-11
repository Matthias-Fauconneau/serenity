#pragma once
/// \file parallel.h
#include <pthread.h> //pthread
#include "data.h"
#include "file.h"
#include "function.h"
#include "thread.h"
#include "time.h"

struct thread {
 pthread_t pthread;
 int64 id; int64* counter; int64 stop;
 function<void(uint, uint)>* delegate;
 uint64 time = 0;
};

extern const int maxThreadCount;
extern Semaphore jobs;
extern Semaphore results;

int threadCount();

/// Runs a loop in parallel
uint64 parallel_for(int64 start, int64 stop, function<void(uint, uint)> delegate, const int unused threadCount = ::threadCount());

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> uint64 parallel_chunk(size_t jobCount, F f, const uint threadCount = ::threadCount()) {
 if(threadCount==1) {
  tsc time; time.start();
  f(0, 0, jobCount);
  return time.cycleCount();
 }
 const size_t chunkSize = max(1ul, jobCount/threadCount);
 const size_t chunkCount = (jobCount+chunkSize-1)/chunkSize; // Last chunk might be smaller
 return parallel_for(0, chunkCount, [&](uint id, int64 chunkIndex) {
  f(id, chunkIndex*chunkSize, min<size_t>(chunkSize, jobCount-chunkIndex*chunkSize));
 }, threadCount);
}

/// Runs a loop in parallel chunks with chunk-wise functor
template<Type F> uint64 parallel_chunk(int64 start, int64 stop, F f, const uint threadCount = ::threadCount()) {
 return parallel_chunk(stop-start, [&](uint id, int64 I0, int64 DI) {
  f(id, start+I0, DI);
 }, threadCount);
}
