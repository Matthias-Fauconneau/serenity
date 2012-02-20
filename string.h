#pragma once
#include "array.h"

/// utf8_iterator is used to iterate UTF-8 encoded strings
struct utf8_iterator {
    const char* pointer;
    utf8_iterator(const char* pointer):pointer(pointer){}
    bool operator!=(const utf8_iterator& o) const { return o.pointer != pointer; }
    int operator* () const;
    const utf8_iterator& operator++();
};

/// \a string is an \a array of characters with specialized methods for UTF-8 string handling
//TODO: proper multibyte encoding support
struct string : array<char> {
    //using array<char>::array<char>;
    string() {}
    explicit string(int capacity):array<char>(capacity){}
    string(array<char>&& o):array<char>(move(o)){}
    string(const char* data, int size):array<char>(data,size){}
    string(const char* begin,const char* end):array<char>(begin,end){}

    const utf8_iterator begin() const { return data; }
    const utf8_iterator end() const { return data+size; }

    //bool startsWith(const string& a) const { return slice(0,a.size)==a; }
    //bool endsWith(const string& a) const { return slice(size-a.size)==a; }
};
template<> inline string copy(const string& s) { return copy<char>(s); }

/// string operations

/// Constructs string literals
inline string operator "" _(const char* data, size_t size) { return string(data,size); }
/// Concatenate strings
string operator +(const string& a, const string& b);
/// Lexically compare strings
bool operator <(const string& a, const string& b);
/// Returns a null-terminated string
string strz(const string& s);
/// Returns a bounded reference to the null-terminated string pointer
string strz(const char* s);
/// Returns a copy of the string between the "start"th and "end"th occurence separator \a sep
/// \note you can use negative \a start, \a end to count from the right
/// \note this is a non-copying alternative to join(split(str,sep).slice(start,end),sep)
string section(const string& str, char sep, int start=0, int end=1);
/// Splits \a str wherever \a sep occurs
array<string> split(const string& str, char sep=' ');
/// Replaces every occurrence of the string \a before with the string \a after
string replace(const string& s, const string& before, const string& after);


/// Human-readable value representation

/// Base template for conversion to human-readable value representation
template<class A> string str(const A&) { static_assert(sizeof(A) && 0,"No string representation defined for type"); return ""; }

/// string representation of a string
inline string str(const string& s) { return copy(s); }

/// Concatenates string representation of its arguments
/// \note directly use operator+ to avoid spaces
template<class A, class... Args> string str(const A& a, const Args&... args) { return str(a)+" "_+str(args...); }

/// String representation of an array
template<class T> string str(const array<T>& a) {
    string s="["_;
    for(int i=0;i<a.size;i++) { s<<str(a[i]); if(i<a.size-1) s<<", "_; }
    return s+"]"_;
}

/// String representation of a boolean
template<> inline string str(const bool& b) { return b?"true"_:"false"_; }
/// String representation of an ASCII character
template<> inline string str(const char& c) { return string(&c,1); }

/// Converts a machine integer to its human-readable representation
string str(uint64 number, int base, int pad=0);
template<> inline string str(const uint& n) { return str(uint64(n),10); }
template<> inline string str(const int& n) { return n>=0?str(uint64(n),10):"-"_+str(uint64(-n),10); }
/// Converts a floating point number to its human-readable representation
string str(float number, int precision, int base=10);
template<> inline string str(const float& number) { return str(number,2); }

/// Enhanced debugging using str(...)
inline void write(int fd, const array<byte>& s) { write(fd,&s,(size_t)s.size); }
template<class... Args> void log(const Args&... args) { write(1,str(args...,"\n"_)); }
/// Display variable name and its value
#define var(v) ({debug( log(#v##_, v); )})
/// Aborts unconditionally and display \a message
#define error(message...) ({debug( trace_off; logTrace(); ) log("Error:\t"_,##message); abort(); })
/// Aborts if \a expr evaluates to false and display \a message (except stack trace)
#define assert(expr, message...) ({debug( if(!(expr)) { trace_off; logTrace(); log("Assert:\t"_,#expr##_, ##message); abort(); } )})


/// Number parsing

/// Parses an integer value
long toInteger(const string& str, int base=10 );
/// Parses an integer value and set \a s after it
long readInteger(const char*& s, int base=10);
/// Parses a decimal value
double toFloat(const string& str, int base=10 );
/// Parses a decimal value and set \a s after it
double readFloat(const char*& s, int base=10 );
