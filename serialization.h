#pragma once
#include "data.h"
#include "map.h"
#include "file.h"
#include "function.h"
#include "vector.h" // int2

// Enforces exact match for overload resolution
generic T parse(TextData&) { static_assert(0&&sizeof(T), "No overload for parse<T>(TextData&)"); }

generic T parse(string str) { TextData s(str); T t = parse<T>(s); assert_(!s, s); return t; }
template<> inline String parse<String>(string source) { return String(source); }

template<> inline int parse<int>(TextData& s) { return s.integer(); }
template<> inline float parse<float>(TextData& s) { return s.decimal(); }

generic array<T> parseArray(string str) { TextData s(str); auto t = parseArray<T>(s); assert_(!s, s); return t; }
generic array<T> parseArray/*<array<T>>*/(TextData& s) {
	array<T> target;
	s.match('[');
	if(s && !s.match(']')) for(;;) {
		s.whileAny(" \t");
		target.append( parse<T>(s) );
		if(!s || s.match(']')) break;
		if(!s.match(',') && !s.match(' ')) s.skip('\n');
	}
	assert_(!s);
	return target;
}

template<> inline map<String, String> parse<map<String, String>>(TextData& s) {
	map<String, String> target;
	s.match('{');
	if(s && !s.match('}')) for(;;) {
		s.whileAny(" \t");
		string key = s.identifier();
		s.match(':'); s.whileAny(" \t");
		string value = s.whileNo("},\t\n");
		target.insert(key, trim(value));
		if(!s || s.match('}')) break;
		if(!s.match(',')) s.skip('\n');
	}
	assert_(!s);
	return target;
}

template<template<typename> class V, Type T, uint N> vec<V,T,N> parseVector/*<vec<V,T,N>>*/(TextData& s) {
	bool bracket = s.match('(');
	vec<V,T,N> value;
	value[0] = parse<T>(s); // Assigns a single value to all components
	if(!bracket && !s) return value[0];
	for(uint index: range(1, N)) { s.whileAny("x, "); value[index] = parse<T>(s); }
	if(bracket) s.skip(')');
	return value;
}
//generic T parseVector(TextData& s) { return parseVector<T::_V, T::_T, T::_N>(s); }
// FIXME: function template partial specialization is not allowed
//template<> inline int2 parse<int2>(TextData& s) { return parseVector<int2>(s); }
//template<> inline vec2 parse<int2>(TextData& s) { return parseVector<vec2>(s); }
template<> inline int2 parse<int2>(TextData& s) { return parseVector<xy, int, 2>(s); }
template<> inline vec2 parse<vec2>(TextData& s) { return parseVector<xy, float, 2>(s); }

generic struct PersistentValue : T {
	File file;
	function<T()> evaluate;

	PersistentValue(File&& file, function<T()> evaluate={}) : T(parse<T>(string(file.read(file.size())))), file(move(file)), evaluate(evaluate) {}
	PersistentValue(const Folder& folder, string name, function<T()> evaluate={})
		: PersistentValue(File(name, folder, Flags(ReadWrite|Create)), evaluate) {}
	~PersistentValue() { file.seek(0); file.resize(file.write(evaluate ? str(evaluate()) : str((const T&)*this))); }
};
generic String str(const PersistentValue<T>& o) { return str((const T&)o); }
