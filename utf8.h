#pragma once
#include "array.h"

/// utf8_iterator is used to iterate UTF-8 encoded strings
struct utf8_iterator {
    const byte* pointer;
    utf8_iterator(const byte* pointer):pointer(pointer){}
    bool operator!=(const utf8_iterator& o) const { return o.pointer != pointer; }
    bool operator==(const utf8_iterator& o) const { return o.pointer == pointer; }
    uint operator* () const;
    const utf8_iterator& operator++();
    const utf8_iterator& operator--();
};

/// Convert Unicode code point to UTF-8
string utf8(uint code);
