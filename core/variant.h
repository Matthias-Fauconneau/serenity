#include "string.h"
#include "vector.h"
#include "data.h"
#include "map.h"

/*int2 fromInt2(string vector) {
    TextData s (vector);
    int x=s.integer(); if(!s) return int2(x);
    s.whileAny("x, "_); int y=s.integer();
    assert_(!s); return int2(x,y);
}*/

inline int3 fromInt3(string vector) {
    if(!vector) return int3(0);
    TextData s (vector);
    int x=s.integer(); if(!s) return int3(x);
    s.whileAny("x, "_); int y=s.integer();
    s.whileAny("x, "_); int z=s.integer();
    assert_(!s); return int3(x,y,z);
}

struct Variant : String {
    Variant(){}
    default_move(Variant);
    Variant(String&& s) : String(move(s)) {}
    Variant(double decimal) : String(ftoa(decimal)){}
    Variant(int3 v) : String(strx(v)) {}
    explicit operator bool() const { return size; }
    operator int() const { return *this ? fromInteger(*this) : 0; }
    operator uint() const { return *this ? fromInteger(*this) : 0; }
    operator float() const { return fromDecimal(*this); }
    operator double() const { return fromDecimal(*this); }
    operator int3() const { return fromInt3(*this); }
    //generic operator T() const { return T((const string&)*this); } // Enables implicit conversion to any type with an implicit string constructor
};
inline String str(const Variant& v) { return String((string&)v); }

// Parses process arguments into parameter=value pairs
inline map<string, Variant> parseParameters(const ref<string> args, const ref<string> parameters) {
    map<string, Variant> arguments;
    for(const string& argument: args) {
        TextData s (argument);
        string key = s.until("="_);
        if(!parameters.contains(key)) error("Unknown parameter", key, parameters);
        // Explicit argument
        string value = s.untilEnd();
        arguments.insert(key, Variant(String(value?:"1"_)));
    }
    return arguments;
}
