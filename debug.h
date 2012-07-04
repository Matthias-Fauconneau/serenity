#pragma once
#include "string.h"

/// Enhanced debugging using str(...)
void write(int fd, const array<byte>& s);
template<class ___ Args> inline void log(const Args& ___ args) { write(1,str(args ___)+"\n"_); }
#define warn log

struct Symbol { string file,function; uint line=0; };
Symbol findNearestLine(void* address);
void catchErrors();

/// Aborts unconditionally without any message
void abort() __attribute((noreturn));
/// Aborts unconditionally and display \a message
#define error(message...) ({ trace(); log(message); abort(); })
/// Aborts if \a expr evaluates to false and display \a message (except stack trace)
#define assert(expr, message...) ({debug( if(!(expr)) { trace(); log(#expr##_, ##message); abort(); })})
