#pragma once
#include "core.h"
#include "time.h"

/// Log disassembly of \a code
void disassemble(const ref<byte>& code);

/// Times \a body execution and logs its disassembly once
#define disasm(id, body) ({ \
    uint64 t=rdtsc(); begin##id: body; end##id: t=rdtsc()-t; \
    static bool unused once = ({log(#id);disassemble(ref<byte>((byte*)&&begin##id,(byte*)&&end##id));true;}); \
    t; })
