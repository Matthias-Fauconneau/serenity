#pragma once
#include "string.h"

/// Enhanced debugging using str(...)
void write(int fd, const array<byte>& s);
template<class ___ Args> inline void log(const Args& ___ args) { write(1,str(args ___)+"\n"_); }
#define warn log

struct Symbol { string file,function; uint line=0; };
Symbol findNearestLine(void* address);
void catchErrors();

/// Aborts unconditionally and display \a message
#define error(message...) ({ trace(); log(message); abort(); })
/// Aborts if \a expr evaluates to false and display \a expr and \a message
#define assert(expr, message...) ({debug( if(!(expr)) error(#expr##_, ##message);)})
/// Aborts if \a expr is negative and display corresponding error code
extern const char* errno[];
#define check(expr, message...) ({ int e=expr; debug( if(e<0) error(#expr##_, errno[-e], ##message); ) e; })
