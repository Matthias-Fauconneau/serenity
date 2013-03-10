#pragma once
/// \file disasm.h x86 (including SSE2) disassembler
#include "core.h"

/// Log disassembly of \a code
void disassemble(const ref<byte>& code);

/// Executes \a body and logs once its disassembly
#define disasm(id,body) ({ static bool unused once = ({log(#id);disassemble(ref<byte>((byte*)&&begin##id,(byte*)&&end##id));true;}); begin##id: body; end##id:; })
