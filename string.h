#pragma once
#include "array.h"

/// utf8_iterator is used to iterate UTF-8 encoded strings
struct utf8_iterator {
    const byte* pointer;
    utf8_iterator(const byte* pointer):pointer(pointer){}
    bool operator!=(const utf8_iterator& o) const { return o.pointer != pointer; }
    bool operator==(const utf8_iterator& o) const { return o.pointer == pointer; }
    uint operator* () const;
    const utf8_iterator& operator++();
    const utf8_iterator& operator--();
};

/// \a string is an \a array of characters with specialized methods for UTF-8 string handling
struct string : array<byte> {
    //using array<byte>::array<byte>;
    string() {}
    explicit string(uint capacity):array<byte>(capacity){}
    string(array<byte>&& o):array<byte>(move(o)){}
    string(const byte* data, uint size):array<byte>(data,size){}
    string(const byte* begin,const byte* end):array<byte>(begin,end){}
    string(utf8_iterator begin,utf8_iterator end):array<byte>(begin.pointer,end.pointer){}
    const utf8_iterator begin() const { return array::begin(); }
    const utf8_iterator end() const { return array::end(); }
    uint at(uint index) const;
    uint operator [](uint i) const { return at(i); }
};
template<> inline string copy(const string& s) { return copy<byte>(s); }

/// Expression template to hold recursive concatenation operations
template<class A> struct cat {
    const A& a;
    const string& b;
    uint size() const { return a.size() + b.size(); }
    void copy(byte*& data) const { a.copy(data); ::copy(data,b.data(),b.size()); data+=b.size(); }
    operator string()  const{ string r(size()); r.setSize(size()); byte* data=r.data(); copy(data); return r; }
};
/// Specialization to concatenate strings
template<> struct cat<string> {
    const string& a;
    const string& b;
    uint size() const { return a.size() + b.size(); }
    void copy(byte*& data) const { ::copy(data,a.data(),a.size()); data+=a.size(); ::copy(data,b.data(),b.size()); data+=b.size(); }
    operator string() const { string r(size()); r.setSize(size()); byte* data=r.data(); copy(data); return r; }
};
template<class A> inline cat< cat<A> > operator +(const cat<A>& a, const string& b) { return i({a,b}); }
inline cat<string> operator +(const string& a, const string& b) { return i({a,b}); }

/// Constructs string literals
inline string operator "" _(const char* data, size_t size) { return string((byte*)data,size); }

/// Lexically compare strings
bool operator >(const array<byte>& a, const array<byte>& b);

/// Returns true if \a str starts with \a sub
bool startsWith(const array<byte>& str, const array<byte>& sub);
/// Returns true if \a str ends with \a sub
bool endsWith(const array<byte>& str, const array<byte>& sub);
/// Returns true if \a str contains \a c
inline bool contains(const string& str, char c) { return contains<byte>(str, c); }
/// Returns true if \a str contains \a sub
bool contains(const string& str, const string& sub);

struct CString {
    no_copy(CString)
    static bool busy;
    char* data;
    int tag; //0=inline, 1=static
    CString(){}
    CString(CString&& o):data(o.data),tag(o.tag) {o.tag=0;}
    ~CString() { if(tag==1) busy=0; }
    operator const char*() { return data; }
};
/// Returns a null-terminated string
CString strz(const string& s);
/// Returns a copy of the null-terminated string pointer
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
array<byte> replace(const array<byte> &s, const array<byte> &before, const array<byte> &after);

/// Lowers case
string toLower(const string& s);

/// Removes heading, trailing whitespace
string trim(const array<byte>& s);
/// Removes duplicate whitespace
string simplify(const array<byte>& s);

/// Convert Unicode code point to UTF-8
string utf8(uint code);

/// Converts a machine integer to its human-readable representation
template<int base=10> string utoa(uint number, int pad=0);
template<int base=10> string itoa(int number, int pad=0);
inline string bin(int n, int pad=0) { return itoa<2>(n,pad); }
inline string oct(int n, int pad=0) { return itoa<8>(n,pad); }
inline string dec(int n, int pad=0) { return itoa<10>(n,pad); }
inline string hex(uint n, int pad=0) { return utoa<16>(n,pad); }

/// Converts a floating point number to its human-readable representation
string ftoa(float number, int precision, int base=10);

bool isInteger(const string& s);
/// Parses an integer value
long toInteger(const string& str, int base=10 );
/// Parses a decimal value
double toFloat(const string& str, int base=10 );

/// Base template for conversion to human-readable value representation
template<class A> string str(const A&) { static_assert(sizeof(A) & 0,"No string representation defined for type"); return ""_; }

/// Returns a bounded reference to the null-terminated string pointer
string str(const char* s);

inline string str(const bool& b) { return b?"true"_:"false"_; }
inline string str(const uint8& n) { return dec(n); }
inline string str(const int8& n) { return dec(n); }
inline string str(const uint16& n) { return dec(n); }
inline string str(const int16& n) { return dec(n); }
inline string str(const uint32& n) { return dec(n); }
inline string str(const int32& n) { return dec(n); }
inline string str(const ulong& n) { return dec(n); }
inline string str(const long& n) { return dec(n); }
inline string str(const float& n) { return ftoa(n,2); }
inline const string& str(const string& s) { return s; }
template<class A> inline string str(const cat<A>& s) { return s; }
template<class A> inline string str(A* const& p) { return p?utoa<16>(uint(p)):"null"_; }
template<class T> inline string str(const array<T>& list) { string r; for(const T& e: list) r << str(e); return r; }

/// Concatenates string representation of its arguments
/// \note directly use operator+ to avoid spaces
template<class A, class ___ Args> inline string str(const A& a, const Args& ___ args) { return str(a)+" "_+str(args ___); }
