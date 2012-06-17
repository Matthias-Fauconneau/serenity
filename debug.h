#pragma once
#include "string.h"
#include "linux.h"

/// Enhanced debugging using str(...)
inline void write_(int fd, const array<byte>& s) { write(fd,s.data(),(ulong)s.size()); }

template<class ___ Args> inline void log(const Args& ___ args) { write_(1,str(args ___)+"\n"_); }
template<class A> inline void log(const cat<A>& a) { write_(1,a+"\n"_); }
inline void log(const string& args) { write_(1,args+"\n"_); }
#define warn log

struct Symbol { string file,function; uint line; };
Symbol findNearestLine(void* address);

/// Display variable name and its value
#define var(v) ({ auto t=v; debug( log(#v##_, t); )  t; })
/// Aborts unconditionally and display \a message
#define error(message...) ({ logTrace(); log(message); exit(-1); })
/// Aborts if \a expr evaluates to false and display \a message (except stack trace)
#define assert(expr, message...) ({debug( if(!(expr)) { logTrace(); log(#expr##_, ##message); exit(-1); })})

template<class T> inline string dump(const T& t) {
    array<uint16> raw((uint16*)&t,sizeof(T));
    string s; for(uint16 b: raw) s<<hex(b)<<" "_; return s;
}
