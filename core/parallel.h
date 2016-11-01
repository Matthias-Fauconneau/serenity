#pragma once
/// \file parallel.h
#include <pthread.h> //pthread
#include "data.h"
#include "file.h"
#include "function.h"
#include "time.h"

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
 assert_(jobCount);
 assert_(threadCount);
 const size_t chunkSize = (jobCount+threadCount-1)/threadCount;
 assert_(chunkSize);
 const size_t chunkCount = (jobCount+chunkSize-1)/chunkSize; // Last chunk might be smaller
 assert_(chunkCount <= threadCount);
 assert_((chunkCount-1)*chunkSize < jobCount);
 assert_(jobCount <= chunkCount*chunkSize);
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
