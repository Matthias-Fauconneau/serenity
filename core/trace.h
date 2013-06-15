#pragma once
/// \file trace.h stack trace using ELF/DWARF debug informations
#include "string.h"

String demangle(const string& symbol);
struct Symbol { String function; string file; uint line=0; };
/// Returns symbolic informations (file, function and line) corresponding to \a address
Symbol findSymbol(void* address);
/// Traces current stack skipping first \a skip frames
String trace(int skip, void* ip=0);
