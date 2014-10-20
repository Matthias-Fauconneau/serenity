#pragma once
#include "string.h"
#include "map.h"
#include "data.h"
//#include "vector.h"

struct Variant {
    enum { Empty, Boolean, Integer, Real, Data, List, Dict, Rational } type = Empty;
    double number=0;
    String data;
    array<Variant> list;
    map<String,Variant> dict;
    double denominator=1;

    Variant(bool boolean) : type(Boolean), number(boolean) {}
    Variant(int number) : type(Integer), number(number) {}
    Variant(int64 number) : type(Integer), number(number) {}
    Variant(uint number) : type(Integer), number(number) {}
    Variant(size_t number) : type(Integer), number(number) {}
    Variant(double number) : type(Real), number(number) {}
    Variant(string data) : type(Data), data(data) {}
    Variant(String&& data) : type(Data), data(move(data)) {}
    Variant(array<Variant>&& list) : type(List), list(move(list)) {}
    Variant(map<String,Variant>&& dict) : type(Dict), dict(move(dict)) {}
    //Variant(map<string,Variant>&& dict) : type(Dict) { for(auto e: dict) this->dict.insert(String(e.key), move(e.value)); }
    Variant(int64 numerator, int64 denominator) : type(Rational), number(numerator), denominator(denominator) {}

    explicit operator bool() const { return type!=Empty; }

    int64 integer() const { assert(type==Integer, *this); return number; }
    double real() const {
        if(type==Rational) { assert_((number/denominator)*denominator==number); return number/denominator; }
        assert(type==Real||type==Integer); return number;
    }
    explicit operator string() const { assert(type==Data); return data; }
    int64 numerator() {  assert(type==Rational, *this); return number; }
};

String str(const Variant& o);

inline String str(const Variant& o) {
    if(o.type==Variant::Boolean) return String(str(bool(o.number)));
    if(o.type==Variant::Integer) return str(int(o.number));
    if(o.type==Variant::Real || o.type==Variant::Rational) return str(o.real());
    if(o.type==Variant::Data) return copy(o.data);
    if(o.type==Variant::List) return str(o.list);
    if(o.type==Variant::Dict) return str(o.dict);
    error("Invalid Variant",int(o.type));
}
