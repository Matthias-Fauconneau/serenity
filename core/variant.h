#include "string.h"
#include "vector.h"
#include "data.h"
#include "map.h"

/// Parses 2 integers separated by 'x', ' ', or ',' to an \a int2
int2 fromInt2(TextData& s) {
    int x=s.integer(); // Assigns a single value to all components
    if(!s) return int2(x);
    s.whileAny("x, "_); int y=s.integer();
    assert_(!s); return int2(x,y);
}
/// Parses 2 integers separated by 'x', ' ', or ',' to an \a int2
inline int2 fromInt2(string str) { TextData s(str); return fromInt2(s); }

/// Parses 3 integers separated by 'x', ' ', or ',' to an \a int3
inline int3 fromInt3(TextData& s) {
    int x=s.integer();
    if(!s) return int3(x); // Assigns a single value to all components
    s.whileAny("x, "_); int y=s.integer();
    s.whileAny("x, "_); int z=s.integer();
    assert_(!s); return int3(x,y,z);
}
/// Parses 3 integers separated by 'x', ' ', or ',' to an \a int3
inline int3 fromInt3(string str) { TextData s(str); return fromInt3(s); }

/// Dynamic-typed value
/// \note Implemented as a String with implicit conversions and copy
struct Variant : String {
    bool isInteger = false; // for proper display of integers (without decimal points)
    Variant(){}
    default_move(Variant);
    Variant(String&& s) : String(move(s)) {}
    Variant(int integer) : String(dec(integer)), isInteger(true) {}
    Variant(double decimal) : String(ftoa(decimal)){}
    Variant(int2 v) : String(strx(v)) {}
    Variant(int3 v) : String(strx(v)) {}
    operator bool() const { return size && *this!="0"_; }
    operator int() const { return *this ? fromInteger(*this) : 0; }
    operator uint() const { return *this ? fromInteger(*this) : 0; }
    operator float() const { return fromDecimal(*this); }
    operator double() const { return fromDecimal(*this); }
    operator int2() const { return fromInt2(*this); }
    operator int3() const { return fromInt3(*this); }
    generic operator T() const { return T((const string&)*this); } // Enables implicit conversion to any type with an implicit string constructor
};
inline String str(const Variant& v) { return String((string&)v); }
inline bool operator <(const Variant& a, const Variant& b) { if(isDecimal(a) && isDecimal(b)) return fromDecimal(a) < fromDecimal(b); else return (string)a < (string)b; }

/// Parses process arguments into parameter=value pairs
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
