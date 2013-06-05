#pragma once
/// \file trace.h stack trace using ELF/DWARF debug informations
#include "string.h"

string demangle(const ref<byte>& symbol);
struct Symbol { string function; ref<byte> file; uint line=0; };
/// Returns symbolic informations (file, function and line) corresponding to \a address
Symbol findSymbol(void* address);
/// Traces current stack skipping first \a skip frames
string trace(int skip, void* ip=0);
