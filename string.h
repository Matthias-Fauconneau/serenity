#pragma once
/// \file string.h String manipulations (using lightweight ref<byte> when possible)
#include "array.h"

// Enforces exact match for overload resolution
template<class T> string str(const T&) { static_assert(0&&sizeof(T),"No overload for str(const T&)"); return string(); }

/// Lexically compare strings
bool operator <(const ref<byte>& a, const ref<byte>& b);

/// Returns a reference to the string between the {begin}th and {end}th occurence of \a separator
/// \note You can use a negative \a begin or \a end to count from the right (-1=last)
ref<byte> section(const ref<byte>& str, byte separator, int begin=0, int end=1);
/// Returns an array of references splitting \a str wherever \a separator occurs
array< ref<byte> > split(const ref<byte>& str, byte separator=' ');
/// Returns a reference with heading and trailing whitespace removed
ref<byte> trim(const ref<byte>& s);

/// Returns true if \a str starts with \a sub
bool startsWith(const ref<byte>& str, const ref<byte>& sub);
/// Returns true if \a str ends with \a sub
bool endsWith(const ref<byte>& str, const ref<byte>& sub);
/// Returns true if \a str contains the \a substring
bool find(const ref<byte>& str, const ref<byte>& substring);

/// Returns true if s contains only [0-9]
bool isInteger(const ref<byte>& s);
/// Parses an integer value
long toInteger(const ref<byte>& str, int base=10);

/// Forwards ref<byte>
inline const ref<byte>& str(const ref<byte>& s) { return s; }
/// Returns a bounded reference to the null-terminated string pointer
ref<byte> str(const char* s);
/// Returns boolean as "true"/"false"
inline ref<byte> str(const bool& b) { return b?"true"_:"false"_; }
/// Returns a reference to the character
inline ref<byte> str(const char& c) { return ref<byte>((byte*)&c,1); }

/// Joins \a list into a single string with each element separated by \a separator
string join(const ref<string>& list, const ref<byte>& separator);
/// Replaces every occurrence of the string \a before with the string \a after
string replace(const ref<byte>& s, const ref<byte>& before, const ref<byte>& after);
/// Lowers case
string toLower(const ref<byte>& s);
/// Removes duplicate whitespace
string simplify(string&& s);

struct stringz : string { operator const char*(){return (char*)data();}};
/// Copies the reference and appends a null byte
stringz strz(const ref<byte>& s);

/// Forwards string
inline const string& str(const string& s) { return s; }
/// Converts integers
template<int base=10> string utoa(uint64 number, int pad=0);
template<int base=10> string itoa(int64 number, int pad=0);
inline string bin(uint n, int pad=0) { return utoa<2>(n,pad); }
inline string dec(long n, int pad=0) { return itoa<10>(n,pad); }
inline string str(const uint8& n) { return dec(n); }
inline string str(const int8& n) { return dec(n); }
inline string str(const uint16& n) { return dec(n); }
inline string str(const int16& n) { return dec(n); }
inline string str(const uint32& n) { return dec(n); }
inline string str(const int32& n) { return dec(n); }
inline string str(const unsigned long& n) { return dec(n); }
inline string str(const long& n) { return dec(n); }
inline string str(const uint64& n) { return dec(n); }
inline string str(const int64& n) { return dec(n); }
inline string hex(uint64 n, int pad=0) { return utoa<16>(n,pad); }
template<class T> inline string str(T* const& p) { string s("0x"_); s<<hex(ptr(p)); return s; }

/// Converts floating-point numbers
string ftoa(double number, int precision=2);
inline string str(const float& n) { return ftoa(n); }
inline string str(const double& n) { return ftoa(n); }

/// Converts references
template<class T> string str(const unique<T>& t) { return str(*t.pointer); }

/// Converts arrays
template<class T> string str(const ref<T>& a, char separator=' ') { string s; for(uint i: range(a.size)) { s<<str(a[i]); if(i<a.size-1) s<<separator;} return s; }
template<class T> string str(const array<T>& a, char separator=' ') { return str(ref<T>(a),separator); }
template<class T> string dec(const ref<T>& a, char separator=' ') { string s; for(uint i: range(a.size)) { s<<dec(a[i]); if(i<a.size-1) s<<separator;} return s; }
template<class T> string dec(const array<T>& a, char separator=' ') { return dec(ref<T>(a),separator); }
template<class T> string hex(const ref<T>& a, char separator=' ') { string s; for(uint i: range(a.size)) { s<<hex(a[i]); if(i<a.size-1) s<<separator;} return s; }
template<class T> string hex(const array<T>& a, char separator=' ') { return hex(ref<T>(a),separator); }

/// Expression template to manage recursive concatenation operations
template<class A, class B> struct cat {
    const A& a;
    const B& b;
    uint size() const { return a.size() + b.size(); }
    void copy(byte*& data) const { a.copy(data); b.copy(data); }
    operator array<byte>()  const{ array<byte> r(size()); r.setSize(size()); byte* data=r.data(); copy(data); return r; }
};
template<class Aa, class Ab, class Ba, class Bb> cat< cat<Aa, Ab>, cat<Ba, Bb> >
operator +(const cat<Aa, Ab>& a, const cat<Ba, Bb>& b) { return __(a,b); }
/// Specialization to append a string
template<class A> struct cat<A, ref<byte> > {
    const A& a;
    const ref<byte>& b;
    uint size() const { return a.size() + b.size; }
    void copy(byte*& data) const { a.copy(data); ::copy(data,b.data,b.size); data+=b.size; }
    operator array<byte>()  const{ array<byte> r(size()); r.setSize(size()); byte* data=r.data(); copy(data); return r; }
};
template<class Aa, class Ab> cat< cat<Aa, Ab>, ref<byte> > operator +(const cat<Aa, Ab>& a, const ref<byte>& b) { return __(a,b); }
/// Specialization to concatenate two strings
template<> struct cat< ref<byte>, ref<byte> > {
    const ref<byte>& a;
    const ref<byte>& b;
    uint size() const { return a.size + b.size; }
    void copy(byte*& data) const { ::copy(data,a.data,a.size); data+=a.size; ::copy(data,b.data,b.size); data+=b.size; }
    operator array<byte>() const { array<byte> r(size()); r.setSize(size()); byte* data=r.data(); copy(data); return r; }
};
inline cat< ref<byte>, ref<byte> > operator +(const ref<byte>& a, const ref<byte>& b) { return __(a,b); }

/// Forwards concatenation
template<class A, class B> const cat<A,B>& str(const cat<A,B>& s) { return s; }
/// Converts and concatenates all arguments separating with spaces
/// \note Use str(a)+str(b)+... to convert and concatenate without spaces
template<class A, class ___ Args> string str(const A& a, const Args& ___ args) { return str(a)+" "_+str(args ___); }

/// Logs to standard output using str(...) serialization
template<class... Args> void log(const Args&... args) { log((ref<byte>)string(str(args ___))); }
/// Logs to standard output using str(...) serialization
template<> inline void log(const string& s) { log((ref<byte>)s); }
/// Logs to standard output using str(...) serialization and terminate all threads
template<class... Args> void __attribute((noreturn)) error(const Args&... args) { error((ref<byte>)string(str(args ___))); }
