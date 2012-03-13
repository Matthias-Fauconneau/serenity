#include "stream.h"
#include "string.h"

Stream::Stream(array<byte>&& buffer) : buffer(move(buffer)), data(this->buffer.data()), index(0), bigEndian(false) {}

/// Returns true if there is data to read
Stream::operator bool() const { return index<buffer.size(); }

template<class T> const T& Stream::peek() const { return *(T*)(data+index); }
template<class T> array<T> Stream::peek(int size) const { return array<T>((T*)(data+index),size); }
template<class T> T Stream::read() { assert(index+sizeof(T)<=buffer.size()); T t = *(T*)(data+index); index+=sizeof(T); return t; }
template<class T> array<T> Stream::read(int size) {
    assert(index*sizeof(T)<=buffer.size());
    array<T> t(data+index,size);
    index+=size*sizeof(T);
    return copy(t);
}
template<class T> array<T> Stream::readArray() { uint32 size=read(); return read<T>(size); }

Stream::ReadOperator::operator uint32() { return s->bigEndian?swap32(s->read<uint32>()):s->read<uint32>(); }
Stream::ReadOperator::operator int32() { return operator uint32(); }
Stream::ReadOperator::operator uint16() { return s->bigEndian?swap16(s->read<uint16>()):s->read<uint16>(); }
Stream::ReadOperator::operator int16() { return operator uint16(); }
template<class T> Stream::ReadOperator::operator T() { return s->read<T>(); }
Stream::ReadOperator Stream::read() { return {this}; }

Stream& Stream::operator ++(int) { index++; return *this; }
Stream& Stream::operator +=(int count) { index+=count; return *this; }

template<class T> bool Stream::matchAny(const array<T>& any) {
    for(const T& e: any) if(peek<T>() == e) { index+=sizeof(T); return true; } return false;
}
template<class T> bool Stream::match(const array<T>& key) {
    if(peek<T>(key.size()) == key) { index+=key.size()*sizeof(T); return true; } else return false;
}
template<class T> array<T> Stream::until(const T& key) {
    int start=index;
    while(index<buffer.size() && peek<T>()!=key) index+=sizeof(T);
    index+=sizeof(T);
    return copy(array<T>(data+start,index-start-sizeof(T)));
}
template<class T> array<T> Stream::whileAny(const array<T>& any) {
    int start=index;
    while(index<buffer.size() && matchAny(any));
    return copy(array<T>(data+start,index-start));
}
template<class T> array<T> Stream::untilAny(const array<T>& any) {
    int start=index;
    while(index<buffer.size() && !matchAny(any)) index+=sizeof(T);
    return copy(array<T>(data+start,index-sizeof(T)-start));
}

template char Stream::read();
template uint Stream::read();
template array<char> Stream::read(int);
template array<char> Stream::readArray();
template bool Stream::match(const array<char>&);
template array<char> Stream::until(const char&);
template array<char> Stream::whileAny(const array<char>&);
template array<char> Stream::untilAny(const array<char>&);

template Stream::ReadOperator::operator char();
template Stream::ReadOperator::operator uint8();
