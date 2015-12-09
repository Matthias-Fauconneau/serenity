#include "parallel.h"

thread threads[::maxThreadCount];

Semaphore jobs __attribute((init_priority(101)));
Semaphore results __attribute((init_priority(101)));

size_t threadCount() {
 static size_t threadCount = ({
  TextData s(File("/proc/cpuinfo").readUpToLoop(1<<17));
  assert_(s.data.size<s.buffer.capacity, s.data.size, s.buffer.capacity, "/proc/cpuinfo", "threadCount");
  size_t threadCount = 0;
  while(s) { if(s.match("processor")) threadCount++; s.line(); }
  //assert_(threadCount <= maxThreadCount, threadCount, maxThreadCount);
  if(environmentVariable("THREADS"_))
   threadCount = min(threadCount, (size_t)parseInteger(environmentVariable("THREADS"_)));
  min(threadCount, maxThreadCount);
 });
 return threadCount;
}

inline void* start_routine(thread* t) {
 for(;;) {
  jobs.acquire(1);
  if(!t->counter) { log(t->id); return 0; }
  tsc time; time.start();
  for(;;) {
   int64 index = __sync_fetch_and_add(t->counter,1);
   if(index >= t->stop) break;
   (*t->delegate)(t->id, index);
  }
  t->time += time.cycleCount();
  results.release(1);
 }
}

__attribute((constructor(102))) void spawnWorkers() {
 for(uint index: range(threadCount())) {
  threads[index].id = index;
  pthread_create(&threads[index].pthread, 0, (void*(*)(void*))start_routine, &threads[index]);
 }
}

uint64 parallel_for(int64 start, int64 stop, function<void(uint, uint)> delegate, const size_t unused threadCount) {
 if(threadCount == 1) {
  tsc time; time.start();
  for(int64 i : range(start, stop)) delegate(0, i);
  return time.cycleCount();
 } else {
  /*for(size_t index: range(::threadCount())) {
   threads[index].counter = &start;
   threads[index].stop = stop;
   threads[index].delegate = &delegate;
   threads[index].time = 0;
  }
  size_t jobCount = ::min(threadCount, size_t(stop-start));
  jobs.release(jobCount);
  results.acquire(jobCount);
  uint64 time = 0;
  for(size_t index: range(::threadCount())) time += threads[index].time;
  return time;*/
  tsc time; time.start();
  #pragma omp parallel for
  for(int i=start; i<stop; i++) delegate(0, i);
  return time.cycleCount();
 }
}
