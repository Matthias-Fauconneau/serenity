#pragma once
#include "core.h"
void disassemble(const ref<byte>& code);

#if __x86_64__
inline uint64 rdtsc() { uint32 lo, hi; asm volatile("rdtsc" : "=a" (lo), "=d" (hi)); return (uint64)hi << 32 | lo; }
/// Returns the number of cycles used to execute \a statements
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc { uint64 start=rdtsc(); operator uint64(){ return rdtsc()-start; } };
#endif

/// Times \a body execution
#define time(body) ({ tsc t; body t; })

/// Times \a body execution and logs its disassembly once
#define disasm(id, body) ({ \
    begin##id: uint64 t=time(body) end##id: \
    static bool unused once = ({log(#id);disassemble(ref<byte>((byte*)&&begin##id,(byte*)&&end##id));true;}); \
    t; })
