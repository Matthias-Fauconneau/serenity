#pragma once
/// \file string.h String manipulations (using lightweight ref<byte> when possible)
#include "array.h"

// Enforces exact match for overload resolution
template<Type T> string str(const T&) { static_assert(0&&sizeof(T),"No overload for str(const T&)"); return string(); }

/// Lexically compare strings
bool operator <(const ref<byte>& a, const ref<byte>& b);

/// Returns a reference to the string between the {begin}th and {end}th occurence of \a separator
/// \note You can use a negative \a begin or \a end to count from the right (-1=last)
ref<byte> section(const ref<byte>& str, byte separator, int begin=0, int end=1);
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
int64 toInteger(const ref<byte>& str, int base=10);
/// Returns true if s matches [0-9]*.?[0-9]*
bool isDecimal(const ref<byte>& s);
/// Parses a decimal value
double toDecimal(const ref<byte>& str);

/// Forwards ref<byte>
inline const ref<byte>& str(const ref<byte>& s) { return s; }
/// Forwards mref<byte>
inline const mref<byte>& str(const mref<byte>& s) { return s; }

/// Returns a bounded reference to the null-terminated string pointer
ref<byte> str(const char* s);
/// Returns boolean as "true"/"false"
inline ref<byte> str(const bool& b) { return b?"true"_:"false"_; }
/// Returns a reference to the character
inline ref<byte> str(const char& c) { return ref<byte>((byte*)&c,1); }

/// Forwards buffer<byte>
inline const buffer<byte>& str(const buffer<byte>& s) { return s; }

/// Joins \a list into a single string with each element separated by \a separator
string join(const ref<string>& list, const ref<byte>& separator);
/// Replaces every occurrence of the string \a before with the string \a after
string replace(const ref<byte>& s, const ref<byte>& before, const ref<byte>& after);
/// Lowers case
string toLower(const ref<byte>& s);
/// Removes duplicate whitespace
string simplify(string&& s);
/// Repeats a string
string repeat(const ref<byte>& s, uint times);

/// Forwards string
inline const string& str(const string& s) { return s; }

struct stringz : string { operator const char*(){ return data; }};
/// Copies the reference and appends a null byte
stringz strz(const ref<byte>& s);

/// Returns an array of references splitting \a str wherever \a separator occurs
array<ref<byte>> split(const ref<byte>& str, byte separator=' ');

/// Converts integers
template<uint base=10> string utoa(uint64 number, int pad=0);
template<uint base=10> string itoa(int64 number, int pad=0);
inline string bin(uint64 n, int pad=0) { return utoa<2>(n,pad); }
inline string dec(int64 n, int pad=0) { return itoa<10>(n,pad); }
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
//template<Type T> inline string str(T* const& p) { string s("0x"_); s<<hex(ptr(p)); return s; }
inline string str(void* const& p) { return "0x"_+hex(ptr(p)); }
template<Type T> inline string str(T* const& p) { return str(*p); }
template<Type T> string str(const unique<T>& t) { return str(*t.pointer); }
template<Type T> string str(const shared<T>& t) { return str(*t.pointer); }

/// Converts floating-point numbers
string ftoa(double number, int precision=2, int pad=0, bool exponent=false);
inline string str(const float& n) { return ftoa(n); }
inline string str(const double& n) { return ftoa(n); }

/// Converts arrays
template<Type T> string str(const ref<T>& a) { string s; for(uint i: range(a.size)) { s<<str(a[i]); if(i<a.size-1) s<<' ';} return s; }
template<Type T> string str(const mref<T>& a) { return str((const ref<T>&)a); }
template<Type T> string str(const buffer<T>& a) { return str((const ref<T>&)a); }
template<Type T> string str(const array<T>& a) { return str((const ref<T>&)a); }
template<Type T> string dec(const ref<T>& a, char separator=' ') { string s; for(uint i: range(a.size)) { s<<dec(a[i]); if(i<a.size-1) s<<separator;} return s; }
template<Type T> string hex(const ref<T>& a, char separator=' ') { string s; for(uint i: range(a.size)) { s<<hex(a[i],2); if(i<a.size-1) s<<separator;} return s; }

/// Converts static arrays
template<Type T, size_t N> string str(const T (&a)[N]) { return str(ref<T>(a,N)); }

/// Converts and concatenates all arguments separating with spaces
/// \note Use str(a)+str(b)+... to convert and concatenate without spaces
template<Type A, Type... Args> string str(const A& a, const Args&... args) { return str(a)+" "_+str(args...); }

/// Logs to standard output using str(...) serialization
template<Type... Args> void log(const Args&... args) { log<ref<byte>>(str(args...)); }
/// Logs to standard output using str(...) serialization and terminate all threads
template<Type... Args> void __attribute((noreturn)) error(const Args&... args) { error<ref<byte>>(str(args...)); }
