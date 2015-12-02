#include "parallel.h"
Semaphore jobs;
Semaphore results;
thread threads[::maxThreadCount];
