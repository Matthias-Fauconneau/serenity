#pragma once
#include "array.h"

typedef array<byte> string;

/// Expression template to hold recursive concatenation operations
template<class A> struct cat {
    const A& a;
    const ref<byte>& b;
    uint size() const { return a.size() + b.size; }
    void copy(byte*& data) const { a.copy(data); ::copy(data,b.data,b.size); data+=b.size; }
    operator array<byte>()  const{ array<byte> r(size()); r.setSize(size()); byte* data=r.data(); copy(data); return r; }
};
/// Specialization to concatenate two strings
template<> struct cat< ref<byte> > {
    const ref<byte>& a;
    const ref<byte>& b;
    uint size() const { return a.size + b.size; }
    void copy(byte*& data) const { ::copy(data,a.data,a.size); data+=a.size; ::copy(data,b.data,b.size); data+=b.size; }
    operator array<byte>() const { array<byte> r(size()); r.setSize(size()); byte* data=r.data(); copy(data); return r; }
};
template<class A> inline cat< cat<A> > operator +(const cat<A>& a, const ref<byte>& b) { return i({a,b}); }
inline cat< ref<byte> > operator +(const ref<byte>& a, const ref<byte>& b) { return i({a,b}); }

/// Constructs string literals
inline constexpr ref<byte> operator "" _(const char* data, size_t size) { return ref<byte>((byte*)data,size); }

/// Lexically compare strings
bool operator >(const ref<byte>& a, const ref<byte>& b);

/// Returns true if \a str starts with \a sub
bool startsWith(const ref<byte>& str, const ref<byte>& sub);
/// Returns true if \a str ends with \a sub
bool endsWith(const ref<byte>& str, const ref<byte>& sub);
/// Returns true if \a str contains \a c
//inline bool contains(const ref<byte>& str, char c) { return contains(str, c); }
/// Returns true if \a str contains \a sub
bool find(const ref<byte>& str, const ref<byte>& sub);

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
CString strz(const ref<byte>& s);

/// Returns a copy of the string between the \a{start}th and \a{end}th occurence of \a separator
/// \note You can use a negative \a start or \a end to count from the right
/// \note This is a shortcut to join(split(str,sep).slice(start,end),sep)
ref<byte> section(const ref<byte>& str, byte separator, int start=0, int end=1, bool includeSeparator=false);

/// Splits \a str wherever \a separator occurs
array<string> split(const ref<byte>& str, byte separator=' ');

/// Joins \a list into a single string with each element separated by \a separator
string join(const array<string>& list, const ref<byte>& separator);

/// Replaces every occurrence of the string \a before with the string \a after
array<byte> replace(const ref<byte>& s, const ref<byte>& before, const ref<byte>& after);

/// Lowers case
string toLower(const ref<byte>& s);

/// Removes heading, trailing whitespace
ref<byte> trim(const ref<byte>& s);
/// Removes duplicate whitespace
string simplify(const ref<byte>& s);

/// Converts a machine integer to its human-readable representation
template<int base=10> string utoa(uint number, int pad=0);
template<int base=10> string itoa(int number, int pad=0);
inline string bin(int n, int pad=0) { return itoa<2>(n,pad); }
inline string oct(int n, int pad=0) { return itoa<8>(n,pad); }
inline string dec(int n, int pad=0) { return itoa<10>(n,pad); }
inline string hex(uint n, int pad=0) { return utoa<16>(n,pad); }

/// Converts a floating point number to its human-readable representation
//string ftoa(float number, int precision, int base=10);

bool isInteger(const ref<byte> &s);
/// Parses an integer value
long toInteger(const ref<byte>& str, int base=10 );
/// Parses a decimal value
//float toFloat(const ref<byte>& str, int base=10 );

/// Base template for conversion to human-readable value representation
template<class A> string str(const A&) { static_assert(sizeof(A) & 0,"No string representation defined for type"); return string(); }

/// Returns a bounded reference to the null-terminated string pointer
string str(const char* s);

inline string str(const bool& b) { return string(b?"true"_:"false"_); }
inline string str(const char& c) { return copy(string((byte*)&c,1)); }
inline string str(const uint8& n) { return dec(n); }
inline string str(const int8& n) { return dec(n); }
inline string str(const uint16& n) { return dec(n); }
inline string str(const int16& n) { return dec(n); }
inline string str(const uint32& n) { return dec(n); }
inline string str(const int32& n) { return dec(n); }
inline string str(const ulong& n) { return dec(n); }
inline string str(const long& n) { return dec(n); }
//inline string str(const float& n) { return ftoa(n,2); }
inline const string& str(const string& s) { return s; }
inline string str(const ref<byte>& s) { return string(s); }
template<class A> inline string str(const cat<A>& s) { return s; }
//template<class A> inline string str(A* const& p) { return p?utoa<16>(uint(p)):string("null"_); }
template<class A> inline string str(A* const& p) { return p?str(*p):string("null"_); }
template<class T> inline string str(const ref<T>& a, const ref<byte>& sep=" "_, const ref<byte>& bracket=""_) {
    if(!a) return string();
    string s; if(bracket) s<<bracket[0]; for(uint i=0;i<a.size;i++) { s<<str(a[i]); if(i<a.size-1) s<<sep;} if(bracket) s<<bracket[1]; return s;
}
template<class T> inline string str(const array<T>& a, const ref<byte>& sep=" "_, const ref<byte>& bracket=""_) {
    return str(ref<T>(a),sep,bracket);
}

/// Concatenates string representation of its arguments
/// \note directly use operator+ to avoid spaces
template<class A, class ___ Args> inline string str(const A& a, const Args& ___ args) { return str(a)+" "_+str(args ___); }
