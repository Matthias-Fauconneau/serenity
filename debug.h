#pragma once
#include "string.h"

/// Enhanced debugging using str(...)
extern "C" ssize_t write(int fd, const void* buf, size_t size);
inline void write_(int fd, const array<byte>& s) { write(fd,s.data(),(size_t)s.size()); }

template<class... Args> inline void log(const Args&... args) { write_(1,str(args...)+"\n"_); }
template<class A> inline void log(const cat<A>& a) { write_(1,a+"\n"_); }
template<> inline void log(const string& args) { write_(1,args+"\n"_); }
#define warn log


struct Symbol { string file,function; uint line; };
Symbol findNearestLine(void* address);

/// Display variable name and its value
#define var(v) ({ auto t=v; debug( log(#v##_, t); )  t; })
/// Aborts unconditionally and display \a message
#define error(message...) ({ logTrace(); log(message); __builtin_abort();})
/// Aborts if \a expr evaluates to false and display \a message (except stack trace)
#define assert(expr, message...) ({debug( if(!(expr)) { logTrace(); log(#expr##_, ##message); __builtin_abort();})})
