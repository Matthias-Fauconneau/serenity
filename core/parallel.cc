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

static size_t spawnWorkers() {
 size_t threadCount = coreCount();
 for(uint index: range(threadCount)) {
  threads[index].id = index;
  pthread_create(&threads[index].pthread, 0, (void*(*)(void*))start_routine, &threads[index]);
 }
 return threadCount;
}

const int threadCount = spawnWorkers();
