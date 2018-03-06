#pragma once
#include "data.h"
#include "vector.h"
#include "map.h"

// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }
generic T parse(string str) { TextData s(str); T t = parse<T>(s); assert_(!s, str); return t; }
template<> inline uint parse<uint>(TextData& s) { return s.integer(false); }
template<> inline float parse<float>(TextData& s) { return s.decimal(); }
#if 0
template<template<Type> Type V, Type T, uint N> vec<V,T,N> parseVector/*<vec<V,T,N>>*/(TextData& s) {
    vec<V,T,N> value;
    value[0] = parse<T>(s); // Assigns a single value to all components
    for(uint index: range(1, N)) { assert_(s.matchAny("x, ")); value[index] = parse<T>(s); }
    return value;
}
#else
template<Type V> V parseVec(TextData& s) {
    V value;
    for(uint index: range(V::N)) { value[index] = parse<Type V::T>(s); if(index<V::_N-1) s.whileAny(' '); }
    return value;
}
#endif
//generic T parseVector(TextData& s) { return parseVector<T::_V, T::_T, T::_N>(s); } // Undefined function template partial specialization
//template<> inline uint2 parse<uint2>(TextData& s) { return parseVector<xy, uint, 2>(s); }
//template<> inline uint3 parse<uint3>(TextData& s) { return parseVector<xyz, uint, 3>(s); }
template<> inline uint4 parse<uint4>(TextData& s) { return parseVec<uint4>(s); }
template<> inline vec3 parse<vec3>(TextData& s) { return parseVec<vec3>(s); }
template<> inline rgb3f parse<rgb3f>(TextData& s) { return parseVec<rgb3f>(s); }
template<> inline rgba4f parse<rgba4f>(TextData& s) { return parseVec<rgba4f>(s); }

static inline ::map<string, string> parse(const ref<string> arguments) {
    ::map<string, string> args;
    for(string arg: arguments) {
        if(arg.contains('=')) args.insert(section(arg,'='), section(arg,'=',-2,-1));
        else args.insert(arg);
    }
    return args;
}

/*static inline ::map<string, string> parse(TextData& s) {
    ::map<string, string> map;
    while(s) {
        string key = s.whileNo("= ");
        assert_(key);
        string value;
        if(s.match('=')) value = s.until(' ');
        assert_(value);
        map.insert(key, value);
    }
    return map;
}
static inline ::map<string, string> parse(string s) { TextData o(s); return parse(o); }*/
static inline ::map<string, string> parse(string s) { return parse(split(s," ")); }
