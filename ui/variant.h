#pragma once
#include "string.h"
#include "map.h"

struct Variant { //FIXME: union
    enum { Empty, Boolean, Integer, Real, Data, List, Dict } type = Empty;
    double number=0; String data; array<Variant> list; map<string,Variant> dict;
    Variant(bool boolean) : type(Boolean), number(boolean) {}
    Variant(int number) : type(Integer), number(number) {}
    Variant(uint number) : type(Integer), number(number) {}
    Variant(size_t number) : type(Integer), number(number) {}
    Variant(double number) : type(Real), number(number) {}
    Variant(string data) : type(Data), data(data) {}
    Variant(String&& data) : type(Data), data(move(data)) {}
    Variant(array<Variant>&& list) : type(List), list(move(list)) {}
    Variant(map<string,Variant>&& dict) : type(Dict), dict(move(dict)) {}
    explicit operator bool() const { return type!=Empty; }
    operator int() const { assert(type==Integer, *this); return number; }
    int integer() const { assert(type==Integer, *this); return number; }
    double real() const { assert(type==Real||type==Integer); return number; }
};

String str(const Variant& o);
String str(const array<Variant>& array) {
    String s;
    s << "["_;
    for(const Variant& element: array) s << str(element) << " "_;
    if(array) s.last() = ']'; else s << ']';
    return s;
}

String str(const map<string,Variant>& dict) {
    String s;
    s << "<<"_;
    for(const const_pair<string,Variant>& entry: dict) s << "/"_+entry.key+" "_<<str(entry.value)<<" "_;
    if(dict) s.last() = '>'; else s << '>'; s << '>';
    return s;
}

String str(const Variant& o) {
    if(o.type==Variant::Boolean) return String(o.number?"true"_:"false"_);
    if(o.type==Variant::Integer) return str(int(o.number));
    if(o.type==Variant::Real) return str(float(o.number));
    if(o.type==Variant::Data) return copy(o.data);
    if(o.type==Variant::List) return str(o.list);
    if(o.type==Variant::Dict) return str(o.dict);
    error("Invalid Variant"_,int(o.type));
}

typedef map<string,Variant> Dict;

struct Object : Dict {
    void operator =(buffer<byte>&& data) {
        this->data = move(data);
        insert("Length"_, this->data.size);
    }

    buffer<byte> data;
};

String str(const Object& o) {
    String s = str((const Dict&)o);
    if(o.data) {
        assert_(o.at("Length"_) == int(o.data.size), (const Dict&)o);
        s << "stream\n"_;
        s << o.data;
        s << "\nendstream"_;
    }
    return s;
}
