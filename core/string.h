#pragma once
/// \file string.h String manipulations (using lightweight string when possible)
#include "array.h"
#include "cat.h"

// -- String

/// Returns const reference to a static string literal
inline String operator "" __(const char* data, size_t size) { return String((char*)data, size, 0); }

// -- str()

// Enforces exact match for overload resolution
generic String str(const T&) { static_assert(0&&sizeof(T),"No overload for str(const T&)"); error(); }

/// Forwards string
inline String str(string s) { return unsafeRef(s); }
/// Forwards char[]
template<size_t N> string str(const char (&source)[N]) { return string(source); }

/// Returns boolean as "true"/"false"
inline string str(bool value) { return value ? "true"_ : "false"_; }
/// Returns a reference to the character
inline string str(const char& character) { return string((char*)&character,1); }

/// Returns a bounded reference to the null-terminated String pointer
string str(const char* source);

// -- string

/// Lexically compare strings
bool operator <(const string a, const string b);
bool operator <=(const string a, const string b);

/// Returns whether \a a starts with \a b
generic bool startsWith(const ref<T> a, const ref<T> b) {
	return a.size>=b.size && a.slice(0, b.size)==b;
}
inline bool startsWith(const string a, const string b) { return startsWith<char>(a, b); }

/// Returns whether \a str ends with \a sub
bool endsWith(const string str, const string sub);
/// Returns whether \a str contains the \a substring
bool find(const string str, const string substring);

/// Returns a reference to the String between the {begin}th and {end}th occurence of \a separator
/// \note You can use a negative \a begin or \a end to count from the right (-1=last)
string section(const string str, byte separator, int begin=0, int end=1);

/// Returns a reference with heading and trailing whitespace removed
string trim(const string s);

/// Returns whether s contains only [0-9]
bool isInteger(const string s);
/// Returns true if s matches [0-9]*.?[0-9]*
bool isDecimal(const string s);
/// Parses an integer value
int64 parseInteger(const string str, int base=10);
/// Parses a decimal value
double parseDecimal(const string str);

// -- String

/// Forwards string
inline string str(const String& s) { return s; }

/// Null-terminated \a String with implicit conversion to const char*
struct strz : buffer<char> {
    /// Copies a string reference, appends a null byte and allows implicit conversion to const char*
    strz(const string s) : buffer<char>(s.size+1) { slice(0, s.size).copy(s); last()='\0'; }
    operator const char*() { return data; }
};

/// Lowers case
char lowerCase(char c);
/// Lowers case
String toLower(string s);
/// Uppers case
String toUpper(string s);

String repeat(string s, uint times);

/// Pads a string to the left
String left(string s, size_t size, const char pad=' ');
/// Pads a string to the right
String right(string s, size_t size, const char pad=' ');

/// Replaces every occurrence of \a before with \a after
String replace(string s, string before, string after);

/// Removes duplicate whitespace
String simplify(array<char>&& s);
//inline String simplify(string s) { return simplify(array<char>(copyRef(s))); }

// -- string[]

/// Joins \a list into a single String with each element separated by \a separator
//String join(ref<string> list, const string separator=""_);

/// Returns an array of references splitting \a str wherever \a separator occurs
buffer<string> split(const string str, string separator/*=", "_*/);

/// Flattens cats
template<Type A, Type B, Type T> String str(const cat<A, B, T>& a) { return a; }

// -- Number conversions

/// Converts an unsigned integer
String str(uint64 number, uint pad=0, char padChar='0', uint base=10);
/// Converts an unsigned integer (implicit conversion)
inline String str(uint8 number, uint pad=0, char padChar='0', uint base=10) { return str(uint64(number), pad, padChar, base); }
/// Converts an unsigned integer (implicit conversion)
inline String str(uint16 number, uint pad=0, char padChar='0', uint base=10) { return str(uint64(number), pad, padChar, base); }
/// Converts an unsigned integer (implicit conversion)
inline String str(uint32 number, uint pad=0, char padChar='0', uint base=10) { return str(uint64(number), pad, padChar, base); }
#if __x86_64__
/// Converts an unsigned integer (implicit conversion)
inline String str(size_t number, uint pad=0, char padChar='0', uint base=10) { return str(uint64(number), pad, padChar, base); }
#endif
/// Converts an unsigned integer in hexadecimal base
inline String hex(uint64 n, uint pad=0) { return str(n, pad, '0', 16); }
/// Converts a memory address in hexadecimal base
generic inline String str(T* const& p) { return "0x"+hex(ptr(p)); }

/// Converts a signed integer
String str(int64 number, uint pad=0, char padChar=' ', uint base=10);
/// Converts a signed integer (implicit conversion)
inline String str(int16 n, uint pad=0, char padChar=' ', uint base=10) { return str(int64(n), pad, padChar, base); }
/// Converts a signed integer (implicit conversion)
inline String str(int32 n, uint pad=0, char padChar=' ', uint base=10) { return str(int64(n), pad, padChar, base); }
/// Converts a signed integer (implicit conversion)
inline String str(long n, uint pad=0, char padChar=' ', uint base=10) { return str(int64(n), pad, padChar, base); }

/// Converts a floating-point number
//String str(double number, uint precision=6, uint exponent=0, uint pad=0);
//inline String str(float n, uint precision=6, uint exponent=0) { return str(double(n), precision, exponent); }

String str(float number, uint precision=4, uint exponent=0, uint pad=0);
//inline String str(double n, uint precision=1, uint exponent=0) { return str(float(n), precision, exponent); }

/*/// Formats value using best binary prefix
String binaryPrefix(uint64 value, string unit="B"_, string unitSuffix=""_);
/// Formats value using best decimal prefix
String decimalPrefix(double value, string unit=""_, string unitSuffix=""_);*/

/// Converts arrays
generic String str(const ref<T> source, string separator=" "_, string bracket="[]"_) {
	array<char> target;
 if(bracket) target.append(bracket[0]);
 for(uint i: range(source.size)) {
  target.append( str(source[i]) );
  if(i<source.size-1) target.append(separator);
 }
 if(bracket) target.append(bracket[1]);
 return move(target);
}
generic String str(const mref<T>& source, string separator=" "_, string bracket="[]"_) { return str((const ref<T>)source, separator, bracket); }
generic String str(const buffer<T>& source, string separator=" "_, string bracket="[]"_) { return str((const ref<T>)source, separator, bracket); }
generic String str(const array<T>& source, string separator=" "_, string bracket="[]"_) { return str((const ref<T>)source, separator, bracket); }
inline String str(const array<char>& a) { return str((string)a); }
inline String str(const ref<char>& a, string b) { return str(a)+" "+str(b); }
inline String str(const ref<char>& a, string b, string c) { return str(a)+" "+str(b)+" "+str(c); } //?
inline String bin(const ref<uint8> source, string separator=" "_) { return str(apply(source, [](uint64 c) { return str(c, 8u, '0', 2u); }), separator); }
inline String hex(const ref<int32> source, string separator=" "_) { return str(apply(source, [](const int32& c) { return hex(c, 8); }), separator); }
inline String hex(const ref<uint8> source, string separator=" "_) { return str(apply(source, [](const uint8& c) { return hex(c, 2); }), separator); }
inline String hex(const ref<byte> source, string separator=" "_) { return hex(cast<uint8>(source), separator); }

/// Converts static arrays
template<Type T, size_t N> String str(const T (&source)[N], string separator=" ") { return str(ref<T>(source, N), separator); }

/// Converts and concatenates all arguments separating with spaces
/// \note Use join({str(args)...}) to convert and concatenate without spaces
template<Type Arg0, Type Arg1, Type... Args>
String str(const Arg0& arg0, const Arg1& arg1, const Args&... args) { return join(ref<string>{str(arg0), str(arg1), str(args)...}, " "_); }

/// Logs to standard output using str(...) serialization
template<Type... Args> void log(const Args&... args) { log((string)join(ref<string>{str(args)...}, " "_)); }
/// Logs to standard output using str(...) serialization and terminate all threads
template<Type... Args> __attribute((noreturn)) void error(const Args&... args) { error<string>(str(args...)); }

/// Converts Strings to strings
inline buffer<string> toRefs(ref<String> source) { return apply(source, [](const String& e) -> string { return e; }); }
inline String join(const ref<String> list, string separator=""_) { return join(toRefs(list), separator); }

/// Forwards handle
generic auto str(const handle<T>& t) -> decltype(str(t.pointer)) { return str(t.pointer); }
/// Forwards unique
generic auto str(const unique<T>& t) -> decltype(str(*t.pointer)) { return str(*t.pointer); }
/// Forwards shared
generic auto str(const shared<T>& t) -> decltype(str(*t.pointer)) { return str(*t.pointer); }
