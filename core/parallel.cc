#include "parallel.h"

thread threads[::maxThreadCount];

Semaphore jobs;
Semaphore results;

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

inline void* start_routine(thread* t) {
 for(;;) {
  jobs.acquire(1);
  if(!t->counter) return 0;
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

static size_t spawnWorkers() {
 size_t threadCount = coreCount();
 for(uint index: range(threadCount)) {
  threads[index].id = index;
  pthread_create(&threads[index].pthread, 0, (void*(*)(void*))start_routine, &threads[index]);
 }
 return threadCount;
}

const int threadCount = spawnWorkers();

__attribute((destructor)) void joinWorkers() {
 for(uint index: range(threadCount)) threads[index].counter = 0;
 jobs.release(threadCount);
 for(uint index: range(threadCount)) pthread_join(threads[index].pthread, 0);
}
