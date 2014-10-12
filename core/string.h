#pragma once
/// \file string.h String manipulations (using lightweight string when possible)
#include "array.h"

// -- str()

// Enforces exact match for overload resolution
generic string str(const T&) { static_assert(0&&sizeof(T),"No overload for str(const T&)"); return {}; }

/// Forwards string
inline string str(string s) { return s; }
/// Forwards String
inline string str(const String& s) { return s; }
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

/// Returns true if \a str starts with \a sub
bool startsWith(const string str, const string sub);
/// Returns true if \a str ends with \a sub
bool endsWith(const string str, const string sub);
/// Returns true if \a str contains the \a substring
bool find(const string str, const string substring);

/// Returns a reference to the String between the {begin}th and {end}th occurence of \a separator
/// \note You can use a negative \a begin or \a end to count from the right (-1=last)
string section(const string str, byte separator, int begin=0, int end=1);

/// Returns true if s contains only [0-9]
bool isInteger(const string s);
/// Parses an integer value
int64 fromInteger(const string str, int base=10);
/// Parses a decimal value
double fromDecimal(const string str);

// -- strz

/// Copies the reference, appends a null byte and allows implicit conversion to const char*
struct strz : String {
    strz(const string s) : String(s+'\0') {}
    operator const char*() { return data; }
};

// -- String

/// Replaces every occurrence of the String \a before with the String \a after
String replace(const string s, const string before, const string after);
/// Lowers case
char toLower(char c);
/// Lowers case
String toLower(const string s);
/// Uppers case
String toUpper(const string s);

/// Pads a string to the left
String left(const string s, size_t size, const char pad=' ');
/// Pads a string to the right
String right(const string s, size_t size, const char pad=' ');

// -- string[]

/// Joins \a list into a single String with each element separated by \a separator
String join(const ref<string> list, const string separator="");

/// Returns an array of references splitting \a str wherever \a separator occurs
array<string> split(const string str, string separator=", ");

// -- Number conversions

String itoa(int64 number, uint base=10, int pad=0, char padChar=' ');
inline String dec(int64 n, int pad=0, char padChar=' ') { return itoa(n,10,pad,padChar); }
inline String str(uint8 n) { return dec(n); }
inline String str(int8 n) { return dec(n); }
inline String str(uint16 n) { return dec(n); }
inline String str(int16 n) { return dec(n); }
inline String str(uint32 n) { return dec(n); }
inline String str(int32 n) { return dec(n); }
inline String str(unsigned long n) { return dec(n); }
inline String str(long n) { return dec(n); }
inline String str(uint64 n) { return dec(n); }
inline String str(int64 n) { return dec(n); }

String utoa(uint64 number, uint base=10, int pad=0, char padChar='0');
inline String hex(uint64 n, int pad=0) { return utoa(n, 16, pad); }
generic inline String str(T* const& p) { return "0x"+hex(ptr(p)); }
generic String str(const unique<T>& t) { return str(*t.pointer); }
generic String str(const shared<T>& t) { return str(*t.pointer); }

/// Converts floating-point numbers
String ftoa(double number, int precision=2, uint pad=0, int exponent=0);
String str(float n);
String str(double n);

/// Formats value using best binary prefix
String binaryPrefix(size_t value, string unit="B");

/// Converts arrays
template<Type T, typename enable_if<!is_same<char, T>::value>::type* = nullptr>
String str(const ref<T> source, char separator=' ') {
    String target;
    target.append('[');
    for(uint i: range(source.size)) {
        target.append( str(source[i]) );
        if(i<source.size-1) target.append(separator);
    }
    target.append(']');
    return target;
}
generic String str(const mref<T>& source, char separator=' ') { return str((const ref<T>)source, separator); }
generic String str(const buffer<T>& source, char separator=' ') { return str((const ref<T>)source, separator); }
generic String str(const array<T>& source, char separator=' ') { return str((const ref<T>)source, separator); }
inline String hex(const ref<uint8> source, char separator=' ') { return str(apply(source, [](const uint8& c) { return hex(c,2); }), separator); }
inline String hex(const ref<byte> source, char separator=' ') { return hex(cast<uint8>(source), separator); }

/// Converts static arrays
template<Type T, size_t N> String str(const T (&source)[N], char separator=' ') { return str(ref<T>(source, N), separator); }

/// Converts and concatenates all arguments separating with spaces
/// \note Use join({str(args)...}) to convert and concatenate without spaces
template<Type Arg, Type... Args> String str(const Arg& arg, const Args&... args) { return join({str(arg), str(args)...}," "); }

/// Logs to standard output using str(...) serialization
template<Type... Args> void log(const Args&... args) { log<string>(str(args...)); }
/// Logs to standard output using str(...) serialization and terminate all threads
template<Type... Args> void __attribute((noreturn)) error(const Args&... args) { error<string>(str(args...)); }
