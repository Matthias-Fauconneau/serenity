#pragma once
/// \file string.h String manipulations (using lightweight string when possible)
#include "array.h"

// Enforces exact match for overload resolution
generic String str(const T&) { static_assert(0&&sizeof(T),"No overload for str(const T&)"); return String(); }

/// Lexically compare strings
bool operator <(const string& a, const string& b);

/// Counts number of occurence of a character in a String
uint count(const string& a, byte c);

/// Returns a reference to the String between the {begin}th and {end}th occurence of \a separator
/// \note You can use a negative \a begin or \a end to count from the right (-1=last)
string section(const string& str, byte separator, int begin=0, int end=1);
/// Returns a reference with heading and trailing whitespace removed
string trim(const string& s);

/// Returns true if \a str starts with \a sub
bool startsWith(const string& str, const string& sub);
/// Returns true if \a str ends with \a sub
bool endsWith(const string& str, const string& sub);
/// Returns true if \a str contains the \a substring
bool find(const string& str, const string& substring);

/// Returns true if a string is a displayable unextended ASCII string
bool isASCII(const string& s);
/// Returns true if a string is a valid UTF8 string
bool isUTF8(const string& s);
/// Returns true if s contains only [0-9]
bool isInteger(const string& s);
/// Parses an integer value
int64 toInteger(const string& str, int base=10);
/// Returns true if s matches [0-9]*.?[0-9]*
bool isDecimal(const string& s);
/// Parses a decimal value
double toDecimal(const string& str);

/// Forwards string
inline const string& str(const string& s) { return s; }
/// Forwards mref<byte>
//inline const mref<byte>& str(const mref<byte>& s) { return s; }

/// Returns a bounded reference to the null-terminated String pointer
string str(const char* s);
/// Returns boolean as "true"/"false"
inline string str(const bool& b) { return b?"true"_:"false"_; }
/// Returns a reference to the character
inline string str(const char& c) { return string((byte*)&c,1); }

/// Forwards buffer<byte>
inline const buffer<byte>& str(const buffer<byte>& s) { return s; }

/// Joins \a list into a single String with each element separated by \a separator
String join(const ref<String>& list, const string& separator);
/// Replaces every occurrence of the String \a before with the String \a after
String replace(const string& s, const string& before, const string& after);
/// Lowers case
String toLower(const string& s);
/// Removes duplicate whitespace
String simplify(String&& s);
/// Repeats a String
String repeat(const string& s, uint times);

/// Forwards String
inline const String& str(const String& s) { return s; }

struct stringz : String { operator const char*(){ return data; }};
/// Copies the reference and appends a null byte
stringz strz(const string& s);

/// Returns an array of references splitting \a str wherever \a separator occurs
array<string> split(const string& str, byte separator=' ');

/// Converts integers
template<uint base=10> String utoa(uint64 number, int pad=0);
template<uint base=10> String itoa(int64 number, int pad=0);
inline String bin(uint64 n, int pad=0) { return utoa<2>(n,pad); }
inline String dec(int64 n, int pad=0) { return itoa<10>(n,pad); }
inline String str(const uint8& n) { return dec(n); }
inline String str(const int8& n) { return dec(n); }
inline String str(const uint16& n) { return dec(n); }
inline String str(const int16& n) { return dec(n); }
inline String str(const uint32& n) { return dec(n); }
inline String str(const int32& n) { return dec(n); }
inline String str(const unsigned long& n) { return dec(n); }
inline String str(const long& n) { return dec(n); }
inline String str(const uint64& n) { return dec(n); }
inline String str(const int64& n) { return dec(n); }
inline String hex(uint64 n, int pad=0) { return utoa<16>(n,pad); }
inline String str(void* const& p) { return "0x"_+hex(ptr(p)); }
generic inline String str(T* const& p) { return str(*p); }
generic String str(const unique<T>& t) { return str(*t.pointer); }
generic String str(const shared<T>& t) { return str(*t.pointer); }

/// Converts floating-point numbers
String ftoa(double number, int precision=2, int pad=0, bool exponent=false, bool inf=true);
inline String str(const float& n) { return ftoa(n); }
inline String str(const double& n) { return ftoa(n); }

/// Converts arrays
generic String str(const ref<T>& a) { String s; for(uint i: range(a.size)) { s<<str(a[i]); if(i<a.size-1) s<<' ';} return s; }
generic String str(const mref<T>& a) { return str((const ref<T>&)a); }
generic String str(const buffer<T>& a) { return str((const ref<T>&)a); }
generic String str(const array<T>& a, char separator=' ') { String s; for(uint i: range(a.size)) { s<<str(a[i]); if(i<a.size-1) s<<separator;} return s; }
generic String dec(const ref<T>& a, char separator=' ') { String s; for(uint i: range(a.size)) { s<<dec(a[i]); if(i<a.size-1) s<<separator;} return s; }
generic String hex(const ref<T>& a, char separator=' ') { String s; for(uint i: range(a.size)) { s<<hex(a[i],2); if(i<a.size-1) s<<separator;} return s; }

/// Converts static arrays
template<Type T, size_t N> String str(const T (&a)[N]) { return str(ref<T>(a,N)); }

/// Converts and concatenates all arguments separating with spaces
/// \note Use str(a)+str(b)+... to convert and concatenate without spaces
template<Type A, Type... Args> String str(const A& a, const Args&... args) { return str(a)+" "_+str(args...); }

/// Logs to standard output using str(...) serialization
template<Type... Args> void log(const Args&... args) { log<string>(str(args...)); }
/// Logs to standard output using str(...) serialization and terminate all threads
template<Type... Args> void __attribute((noreturn)) error(const Args&... args) { error<string>(str(args...)); }
