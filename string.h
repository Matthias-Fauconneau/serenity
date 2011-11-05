#pragma once
#include "core.h"
#include "array.h"

/// string
typedef array<char> string;
#define _(s) string(s,sizeof(s)-1)
void log_(const string& s);
string strz(const string& s);
string strz(const char* s);
string toString(long n, int base=10, int pad=0);
inline string toString(int n, int base=10, int pad=0) { return toString((long)n,base,pad); }
string toString(double number);
long toInteger(const string& str, int base=10 );
long readInteger(const char*& s, int base=10);
double toFloat(const string& str, int base=10 );
double readFloat(const char*& s, int base=10 );
string section(const string& str, char sep, int start=0, int end=1);

inline bool operator <(const string& a, const string& b) {
	for(int i=0;i<min(a.size,b.size);i++) {
		if(a[i] > b[i]) return false;
		if(a[i] < b[i]) return true;
	}
	return a.size < b.size;
}

template <class A> struct cat {
	const A& a; const string& b;
	struct { cat* c; operator int() const { return c->a.size+c->b.size; } } size;
	cat(const A& a,const string& b) : a(a), b(b) { size.c=this; }
	void copy(char* data) const { a.copy(data); ::copy(data+a.size,b.data,b.size); }
	operator string() { string r; r.resize(size); copy((char*)r.data); return r; }
};
template <class A> cat<A> operator +(const A& a,const string& b) { return cat<A>(a,b); }

/// stream

constexpr uint32 swap32(uint32 x) { return ((x&0xff000000)>>24)|((x&0x00ff0000)>>8)|((x&0x0000ff00)<<8)|((x&0x000000ff)<<24); }
constexpr uint16 swap16(uint16 x) { return ((x>>8)&0xff)|((x&0xff)<<8); }

struct Stream {
	const uint8* data;
	int size;
	int i=0;
	Stream(const uint8* data, int size) : data(data), size(size) {}
	Stream(const string& data) : data((uint8*)data.data), size(data.size) {}
	Stream(const array<uint8>& data) : data(data.data), size(data.size) {}
	operator bool() { return i<size; }
	template<class T=char> array<T> peek(int size) { array<T> t((T*)(data+i),size); return t; }
	template<class T=char> T readRaw() { T t = *(T*)(data+i); i+=sizeof(T); return t; }
	template<class T> T read();
	template<class T=char> T* readRaw(int size) { T* t = (T*)(data+i); i+=size*sizeof(T); return t; }
	template<class T=char> array<T> read(int size) { return array<T>(readRaw<T>(size),size); }
	template<class T> bool match(const array<T>& key) { if(peek(key.size) == key) { i+=key.size; return true; } else return false; }
	struct ReadOperator {
		Stream * s;
		operator uint32() { return swap32(s->readRaw<uint32>()); }
		operator uint16() { return swap16(s->readRaw<uint16>()); }
		operator uint8() { return s->readRaw<uint8>(); }
	};
	ReadOperator read() { return ReadOperator{this}; }
	Stream& operator ++(int) { i++; return *this; }
	Stream& operator +=(int s) { i+=s; return *this; }
	uint8 operator*() { return data[i]; }
	uint8 operator[](int offset) { return data[i+offset]; }
	//explicit operator array<uint8>() { return data.slice(i); }
	array<uint8> slice(int offset, int size) { return array<uint8>(data+i+offset,size); }
};

struct TextStream : Stream {
	//TextStream(const char* data, int size) : Stream((uint8*)data,size) {}
	TextStream(const string& data) : Stream(data) {}
	long readInteger(int base=10);
	double readFloat(int base=10);
	operator string() { return string((const char*)data+i,size-i); }
	operator const char*() { return (const char*)data+i; }
};
