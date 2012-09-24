#pragma once
/// \file disasm.h x86 (including SSE2) disassembler
#include "core.h"
#include "time.h"

/// Log disassembly of \a code
void disassemble(const ref<byte>& code);

/// Executes \a body and logs once its disassembly
#define disasm(id,body) ({ begin##id: body; end##id: static bool unused once = ({log(#id);disassemble(ref<byte>((byte*)&&begin##id,(byte*)&&end##id));true;}); })
