#pragma once
#include "string.h"
#include "map.h"
#include "deflate.h" //DEBUG

struct Variant {
    enum { Empty, Boolean, Integer, Real, Data, List, Dict } type = Empty;
    double number=0;
    String data;
    array<Variant> list;
    map<String,Variant> dict;

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
    Variant(map<string,Variant>&& dict) : type(Dict) { for(auto e: dict) this->dict.insert(String(e.key), move(e.value)); }
    explicit operator bool() const { return type!=Empty; }
    //operator int() const { assert(type==Integer, *this); return number; }
    int integer() const { assert(type==Integer, *this); return number; }
    double real() const { assert(type==Real||type==Integer); return number; }
    explicit operator string() const { assert(type==Data); return data; }
};

String str(const Variant& o);

inline String str(const array<Variant>& array) {
    String s;
    s << "["_;
    for(const Variant& element: array) s << str(element) << " "_;
    if(array) s.last() = ']'; else s << ']';
    return s;
}

inline String str(const map<string,Variant>& dict) {
    String s;
    s << "<<"_;
    for(const const_pair<string,Variant>& entry: dict) s << "/"_+entry.key+" "_<<str(entry.value)<<" "_;
    s << ">>"_;
    return s;
}

inline String str(const map<String,Variant>& dict) {
    String s;
    s << "<<"_;
    for(const const_pair<String,Variant>& entry: dict) s << "/"_+entry.key+" "_<<str(entry.value)<<" "_;
    s << ">>"_;
    return s;
}

inline String str(const Variant& o) {
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
    buffer<byte> data;

    void operator =(buffer<byte>&& data) {
        this->data = move(data);
        insert("Length"_, this->data.size);
    }
};

inline String str(const Object& o) {
    String s = str((const Dict&)o);
    if(o.data) {
        assert_(o.at("Length"_).integer() == int(o.data.size), (const Dict&)o);
        s << "\nstream\n"_;
        assert_(o.data.size <= 30 || o.value("Filter"_,""_)=="/FlateDecode"_, o.data.size, deflate(o.data).size, o.value("Filter"_,""_));
        s << o.data;
        s << "\nendstream"_;
    }
    return s;
}
