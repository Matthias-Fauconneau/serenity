#pragma once
#include "data.h"
#include "vector.h"
#include "map.h"

// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }
generic T parse(string str) { TextData s(str); T t = parse<T>(s); assert_(!s, str); return t; }
template<> inline uint parse<uint>(TextData& s) { return s.integer(false); }
template<> inline float parse<float>(TextData& s) { return s.decimal(); }
template<Type V> V parseVec(TextData& s) {
    V value;
    for(uint index: range(V::N)) { value[index] = parse<Type V::T>(s); if(index<V::N-1) s.whileAny(' '); }
    return value;
}
template<> inline uint4 parse<uint4>(TextData& s) { return parseVec<uint4>(s); }
template<> inline vec3 parse<vec3>(TextData& s) { return parseVec<vec3>(s); }
