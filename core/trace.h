#pragma once
/// \file trace.h stack trace using ELF/DWARF debug informations
#include "string.h"

struct Symbol {
    Symbol() {}
    Symbol(Symbol&& o) : function(::move(o.function)), file(o.file), line(o.line) {}
    String function; string file; int line=0;
};

/// Returns symbolic informations (file, function and line) corresponding to \a address
Symbol findSymbol(void* address);

/// Traces current stack skipping first \a skip frames
String trace(int skip=0, void* ip=0);
