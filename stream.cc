#include "stream.h"
#include "string.h"

/// Returns true if there is data to read
DataStream::operator bool() { return available(1); }

#define need(size) ({ uint unused has=available(size); assert(has>=size, has, size); })
template<class T> const T& DataStream::peek() {
    need(sizeof(T));
    return *(T*)(peekData((uint)sizeof(T)).data());
}
template<class T> array<T> DataStream::peek(uint size) {
    need(size*sizeof(T));
    return array<T>(peekData((uint)size*sizeof(T)));
}
template<class T> T DataStream::read() { T t = peek<T>(); advance(sizeof(T)); return t; }
template<class T> array<T> DataStream::read(uint size) { array<T> t = peek<T>(size); advance(size*sizeof(T)); return t; }
template<class T> array<T> DataStream::readArray() { uint size=read<uint32>(); return read<T>(size); }
template<class T> array<T> DataStream::readAll() { uint size=available(-1); assert(!(size%sizeof(T))); return read<T>(size/sizeof(T)); }

DataStream::ReadOperator::operator uint32() { return s->bigEndian?swap32(s->read<uint32>()):s->read<uint32>(); }
DataStream::ReadOperator::operator int32() { return operator uint32(); }
DataStream::ReadOperator::operator uint16() { return s->bigEndian?swap16(s->read<uint16>()):s->read<uint16>(); }
DataStream::ReadOperator::operator int16() { return operator uint16(); }
template<class T> DataStream::ReadOperator::operator T() { return s->read<T>(); }
DataStream::ReadOperator DataStream::read() { return {this}; }

//DataStream& DataStream::operator ++(int) { index++; return *this; }
//DataStream& DataStream::operator +=(int count) { index+=count; return *this; }

template<class T> bool DataStream::matchAny(const array<T>& any) {
    if(available(sizeof(T))>=sizeof(T)) for(const T& e: any) if(peek<T>() == e) { advance(sizeof(T)); return true; }
    return false;
}
template<class T> bool DataStream::match(const array<T>& key) {
    if(available(key.size()*sizeof(T))>=key.size()*sizeof(T) && peek<T>(key.size()) == key) { advance(key.size()*sizeof(T)); return true; } else return false;
}
template<class T> array<T> DataStream::until(const T& key) {
    array<T> a;
    while(available(sizeof(T))>=sizeof(T)) { T t = read<T>(); if(t==key) break; else a<<t; }
    return a;
}
template<class T> array<T> DataStream::until(const array<T>& key) {
    array<T> a;
    for(;available(key.size()*sizeof(T))>=key.size()*sizeof(T) && peek<T>(key.size()) != key;) a << read<T>();
    match(key);
    return a;
}
template<class T> void DataStream::whileAny(const array<T>& any) { while(matchAny(any)); }
template<class T> array<T> DataStream::untilAny(const array<T>& any) {
    array<T> a;
    while(available(sizeof(T))>=sizeof(T) && !matchAny(any)) a<<read<T>();
    return a;
}

template char DataStream::read();
template uint DataStream::read();
template array<char> DataStream::read(uint);
template array<char> DataStream::readArray();
template array<char> DataStream::readAll();
template bool DataStream::match(const array<char>&);
template array<char> DataStream::until(const char&);
template array<char> DataStream::until(const array<char>&);
template void DataStream::whileAny(const array<char>&);
template array<char> DataStream::untilAny(const array<char>&);
template DataStream::ReadOperator::operator char();
template DataStream::ReadOperator::operator uint8();

Buffer::Buffer(array<byte>&& buffer) : buffer(move(buffer)) {}
uint Buffer::available(uint) { return buffer.size()-index; }
void Buffer::advance(int count) { index+=count;  assert(index<=buffer.size()); }
array<byte> Buffer::peekData(uint size) { assert(index+size<=buffer.size());  return copy(array<byte>(buffer.data()+index,size)); }

void TextStream::skip() { whileAny(" \t\n\r"_); }
string TextStream::until(const string& key) { return DataStream::until((array<char>&)key); }
string TextStream::word() {
    string word;
    for(;available(1);) { char c=peek<char>(); if(!(c>='a'&&c<='z')) break; word<<c; advance(1); }
    return word;
}
string TextStream::xmlIdentifier() {
    string identifier;
    for(;available(1);) {
        char c=peek<char>();
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==':'||c=='-'||c=='_')) break;
        identifier<<c;
        advance(1);
    }
    return identifier;
}
