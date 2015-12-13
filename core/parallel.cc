#include "parallel.h"

#if __MIC__
const int maxThreadCount = 60; //240;
#elif !DEBUG && 1
const int maxThreadCount = 12;
#else
const int maxThreadCount = 1;
#endif
extern thread threads[::maxThreadCount];
thread threads[::maxThreadCount];

Semaphore jobs __attribute((init_priority(101)));
Semaphore results __attribute((init_priority(101)));

int threadCount() {
 static int threadCount = ({
  TextData s(File("/proc/cpuinfo").readUpToLoop(1<<17));
  assert_(s.data.size<s.buffer.capacity, s.data.size, s.buffer.capacity, "/proc/cpuinfo", "threadCount");
  int threadCount = 0;
  while(s) { if(s.match("processor")) threadCount++; s.line(); }
#if !DEBUG && 1
  assert_(threadCount <= maxThreadCount, threadCount, maxThreadCount);
#endif
  if(environmentVariable("THREADS"_))
   threadCount = min(threadCount, (int)parseInteger(environmentVariable("THREADS"_)));
  min(threadCount, maxThreadCount);
 });
 //assert_(threadCount == maxThreadCount, threadCount, maxThreadCount);
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

extern "C" int omp_get_thread_num();
extern "C" void omp_set_num_threads(int threadCount);
uint64 parallel_for(int64 start, int64 stop, function<void(uint, uint)> delegate, const int unused threadCount) {
 if(threadCount == 1) {
  tsc time; time.start();
  for(int64 i : range(start, stop)) delegate(0, i);
  return time.cycleCount();
 } else {
#if OPENMP
  tsc time; time.start();
  omp_set_num_threads(threadCount);
  #pragma omp parallel for
  for(int i=start; i<stop; i++) delegate(omp_get_thread_num(), i);
  return time.cycleCount();
#else
  assert_(threadCount == ::threadCount());
  for(int index: range(::threadCount())) {
   threads[index].counter = &start;
   threads[index].stop = stop;
   threads[index].delegate = &delegate;
   threads[index].time = 0;
  }
  int jobCount = ::min(threadCount, int(stop-start));
  Time time; time.start();
  jobs.release(jobCount);
  //tsc time; time.start();
  results.acquire(jobCount);
  return time.nanoseconds();
  //return time.cycleCount();
  //uint64 time = 0; for(int index: range(::threadCount())) time += threads[index].time; return time;
#endif
 }
}
