#pragma once
#include "string.h"

struct Symbol { ref<byte> file; string function; uint line=0; };
/// Returns debug symbol nearest to address
Symbol findNearestLine(void* address);

/// Writes /a data to /a fd
bool write(int fd, const ref<byte>& data);

/// Logs to standard output using str(...) serialization
template<class ___ Args> void log(const Args& ___ args) { write(1,string(str(args ___)+"\n"_)); }

/// Critical error in debug mode, log in release mode
#if DEBUG
#define warn error
#else
#define warn log
#endif

/// Aborts unconditionally and logs \a message
#define error(message...) ({ trace(); log(message); exit_(-1); })

/// Logs \a expr name and value
#define eval(expr) log(#expr ":",expr)

/// Aborts if \a expr evaluates to false and logs \a expr and \a message
#define assert(expr, message...) ({debug( if(!(expr)) error(#expr, ##message);)})
/// Aborts if \a expr is negative and logs corresponding error code
#define check(expr, message...) ({ long e=(long)expr; if(e<0 && -e<133) warn(#expr##_, errno[-e], ##message); e; })
#define check_(expr, message...) ({ long unused e=expr; if(e<0 && -e<133) warn(#expr##_, errno[-e], ##message); })

/// Linux error code names
#include "asm-generic/errno.h"
constexpr ref<byte> errno[] = {"OK"_, "PERM"_, "NOENT"_, "SRCH"_, "INTR"_, "IO"_, "NXIO"_, "2BIG"_, "NOEXEC"_, "BADF"_, "CHILD"_, "AGAIN"_, "NOMEM"_, "ACCES"_, "FAULT"_,"NOTBLK"_, "BUSY"_, "EXIST"_, "XDEV"_, "NODEV"_, "NOTDIR"_, "ISDIR"_, "INVAL"_, "NFILE"_, "MFILE"_, "NOTTY"_, "TXTBSY"_, "FBIG"_, "NOSPC"_, "SPIPE"_, "ROFS"_, "MLINK"_, "PIPE"_, "DOM"_, "RANGE"_, "DEADLK"_, "NAMETOOLONG"_, "NOLCK"_, "NOSYS"_, "NOTEMPTY"_, "LOOP"_, "WOULDBLOCK"_, "NOMSG"_, "IDRM"_, "CHRNG"_, "L2NSYNC"_, "L3HLT"_, "L3RST"_, "LNRNG"_, "UNATCH"_, "NOCSI"_, "L2HLT"_, "BADE"_, "BADR"_, "XFULL"_, "NOANO"_, "BADRQC"_, "BADSLT"_,"DEADLOCK""EBFONT"_, "NOSTR"_, "NODATA"_, "TIME"_, "NOSR"_, "NONET"_, "NOPKG"_, "REMOTE"_, "NOLINK"_, "ADV"_, "SRMNT"_, "COMM"_, "PROTO"_, "MULTIHO"_, "DOTDOT"_, "BADMSG"_, "OVERFLOW"_, "NOTUNIQ"_, "BADFD"_, "REMCHG"_, "LIBACC"_, "LIBBAD"_, "LIBSCN"_, "LIBMAX"_, "LIBEXEC"_, "ILSEQ"_, "RESTART"_, "STRPIPE"_, "USERS"_, "NOTSOCK"_, "DESTADDRREQ"_, "MSGSIZE"_, "PROTOTYPE"_, "NOPROTOOPT"_, "PROTONOSUPPORT"_, "SOCKTNOSUPPORT"_, "OPNOTSUPP"_, "PFNOSUPPORT"_, "AFNOSUPPORT"_, "ADDRINUSE"_, "ADDRNOTAVAIL"_, "NETDOWN"_, "NETUNREACH"_, "NETRESET"_, "CONNABORTED"_, "CONNRESET"_, "NOBUFS"_, "ISCONN"_, "NOTCONN"_, "SHUTDOWN"_, "TOOMANYREFS"_, "TIMEDOUT"_, "CONNREFUSED"_, "HOSTDOWN"_, "HOSTUNREACH"_, "ALREADY"_, "INPROGRESS"_, "STALE"_, "UCLEAN"_, "NOTNAM"_, "NAVAIL"_, "ISNAM"_, "REMOTEIO"_, "DQUOT"_, "NOMEDIUM"_, "MEDIUMTYPE"_, "CANCELED"_, "NOKEY"_, "KEYEXPIRED"_, "KEYREVOKED"_, "KEYREJECTED"_, "OWNERDEAD"_, "NOTRECOVERABLE"_," ERFKILL"_," EHWPOISON"_};
