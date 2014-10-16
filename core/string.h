#pragma once
/// \file string.h String manipulations (using lightweight string when possible)
#include "array.h"

// -- str()

// Enforces exact match for overload resolution
generic string str(const T&) { static_assert(0&&sizeof(T),"No overload for str(const T&)"); }

/// Forwards string
inline string str(string s) { return s; }
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

// -- String

/// array<char> holding a UTF8 text string with a stack-allocated buffer for small strings
struct String : array<char> {
    char buffer[32];
    String() : array<char>(::buffer<char>(mref<char>(buffer))) {}
    /// Allocates an empty array with storage space for \a capacity elements, on stack if possible
    String(size_t capacity) { reserve(capacity); }
    /// Copies a string reference
    String(const string s) : String(s.size) { append(s); }

    void reserve(size_t nextCapacity) override {
        if(data!=buffer) array<char>::reserve(nextCapacity);
        else if(nextCapacity > sizeof(buffer)) { data=0, capacity=0; array<char>::reserve(nextCapacity); copy(ref<char>(buffer, size)); }
    }
};
static_assert(sizeof(String)==64,"");

/// Forwards String
inline string str(const String& s) { return s; }

/// Converts Strings to strings
inline buffer<string> toRefs(const ref<String>& source) { return apply(source, [](const String& e) -> string { return  e; }); }

/// Null-terminated \a String with implicit conversion to const char*
struct strz : String {
    /// Copies a string reference, appends a null byte and allows implicit conversion to const char*
    strz(const string s) : String(s.size+1) { append(s); append('\0'); }
    operator const char*() { return data; }
};

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

// -- cat

/// Concatenates another \a cat and a value
template<class A, class T> struct cat {
    const A a; const T b;
    cat(const A a, const T b) : a(a), b(b) {}
    int size() const { return a.size() + 1; };
    void copy(mref<char> target) const { a.copy(target.slice(0, a.size())); target.set(a.size(), b); }
    //operator buffer<char>() const { buffer<char> target (size()); copy(target); return move(target); }
    //operator array<char>() const { return operator buffer<char>(); }
    operator String() const { String target (size()); copy(target); return move(target); }
};
template<class A, class B> cat<cat<A,B>, char> operator +(cat<A,B> a, char b) { return {a,b}; }

/// Concatenates another \a cat and a ref
template<class A> struct cat<A, ref<char>> {
    const A a; const ref<char> b;
    cat(A a, ref<char> b) : a(a), b(b) {}
    int size() const { return a.size() + b.size; };
    void copy(mref<char> target) const { a.copy(target.slice(0, a.size())); target.slice(a.size()).copy(b); }
    //operator buffer<char>() const { buffer<char> target (size()); copy(target); return move(target); }
    //operator array<char>() const { return operator buffer<char>(); }
    operator String() const { String target (size()); copy(target); return move(target); }
};
template<class A, class B> cat<cat<A, B>, ref<char>> operator +(cat<A, B> a, ref<char> b) { return {a,b}; }
// Required for implicit string literal conversion
//template<class char, class A, class B, size_t N> cat<cat<A, B>, ref<char>> operator +(cat<A, B> a, const char(&b)[N]) { return {a,b}; }
// Prevents wrong match with operator +(const cat<A, B>& a, const char& b)
//template<class char, class A, class B> cat<cat<A, B>, ref<char>> operator +(cat<A, B> a, const buffer<char>& b) { return {a,b}; }
//template<class char, class A, class B> cat<cat<A, B>, ref<char>> operator +(cat<A, B> a, const array<char>& b) { return {a,b}; }

/// Specialization to concatenate a value with a ref
template<> struct cat<char, ref<char>> {
    char a; ref<char> b;
    cat(char a, ref<char> b) : a(a), b(b) {}
    int size() const { return 1 + b.size; };
    void copy(mref<char> target) const { target.set(0, a); target.slice(1).copy(b); }
    //operator buffer<char>() const { buffer<char> target (size()); copy(target); return move(target); }
    //operator array<char>() const { return operator buffer<char>(); }
    operator String() const { String target (size()); copy(target); return move(target); }
};
cat<char, ref<char>> operator +(char a, ref<char> b) { return {a,b}; }

/// Specialization to concatenate a ref with a value
template<> struct cat<ref<char>, char> {
    ref<char> a; char b;
    cat(ref<char> a, char b) : a(a), b(b) {}
    int size() const { return a.size + 1; };
    void copy(mref<char> target) const { target.slice(0, a.size).copy(a); target.set(a.size, b); }
    //operator buffer<char>() const { buffer<char> target (size()); copy(target); return move(target); }
    //operator array<char>() const { return operator buffer<char>(); }
    operator String() const { String target (size()); copy(target); return move(target); }
};
cat<ref<char>, char> operator +(ref<char> a, char b) { return {a,b}; }

/// Specialization to concatenate a ref with a ref
template<> struct cat<ref<char>, ref<char>> {
    ref<char> a; ref<char> b;
    cat(ref<char> a, ref<char> b) : a(a), b(b) {}
    int size() const { return a.size + b.size; };
    void copy(mref<char> target) const { target.slice(0, a.size).copy(a); target.slice(a.size).copy(b); }
    //operator array<char>() const { buffer<char> target (size()); copy(target); return move(target); }
    operator String() const { String target (size()); copy(target); return move(target); }
};
//generic cat<ref<char>,ref<char>> operator +(ref<char> a, ref<char> b) { return {a,b}; }
//generic cat<ref<char>,ref<char>> operator +(ref<char> a, const array<char>& b) { return {a,b}; }
// Required for implicit string literal conversion
inline cat<ref<char>,ref<char>> operator +(ref<char> a, ref<char> b) { return {a,b}; }

/// Flatten cats
template<class A, class B> String str(const cat<A, B>& a) { return a; }

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
String binaryPrefix(size_t value, string unit="B", string unitSuffix="");

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
