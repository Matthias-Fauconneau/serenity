#pragma once
#include "array.h"

/// utf8_iterator is used to iterate UTF-8 encoded strings
struct utf8_iterator {
    const char* pointer;
    utf8_iterator(const char* pointer):pointer(pointer){}
    bool operator!=(const utf8_iterator& o) const { return o.pointer != pointer; }
    bool operator==(const utf8_iterator& o) const { return o.pointer == pointer; }
    uint operator* () const;
    const utf8_iterator& operator++();
    const utf8_iterator& operator--();
};

/// \a string is an \a array of characters with specialized methods for UTF-8 string handling
struct string : array<char> {
    //using array<char>::array<char>;
    string() {}
    explicit string(uint capacity):array<char>(capacity){}
    string(array<char>&& o):array<char>(move(o)){}
    string(const char* data, uint size):array<char>(data,size){}
    string(const char* begin,const char* end):array<char>(begin,end){}
    string(utf8_iterator begin,utf8_iterator end):array<char>(begin.pointer,end.pointer){}

    uint at(uint index) const { utf8_iterator it=begin(); for(uint i=0;it!=end();++it,++i) if(i==index) return *it; fail(); }
    uint operator [](uint i) const { return at(i); }

    const utf8_iterator begin() const { return array::begin(); }
    const utf8_iterator end() const { return array::end(); }
};
template<> inline string copy(const string& s) { return copy<char>(s); }

/// Expression template to hold recursive concatenation operations
template<class A> struct cat {
    const A& a;
    const string& b;
    uint size() const { return a.size() + b.size(); }
    void copy(char*& data) const { a.copy(data); ::copy(data,b.data(),b.size()); data+=b.size(); }
    operator string()  const{ string r(size()); r.setSize(size()); char* data=r.data(); copy(data); return r; }
};
/// Specialization to concatenate strings
template<> struct cat<string> {
    const string& a;
    const string& b;
    uint size() const { return a.size() + b.size(); }
    void copy(char*& data) const { ::copy(data,a.data(),a.size()); data+=a.size(); ::copy(data,b.data(),b.size()); data+=b.size(); }
    operator string() const { string r(size()); r.setSize(size()); char* data=r.data(); copy(data); return r; }
};
template<class A> inline cat< cat<A> > operator +(const cat<A>& a, const string& b) { return i({a,b}); }
inline cat<string> operator +(const string& a, const string& b) { return i({a,b}); }

/// Constructs string literals
inline string operator "" _(const char* data, size_t size) { return string(data,size); }

/// Lexically compare strings
bool operator <(const string& a, const string& b);

/// Returns true if \a str starts with \a sub
bool startsWith(const array<byte>& str, const array<byte>& sub);
/// Returns true if \a str ends with \a sub
bool endsWith(const array<byte>& str, const array<byte>& sub);
/// Returns true if \a str contains \a sub
bool contains(const string& str, const string& sub);

/// Returns a null-terminated string
string strz(const string& s);
/// Returns a bounded reference to the null-terminated string pointer
string strz(const char* s);

/// Returns a copy of the string between the \a{start}th and \a{end}th occurence of \a separator
/// \note You can use a negative \a start or \a end to count from the right
/// \note This is a shortcut to join(split(str,sep).slice(start,end),sep)
string section(const string& str, uint separator, int start=0, int end=1, bool includeSeparator=false);

/// Splits \a str wherever \a separator occurs
array<string> split(const string& str, uint separator=' ');

/// Joins \a list into a single string with each element separated by \a separator
string join(const array<string>& list, const string& separator);

/// Replaces every occurrence of the string \a before with the string \a after
array<char> replace(const array<char> &s, const array<char> &before, const array<char> &after);

/// Lowers case
string toLower(const string& s);

/// Removes heading, trailing whitespace
string trim(const array<byte>& s);
/// Removes duplicate whitespace
string simplify(const array<byte>& s);

/// Convert Unicode code point to UTF-8
string utf8(uint code);

/// Human-readable value representation

/// Base template for conversion to human-readable value representation
template<class A> string str(const A&) { static_assert(sizeof(A) & 0,"No string representation defined for type"); return ""_; }

/// String representation of a boolean
template<> inline string str(const bool& b) { return b?"true"_:"false"_; }
/// String representation of an ASCII character
template<> inline string str(const char& c) { return string(&c,1); }
/// String representation of a C string literal
inline string str(const char* s) { int l=0; for(;s[l];l++){} return string(s,l); }
inline string str(char* s) { int l=0; for(;s[l];l++){} return string(s,l); }

/// Converts a machine integer to its human-readable representation
string itoa(int64 number, int base=10, int pad=0);
inline string hex(int64 n, int pad=0) { return itoa(n,16,pad); }
inline string dec(int64 n, int pad=0) { return itoa(n,10,pad); }
inline string oct(int64 n, int pad=0) { return itoa(n,8,pad); }
inline string bin(uint64 n, int pad=0) { return itoa(n,2,pad); }

template<> inline string str(void* const& n) { return hex(int64(n)); }
template<> inline string str(const uint64& n) { return dec(n); }
template<> inline string str(const int64& n) { return dec(n); }
template<> inline string str(const unsigned long& n) { return dec(n); }
template<> inline string str(const long& n) { return dec(n); }
template<> inline string str(const uint32& n) { return dec(n); }
template<> inline string str(const int32& n) { return dec(n); }
template<> inline string str(const uint16& n) { return dec(n); }
template<> inline string str(const int16& n) { return dec(n); }
template<> inline string str(const uint8& n) { return dec(n); }
template<> inline string str(const int8& n) { return dec(n); }

/// Converts a floating point number to its human-readable representation
string ftoa(float number, int precision, int base=10);
template<> inline string str(const float& number) { return ftoa(number,2); }

/// Concatenates string representation of its arguments
/// \note directly use operator+ to avoid spaces
template<class A, class... Args, predicate(!is_convertible(A,string))> string str(const A& a, const Args&... args) { return str(a)+" "_+str(args...); }
template<class... Args> string str(const string& s, const Args&... args) { return s+" "_+str(args...); }
template<class A, predicate(!is_convertible(A,string))> string str(const A& a, const string& s) { return str(a)+" "_+s; }
inline string str(const string& a, const string& b) { return a+" "_+b; }

/// String representation of a cat (force conversion to string)
template<class A> string str(const cat<A>& s) { return s; }

/// String representation of a pointer
template<class A> string str(A* const& s) { return s?"null"_:str(*s); }

/// String representation of an array
template<class T> string str(const array<T>& list) { return str(apply<string>(list,[](const T& t){return str(t);})); }
template<> inline string str(const array<string>& list) { return "["_+join(list,", "_)+"]"_; }

bool isInteger(const string& s);
/// Parses an integer value
long toInteger(const string& str, int base=10 );
/// Parses a decimal value
double toFloat(const string& str, int base=10 );
