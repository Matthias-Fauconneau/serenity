#pragma once
#include "string.h"

/// Enhanced debugging using str(...)
void write_(int fd, const array<byte>& s);
template<class ___ Args> inline void log(const Args& ___ args) { write_(1,str(args ___)+"\n"_); }
template<class A> inline void log(const cat<A>& a) { write_(1,a+"\n"_); }
inline void log(const string& args) { write_(1,args+"\n"_); }
#define warn log

struct Symbol { string file,function; uint line=0; };
Symbol findNearestLine(void* address);
void catchErrors();

/// Aborts unconditionally without any message
void abort() __attribute((noreturn));
/// Aborts unconditionally and display \a message
#define error(message...) ({ logTrace(); log(message); abort(); })
/// Aborts if \a expr evaluates to false and display \a message (except stack trace)
#define assert(expr, message...) ({debug( if(!(expr)) { logTrace(); log(#expr##_, ##message); abort(); })})
