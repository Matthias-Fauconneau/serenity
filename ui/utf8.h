#pragma once
/// \file utf8.h UTF-8 iterator and encoder
#include "string.h"

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

/// Converts UTF-8 String to UCS-2
array<uint16> toUCS2(string utf8);
/// Converts UTF-8 String to UCS-4
array<uint32> toUCS4(string utf8);

/// Converts UCS-2 String to UTF-8
String toUTF8(ref<uint16> ucs);
/// Converts UCS-4 String to UTF-8
String toUTF8(ref<uint32> ucs);
