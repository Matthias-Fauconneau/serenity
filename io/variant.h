#pragma once
#include "string.h"
#include "map.h"
#include "data.h"

struct Variant {
 enum { Null, Boolean, Integer, Real, Data, List, Dict, Rational } type = Null;
 float number = 0;
 String data;
 array<Variant> list;
 map<String,Variant> dict;
 float denominator = 1;

 Variant(){}
 Variant(decltype(nullptr)) : type(Null) {}
 Variant(bool boolean) : type(Boolean), number(boolean) {}
 Variant(int number) : type(Integer), number(number) {}
 Variant(int64 number) : type(Integer), number(number) {}
 Variant(uint number) : type(Integer), number(number) {}
 Variant(size_t number) : type(Integer), number(number) {}
 Variant(double number) : type(number==int(number)?Integer:Real), number(number) {}
 Variant(string data) : type(Data), data(copyRef(data)) {}
 Variant(String&& data) : type(Data), data(move(data)) {}
 Variant(array<Variant>&& list) : type(List), list(move(list)) {}
 Variant(map<String,Variant>&& dict) : type(Dict), dict(move(dict)) {}
 Variant(int64 numerator, int64 denominator) : type(Rational), number(numerator), denominator(denominator) {}

 explicit operator bool() const { return type != Null; }

 float real() const {
  if(type==Rational) { return number/denominator; }
  if(type==Real||type==Integer) return number;
  error(int(type));
 }
 explicit operator string() const { assert_(type==Data); return data; }
 int64 numerator() {  assert(type==Rational); return number; }
 operator float() const { return real(); }
 operator const map<String, Variant>&() const { return dict; }
 operator const ref<Variant>() const { return list; }
};

inline String str(const Variant& o) {
 if(o.type==Variant::Boolean) return unsafeRef(str(bool(o.number)));
 if(o.type==Variant::Integer) { assert(o.number==int(o.number)); return str(int(o.number)); }
 if(o.type==Variant::Real || o.type==Variant::Rational) return str(o.real());
 if(o.type==Variant::Data) return unsafeRef(o.data);
 if(o.type==Variant::List) return str(o.list);
 if(o.type==Variant::Dict) return str(o.dict);
 if(o.type==Variant::Null) return String();
 error("Invalid Variant",int(o.type));
}

typedef map<String,Variant> Dict; /// Associative array of variants
