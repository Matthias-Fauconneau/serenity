#include "string.h"
#include "vector.h"
#include "data.h"
#include "map.h"

inline int3 fromInt3(TextData& s) {
    int x=s.integer(); if(!s) return int3(x);
    s.whileAny("x, "_); int y=s.integer();
    s.whileAny("x, "_); int z=s.integer();
    assert_(!s); return int3(x,y,z);
}
inline int3 fromInt3(string str) { TextData s(str); return fromInt3(s); }

struct Variant : String {
    bool isInteger = false; // for proper display of integers (without decimal points)
    Variant(){}
    default_move(Variant);
    Variant(String&& s) : String(move(s)) {}
    Variant(int integer) : String(dec(integer)), isInteger(true) {}
    Variant(double decimal) : String(ftoa(decimal)){}
    //Variant(int3 v) : String(strx(v)) {}
    explicit operator bool() const { return size; }
    operator int() const { return *this ? fromInteger(*this) : 0; }
    operator uint() const { return *this ? fromInteger(*this) : 0; }
    operator float() const { return fromDecimal(*this); }
    operator double() const { return fromDecimal(*this); }
    operator int3() const { return fromInt3(*this); }
    //generic operator T() const { return T((const string&)*this); } // Enables implicit conversion to any type with an implicit string constructor
};
inline String str(const Variant& v) { return String((string&)v); }
inline bool operator <(const Variant& a, const Variant& b) { if(isDecimal(a) && isDecimal(b)) return fromDecimal(a) < fromDecimal(b); else return (string)a < (string)b; }

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
