#pragma once
/// \file string.h String manipulations (using lightweight string when possible)
#include "array.h"
#include "cat.h"

// -- str()

// Enforces exact match for overload resolution
generic string str(const T&) { static_assert(0&&sizeof(T),"No overload for str(const T&)"); }

/// Forwards string
inline string str(string s) { return s; }
/// Forwards char[]
template<size_t N> string str(const char (&source)[N]) { return string(source); }
/// Forwards unique
generic auto str(const unique<T>& t) -> decltype(str(*t.pointer)) { return str(*t.pointer); }
/// Forwards shared
generic auto str(const shared<T>& t) -> decltype(str(*t.pointer)) { return str(*t.pointer); }

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

/// Returns a reference with heading and trailing whitespace removed
string trim(const string s);

/// Returns true if s contains only [0-9]
bool isInteger(const string s);
/// Parses an integer value
int64 parseInteger(const string str, int base=10);
/// Parses a decimal value
double parseDecimal(const string str);

// -- String

typedef buffer<char> String;

/// Converts a static reference to a buffer
//template<size_t N> buffer<char> staticRef(char const(&a)[N]) { return buffer<char>((char*)a, N-1 /*Discards trailing zero byte*/, 0); }
/// Returns const reference to a static string literal
inline String operator "" __(const char* data, size_t size) { return String((char*)data, size, 0); }

/// Forwards string
inline string str(const String& s) { return s; }

/// Null-terminated \a String with implicit conversion to const char*
struct strz : buffer<char> {
    /// Copies a string reference, appends a null byte and allows implicit conversion to const char*
	strz(const string s) : buffer(s.size+1) { slice(0, s.size).copy(s); last()='\0'; }
    operator const char*() { return data; }
};

/// Lowers case
char toLower(char c);
/// Lowers case
String toLower(const string s);
/// Uppers case
String toUpper(const string s);

/// Pads a string to the left
String left(string s, size_t size, const char pad=' ');
/// Pads a string to the right
String right(string s, size_t size, const char pad=' ');

/// Replaces every occurrence of \a before with \a after
String replace(string s, const string& before, const string& after);

/// Removes duplicate whitespace
String simplify(String&& s);
inline String simplify(string s) { return simplify(copyRef(s)); }

// -- string[]

/// Joins \a list into a single String with each element separated by \a separator
String join(ref<string> list, const string separator="");

/// Returns an array of references splitting \a str wherever \a separator occurs
array<string> split(const string str, string separator=", ");

/// Flatten cats
template<class A, class B, class T> String str(const cat<A, B, T>& a) { return a; }

// -- Number conversions

/// Converts an unsigned integer
String str(uint64 number, int pad=0, uint base=10, char padChar='0');
/// Converts an unsigned integer (implicit conversion)
inline String str(uint8 number, int pad=0, uint base=10, char padChar='0') { return str(uint64(number), pad, base, padChar); }
/// Converts an unsigned integer (implicit conversion)
inline String str(uint16 number, int pad=0, uint base=10, char padChar='0') { return str(uint64(number), pad, base, padChar); }
/// Converts an unsigned integer (implicit conversion)
inline String str(uint32 number, int pad=0, uint base=10, char padChar='0') { return str(uint64(number), pad, base, padChar); }
/// Converts an unsigned integer (implicit conversion)
inline String str(size_t number, int pad=0, uint base=10, char padChar='0') { return str(uint64(number), pad, base, padChar); }
/// Converts an unsigned integer in hexadecimal base
inline String hex(uint64 n, int pad=0) { return str(n, pad, 16); }
/// Converts a memory address in hexadecimal base
generic inline String str(T* const& p) { return "0x"+hex(ptr(p)); }

/// Converts a signed integer
String str(int64 number, int pad=0, uint base=10, char padChar=' ');
/// Converts a signed integer (implicit conversion)
inline String str(int32 n, int pad=0, uint base=10, char padChar=' ') { return str(int64(n), pad, base, padChar); }

/// Converts a floating-point number
String str(double number, int precision=2, uint pad=0, int exponent=0);
inline String str(float n, int precision=2) { return str(double(n), precision); }

/// Formats value using best binary prefix
String binaryPrefix(uint64 value, string unit="B", string unitSuffix="");

/// Converts arrays
template<Type T, typename enable_if<!is_same<char, T>::value>::type* = nullptr>
String str(const ref<T> source, string separator=" ") {
	array<char> target;
    target.append('[');
    for(uint i: range(source.size)) {
        target.append( str(source[i]) );
        if(i<source.size-1) target.append(separator);
    }
    target.append(']');
	return move(target);
}
generic String str(const mref<T>& source, string separator=" ") { return str((const ref<T>)source, separator); }
generic String str(const buffer<T>& source, string separator=" ") { return str((const ref<T>)source, separator); }
generic String str(const array<T>& source, string separator=" ") { return str((const ref<T>)source, separator); }
inline String hex(const ref<uint8> source, string separator=" ") { return str(apply(source, [](const uint8& c) { return hex(c,2); }), separator); }
inline String hex(const ref<byte> source, string separator=" ") { return hex(cast<uint8>(source), separator); }

/// Converts static arrays
template<Type T, size_t N> String str(const T (&source)[N], string separator=" ") { return str(ref<T>(source, N), separator); }

/// Converts and concatenates all arguments separating with spaces
/// \note Use join({str(args)...}) to convert and concatenate without spaces
template<Type Arg0, Type Arg1, Type... Args>
String str(const Arg0& arg0, const Arg1& arg1, const Args&... args) { return join({str(arg0), str(arg1), str(args)...}," "); }

/// Logs to standard output using str(...) serialization
template<Type... Args> void log(const Args&... args) { log((string)str(args...)); }
/// Logs to standard output using str(...) serialization and terminate all threads
template<Type... Args> void __attribute((noreturn)) error(const Args&... args) { error<string>(str(args...)); }

/// Converts Strings to strings
inline buffer<string> toRefs(ref<String> source) { return apply(source, [](const String& e) -> string { return e; }); }
inline String join(const ref<String> list, string separator="") { return join(toRefs(list), separator); }
