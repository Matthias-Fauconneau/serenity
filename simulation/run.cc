#include "simulation.h"
#include "thread.h"
#include "parallel.h"
#include <unistd.h>

__attribute((constructor(101))) void logCompiler() {
#if __INTEL_COMPILER
 log("ICC", simd, ARGS);
#elif __clang__
 log("Clang", simd, ARGS);
#elif __GNUC__
 log("gcc", simd, ARGS);
#else
#error
#endif
#if OPENMP
 log("OpenMP", ::threadCount());
#else
 log("PThread", ::threadCount());
#endif
 //log_(File("/proc/sys/kernel/hostname").readUpTo(256));
 log(arguments());
}

Dict parameters() {
 Dict parameters;
 for(string argument: arguments()) parameters.append(parseDict(argument));
 return parameters;
}

struct SimulationView : Simulation {
 SimulationView(const Dict& parameters) : Simulation(parameters) { run(); }
} app (parameters());
