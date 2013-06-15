#pragma once
/// \file utf8.h UTF-8 iterator and encoder
#include "array.h"

/// Iterates UTF-8 encoded strings
struct utf8_iterator {
    const byte* pointer;
    utf8_iterator(const byte* pointer):pointer(pointer){}
    bool operator!=(const utf8_iterator& o) const { return o.pointer != pointer; }
    uint operator* () const;
    const utf8_iterator& operator++();
};

/// Converts Unicode code point to UTF-8
String utf8(uint code);

/// Converts UTF8 String to UTF32
array<uint> toUTF32(string utf8);

/// Converts UTF32 String to UTF8
String toUTF8(ref<uint> utf32);
