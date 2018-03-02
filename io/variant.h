#pragma once
#include "string.h"
#include "map.h"
#include "data.h"

struct Variant {
 enum { Null, Boolean, Integer, Real, Data, List, Dict, Rational } type = Null;
 static constexpr string types[] = {"0","bool","int","real","data","[]","{}","/"};
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

 //explicit operator bool() const { return type != Null; }

 float real() const {
  if(type==Rational) { return number/denominator; }
  if(type==Real||type==Integer) return number;
  error("NaN"_, types[int(type)], *this);
 }

          operator bool() const { assert_(type==Boolean); return number; }
          operator string() const { assert_(type==Data); return data; }
          operator float() const { return real(); }
          operator const map<String, Variant>&() const { assert_(type==Dict); return dict; }
 explicit operator const ref<Variant>() const { assert_(type==List); return list; }

 const Variant& operator[](const size_t index) const { assert_(type==List); return list[index]; }

 bool contains(const string key) const { assert_(type==Dict); return dict.contains(key); }
 const Variant& operator()(const string key) const { assert_(type==Dict); return dict.at(key); }

 inline const Variant* begin() const { assert_(type==List); return list.begin(); }
 inline const Variant* end() const { assert_(type==List); return list.end(); }

 inline bool operator==(const string& s) const { return (string)*this == s; }
};

inline String str(const Variant& o) {
 if(o.type==Variant::Boolean) return unsafeRef(str(bool(o.number)));
 if(o.type==Variant::Integer) { assert(o.number==int(o.number)); return str(int(o.number)); }
 if(o.type==Variant::Real || o.type==Variant::Rational) return str(o.real());
 if(o.type==Variant::Data) return unsafeRef(o.data);
 if(o.type==Variant::List) return str(o.list);
 if(o.type==Variant::Dict) return str(o.dict);
 if(o.type==Variant::Null) return String();
 error("Invalid variant", Variant::types[int(o.type)]);
}

typedef map<String,Variant> Dict; /// Associative array of variants
