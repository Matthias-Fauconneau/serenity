#pragma once
#include "core.h"
#include "array.h"

/// string

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
/*string replace(string before, string after) const {
string r; r.resize(size); T* d=(T*)r.data; for(int i=0;i<size;i++) d[i] = data[i] == before ? after : data[i]; return r;
}*/

/// stream

template<bool endian=false> struct Stream {
	const uint8* data;
	int size;
	int i=0;
	Stream(const uint8* data, int size) : data(data), size(size) {}
	operator bool() { return i<size; }
	template<class T=char> array<T> peek(int size) { array<T> t((T*)(data+i),size); return t; }
	template<class T=char> array<T> read(int size) { array<T> t((T*)(data+i),size); i+=size*(int)sizeof(T); return t; }
	template<class T> bool match(const array<T>& key) { if(peek(key.size) == key) { i+=key.size; return true; } else return false; }
	template<class T> T read() { T t=swap(*(T*)(data+i)); i+=(int)sizeof(T); return t; }
	struct ReadOperator { Stream * s; template <class T> operator T() { return s->read<T>(); } };
	ReadOperator read() { return ReadOperator{this}; }
	template<class T> Stream& operator >>(T& t) { t=read<T>(); return *this; }
	Stream& operator ++(int) { i++; return *this; }
	Stream& operator +=(int s) { i+=s; return *this; }
	uint8 operator*() { return data[i]; }
	//explicit operator array<uint8>() { return data.slice(i); }
};
typedef Stream<true> EndianStream;

/*struct TextStream : Stream<false> {
	TextStream(const char* data, int size) : Stream((uint8*)data,size) {}
	long readInteger(int base=10);
	double readFloat(int base=10);
};*/
