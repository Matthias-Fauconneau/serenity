#pragma once
#include "string.h"

/// Setup signal handlers to log trace on {ABRT,SEGV,TERM.PIPE}
void catchErrors();

/// Writes /a data to /a fd
void write(int fd, const ref<byte>& data);

/// Logs to standard output using str(...) serialization
template<class ___ Args> void log(const Args& ___ args) { write(1,string(str(args ___)+"\n"_)); }
#define warn log

/// Logs \a expr name and value
#define eval(expr) log(#expr ":",expr)

struct Symbol { ref<byte> file; string function; uint line=0; };
/// Returns debug symbol nearest to address
Symbol findNearestLine(void* address);

/// Logs current stack trace skipping /a skip last frames
void trace(int skip=0, uint size=-1);
/// Aborts unconditionally without any message
void abort() __attribute((noreturn));
/// Aborts unconditionally and display \a message
#define error(message...) ({ trace(); log(message); abort(); })
/// Aborts if \a expr evaluates to false and display \a expr and \a message
#define assert(expr, message...) ({debug( if(!(expr)) error(#expr, ##message);)})
/// Aborts if \a expr is negative and display corresponding error code
#define check(expr, message...) ({ long e=expr; debug( if(e<0) error(#expr##_, string(errno[-e]), ##message); ) e; })
#define check_(expr, message...) ({ long e=expr; debug( if(e<0) error(#expr##_, string(errno[-e]), ##message); ); })
/// Linux error code names
constexpr ref<byte> errno[] = {"OK"_, "PERM"_, "NOENT"_, "SRCH"_, "INTR"_, "IO"_, "NXIO"_, "2BIG"_, "NOEXEC"_, "BADF"_, "CHILD"_, "AGAIN"_, "NOMEM"_, "ACCES"_, "FAULT"_,"NOTBLK"_, "BUSY"_, "EXIST"_, "XDEV"_, "NODEV"_, "NOTDIR"_, "ISDIR"_, "INVAL"_, "NFILE"_, "MFILE"_, "NOTTY"_, "TXTBSY"_, "FBIG"_, "NOSPC"_, "SPIPE"_, "ROFS"_, "MLINK"_, "PIPE"_, "DOM"_, "RANGE"_, "DEADLK"_, "NAMETOOLONG"_, "NOLCK"_, "NOSYS"_, "NOTEMPTY"_, "LOOP"_, "EWOULDBLOCK"_, "NOMSG"_, "IDRM"_, "CHRNG"_, "L2NSYNC"_, "L3HLT"_, "L3RST"_, "LNRNG"_, "UNATCH"_, "NOCSI"_, "L2HLT"_, "BADE"_, "BADR"_, "XFULL"_, "NOANO"_, "BADRQC"_, "BADSLT"_};
