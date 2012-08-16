#pragma once
#include "string.h"

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

/*/// \a utf8_string is an \a array of characters with specialized methods for UTF-8 string handling
struct utf8_string : array<byte> {
    //using array<byte>::array<byte>;
    string() {}
    explicit string(uint capacity):array<byte>(capacity){}
    string(array<byte>&& o):array<byte>(move(o)){}
    string(const list<byte>& list):array<byte>(list){}
    string(const byte* data, uint size):array<byte>(data,size){}
    string(const byte* begin,const byte* end):array<byte>(begin,end){}
    string(utf8_iterator begin,utf8_iterator end):array<byte>(begin.pointer,end.pointer){}
    const utf8_iterator begin() const { return array::begin(); }
    const utf8_iterator end() const { return array::end(); }
    uint at(uint index) const;
    uint operator [](uint i) const { return at(i); }
};
template<> inline string copy(const string& s) { return copy<byte>(s); }*/
