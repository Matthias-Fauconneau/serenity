#pragma once
#include "string.h"
#include "map.h"
#include "data.h"
//#include "vector.h"

struct Variant {
    enum { Null, Boolean, Integer, Real, Data, List, Dict, Rational } type = Null;
    double number = 0;
    String data;
    array<Variant> list;
    map<String,Variant> dict;
    double denominator = 1;

    Variant(decltype(nullptr)) : type(Null) {}
    Variant(bool boolean) : type(Boolean), number(boolean) {}
    Variant(int number) : type(Integer), number(number) {}
    Variant(int64 number) : type(Integer), number(number) {}
    Variant(uint number) : type(Integer), number(number) {}
    Variant(size_t number) : type(Integer), number(number) {}
    Variant(double number) : type(Real), number(number) {}
	Variant(string data) : type(Data), data(copyRef(data)) {}
    Variant(String&& data) : type(Data), data(move(data)) {}
    Variant(array<Variant>&& list) : type(List), list(move(list)) {}
    Variant(map<String,Variant>&& dict) : type(Dict), dict(move(dict)) {}
	Variant(int64 numerator, int64 denominator) : type(Rational), number(numerator), denominator(denominator) {}

    explicit operator bool() const { return type != Null; }

    int64 integer() const { assert_(type==Integer, *this); return number; }
    double real() const {
        if(type==Rational) { assert_((number/denominator)*denominator==number); return number/denominator; }
        assert(type==Real||type==Integer); return number;
    }
    explicit operator string() const { assert(type==Data); return data; }
    int64 numerator() {  assert(type==Rational, *this); return number; }
};

generic String str(const map<T,Variant>& dict) {
	array<char> s;
	s.append("<<"); for(auto entry: dict) s.append( '/'+entry.key+' '+str(entry.value)+' ' ); s.append(">>");
	return move(s);
}

inline String str(const Variant& o) {
	if(o.type==Variant::Boolean) return unsafeRef(str(bool(o.number)));
    if(o.type==Variant::Integer) return str(int(o.number));
    if(o.type==Variant::Real || o.type==Variant::Rational) return str(o.real());
    if(o.type==Variant::Data) return copy(o.data);
    if(o.type==Variant::List) return str(o.list);
    if(o.type==Variant::Dict) return str(o.dict);
    error("Invalid Variant",int(o.type));
}

typedef map<String,Variant> Dict;
