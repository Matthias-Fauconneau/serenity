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

/*__attribute((destructor)) void joinWorkers() {
 for(uint index: range(threadCount())) threads[index].counter = 0;
 log("release", threadCount());
 jobs.release(threadCount());
 log("released");
 for(uint index: range(threadCount())) {
  log(index);
  pthread_join(threads[index].pthread, 0);
 }
}*/
