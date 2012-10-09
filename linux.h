#pragma once
/// file linux.h Linux kernel syscalls and error codes

#if __x86_64
#define attribute __attribute((always_inline))
#define rN rax
#define r0 rdi
#define r1 rsi
#define r2 rdx
#define r3 r10
#define r4 r8
#define r5 r9
#define kernel "syscall"
#define clobber "rcx", "r11",
#define rR "rax"
#elif __arm
#define attribute __attribute((always_inline))
#define rN r7
#define r0 r0
#define r1 r1
#define r2 r2
#define r3 r3
#define r4 r4
#define r5 r5
#define kernel "swi 0"
#define clobber
#define rR "r0"
#elif __i386
#define attribute __attribute((noinline))
#define rN eax
#define r0 ebx
#define r1 ecx
#define r2 edx
#define r3 esi
#define r4 edi
#define r5 ebp
#define kernel "int $0x80"
#define clobber
#define rR "eax"
#else
#error Unsupported architecture
#endif

#define str(s) #s
#define r(reg,arg) register long reg asm(str(reg)) = (long)arg;
#define syscall(type, name, args ...) \
    r(rN,sys::name); \
    register long r asm(rR); \
    asm volatile(kernel: "=r" (r): "r"(rN), ## args : clobber "memory"); \
    return (type)r;

#define syscall0(type,name) \
    inline type name() \
{syscall(type, name)}
#define syscall1(type,name,type0,arg0) \
    inline attribute type name(type0 arg0) \
{r(r0,arg0) syscall(type, name, "r"(r0))}
#define syscall2(type,name,type0,arg0,type1,arg1) \
    inline attribute type name(type0 arg0, type1 arg1) \
{r(r0,arg0) r(r1,arg1) syscall(type, name, "r"(r0), "r"(r1))}
#define syscall3(type,name,type0,arg0,type1,arg1,type2,arg2) \
    inline attribute type name(type0 arg0, type1 arg1, type2 arg2) \
{r(r0,arg0) r(r1,arg1) r(r2,arg2) syscall(type, name, "r"(r0), "r"(r1), "r"(r2))}
#define syscall4(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3) \
    inline attribute type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3) \
{r(r0,arg0) r(r1,arg1) r(r2,arg2) r(r3,arg3) syscall(type, name, "r"(r0), "r"(r1), "r"(r2), "r"(r3))}
#define syscall5(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
    inline attribute type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4) \
{r(r0,arg0) r(r1,arg1) r(r2,arg2) r(r3,arg3) r(r4,arg4) syscall(type, name, "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4))}
#define syscall6(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5) \
    inline attribute type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{r(r0,arg0) r(r1,arg1) r(r2,arg2) r(r3,arg3) r(r4,arg4) r(r5, arg5) syscall(type, name, "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5))}

enum class sys {
#if __x86_64
    read, write, open, close, stat, fstat, lstat, poll, lseek, mmap, mprotect, munmap, brk, sigaction, sigprocmask, ioctl=16, sched_yield=24, shmget=29, shmat, shmctl, pause=34,
    getpid=39, socket=41, connect, clone=56, fork, execve=59, exit, wait4, kill, shmdt=67, fcntl=72, getdents=78, setpriority=141, mlock=149, mlockall=151, setrlimit=160,
    gettid=186, futex=202, clock_gettime=228, exit_group=231, tgkill=234, openat=257, mkdirat, unlinkat=263, symlinkat=266, utimensat=280, timerfd_create=283,
    timerfd_settime=286, eventfd2=290
#else
    exit=1, fork, read, write, open, close, execve=11, getpid=20, brk=45, ioctl=54, fcntl, setrlimit=75, munmap=91, setpriority=97, socketcall=102, wait4=114,
    ipc = 117, clone=120, mprotect=125, getdents=141, mlock=150, sched_yield=158, poll=168, sigaction=174, mmap=192, fstat=197,
    gettid=224, futex=240, exit_group = 248,
    socket=281, connect=283,
#if __arm__
    clock_gettime=263, tgkill=268,
    shmat=305,shmdt,shmget,shmctl,
    openat=322, mkdirat, fstatat, unlinkat, symlinkat=331, utimensat=348,
    timerfd_create=350, timerfd_settime=353, eventfd2=356
#elif __i386__
    clock_gettime=265,
    openat=295, mkdirat, fstatat=300, unlinkat, symlinkat=304,
    timerfd_create=322, timerfd_settime=325
#endif
#endif
};

syscall3(int, read, int,fd, void*,buf, long,size)
syscall3(int, write, int,fd, const void*,buf, long,size)
syscall3(int, open, const char*,name, int,oflag, int,perms)
syscall1(int, close, int,fd)
syscall2(int, fstat, int,fd, struct stat*,buf)
syscall3(int, poll, struct pollfd*,fds, long,nfds, int,timeout)
//syscall3(int, lseek, int,fd, long,offset, int,whence)
syscall6(void*, mmap, void*,addr, long,len, int,prot, int,flags, int,fd, long,offset)
syscall2(int, munmap, void*,addr, long,len)
syscall3(int, mprotect, void*,addr, long,len, int,prot)
syscall1(void*, brk, void*,new_brk)
syscall4(int, sigaction, int,sig, const void*,act, void*,old, int, sigsetsize)
syscall3(int, ioctl, int,fd, long,request, void*,arguments)
syscall0(int, sched_yield)
#if !__i386__
syscall3(int, shmget, int,key, long,size, int,flag)
syscall3(long, shmat, int,id, const void*,ptr, int,flag)
syscall3(int, shmctl, int,id, int,cmd, struct shmid_ds*,buf)
syscall1(int, shmdt, const void*,ptr)
#endif
syscall0(int, getpid)
#if !__i386__
syscall3(int, socket, int,domain, int,type, int,protocol)
syscall3(int, connect, int,fd, void*,addr, int,len)
#endif
//syscall5(long, clone, long,flags, void*,stack, void*,ptid, void*,ctid, struct pt_regs*,regs)
syscall0(int, fork)
syscall3(int, execve, const char*,path, const char**,argv, const char**,envp)
//syscall1(int, exit, int, status)
syscall4(int, wait4, int,pid, int*,status, int,options, struct rusage*, rusage)
syscall3(int, fcntl, int,fd, int,cmd, int,param)
syscall3(int, getdents, int,fd, void*,entry, long,size)
syscall3(int, setpriority, int,which, int,who, int,prio)
syscall2(int, mlock,const void*,addr, long,len)
syscall2(int, setrlimit, int,resource, struct rlimit*,limit)
syscall0(int, gettid)
syscall6(int, futex, int*,uaddr, int,op, int,val, const struct timespec*,timeout, int*,uaddr2, int,val3)
syscall2(int, clock_gettime, int,type, struct timespec*,ts)
//syscall1(int, exit_group, int, status)
syscall3(int, tgkill, int,tgid, int,tid, int,sig)
syscall4(int, openat, int,fd, const char*,name, int,oflag, int,perms)
syscall3(int, mkdirat, int,fd, const char*,name, int,mode)
syscall3(int, unlinkat, int,fd, const char*,name, int,flag)
syscall3(int, symlinkat, const char*,target, int,fd, const char*,name)
syscall4(int, utimensat, int,fd, const char*,name, const struct timespec*,times, int,flags)
syscall2(int, timerfd_create, int,clock_id, int,flags)
syscall4(int, timerfd_settime, int,ufd, int,flags, const struct timespec*,utmr, struct timespec*,otmr)
syscall2(int, eventfd2, int,val, int,flags)

inline __attribute((noreturn)) void exit_thread(int status) {r(r0,status) r(rN,sys::exit); asm volatile(kernel:: "r"(rN), "r"(r0)); __builtin_unreachable();}
inline __attribute((noreturn)) int exit_group(int status) {r(r0,status)  r(rN,sys::exit_group); asm volatile(kernel:: "r"(rN), "r"(r0)); __builtin_unreachable();}
inline __attribute((noinline)) long clone(int (*fn)(void*), void* stack, int flags, void* arg) {
    r(rN,sys::clone) r(r0,flags) r(r1,stack) r(r2,0) r(r3,0) r(r4,0) register long r asm(rR);
    asm volatile("mov %0, %%r14; mov %1, %%r15"::"r"(arg), "r"(fn): "r14", "r15");
    asm volatile("syscall": "=r"(r):"r"(rN), "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4) : "rsp", "rcx", "r9", "r11");
    if(r!=0) return r;
    asm("movq $0, %%rbp; movq %%r14, %%rdi; call *%%r15; movq %%rax, %%rdi; movq $60, %%rax; syscall":::/*"rbp",*/"rsp","rdi","rax");
    __builtin_unreachable();
}
#if __i386
syscall6(int, ipc, int,call, long,first, long,second, long,third, const void*,ptr, long,fifth)
inline long shmat(int id, const void* ptr, int flag) { long addr; return ipc(21,id,flag,(long)&addr,ptr,0)<0 ?: addr; }
inline int shmdt(const void* ptr) { return ipc(22,0,0,0,ptr,0); }
inline int shmget(int key, long size, int flag) { return ipc(23,key,size,flag,0,0); }
inline int shmctl(int id, int cmd, struct shmid_ds* buf) { return ipc(24,id,cmd,0,buf,0); }
syscall2(int, socketcall, int,call, long*,args)
inline int socket(int domain, int type, int protocol) { long a[]={domain,type,protocol}; return socketcall(1,a); }
inline int connect(int fd, struct sockaddr* addr, int len) { long a[]={fd,(long)addr,len}; return socketcall(3,a); }
#endif

#undef attribute
#undef rN
#undef r0
#undef r1
#undef r2
#undef r3
#undef r4
#undef r5
#undef kernel
#undef clobber
#undef rR
#undef str
#undef r
#undef syscall
#undef syscall0
#undef syscall1
#undef syscall2
#undef syscall3
#undef syscall4
#undef syscall5
#undef syscall6

/// Linux error code names
enum Error {OK, PERM, NOENT, SRCH, INTR, EIO, NXIO, TOOBIG, NOEXEC, BADF, CHILD, AGAIN, NOMEM, ACCES, FAULT, NOTBLK, BUSY, EXIST, XDEV, NODEV, NOTDIR, ISDIR, INVAL, NFILE, MFILE, NOTTY, TXTBSY, FBIG, NOSPC, SPIPE, ROFS, MLINK, PIPE, DOM, RANGE, DEADLK, NAMETOOLONG, NOLCK, NOSYS, NOTEMPTY, LOOP, WOULDBLOCK, NOMSG, IDRM, CHRNG, L2NSYNC, L3HLT, L3RST, LNRNG, UNATCH, NOCSI, L2HLT, BADE, BADR, XFULL, NOANO, BADRQC, BADSLT, DEADLOCK, EBFONT, NOSTR, NODATA, TIME, NOSR, NONET, NOPKG, REMOTE, NOLINK, ADV, SRMNT, COMM, PROTO, MULTIHO, DOTDOT, BADMSG, OVERFLOW, NOTUNIQ, BADFD, REMCHG, LIBACC, LIBBAD, LIBSCN, LIBMAX, LIBEXEC, ILSEQ, RESTART, STRPIPE, USERS, NOTSOCK, DESTADDRREQ, MSGSIZE, PROTOTYPE, NOPROTOOPT, PROTONOSUPPORT, SOCKTNOSUPPORT, OPNOTSUPP, PFNOSUPPORT, AFNOSUPPORT, ADDRINUSE, ADDRNOTAVAIL, NETDOWN, NETUNREACH, NETRESET, CONNABORTED, CONNRESET, NOBUFS, ISCONN, NOTCONN, SHUTDOWN, TOOMANYREFS, TIMEDOUT, CONNREFUSED, HOSTDOWN, HOSTUNREACH, ALREADY, INPROGRESS, STALE, UCLEAN, NOTNAM, NAVAIL, ISNAM, REMOTEIO, DQUOT, NOMEDIUM, MEDIUMTYPE, CANCELED, NOKEY, KEYEXPIRED, KEYREVOKED, KEYREJECTED, OWNERDEAD, NOTRECOVERABLE, RFKILL, HWPOISON, LAST};
constexpr ref<byte> errno[] = {"OK"_, "PERM"_, "NOENT"_, "SRCH"_, "INTR"_, "IO"_, "NXIO"_, "TOOBIG"_, "NOEXEC"_, "BADF"_, "CHILD"_, "AGAIN"_, "NOMEM"_, "ACCES"_, "FAULT"_, "NOTBLK"_, "BUSY"_, "EXIST"_, "XDEV"_, "NODEV"_, "NOTDIR"_, "ISDIR"_, "INVAL"_, "NFILE"_, "MFILE"_, "NOTTY"_, "TXTBSY"_, "FBIG"_, "NOSPC"_, "SPIPE"_, "ROFS"_, "MLINK"_, "PIPE"_, "DOM"_, "RANGE"_, "DEADLK"_, "NAMETOOLONG"_, "NOLCK"_, "NOSYS"_, "NOTEMPTY"_, "LOOP"_, "WOULDBLOCK"_, "NOMSG"_, "IDRM"_, "CHRNG"_, "L2NSYNC"_, "L3HLT"_, "L3RST"_, "LNRNG"_, "UNATCH"_, "NOCSI"_, "L2HLT"_, "BADE"_, "BADR"_, "XFULL"_, "NOANO"_, "BADRQC"_, "BADSLT"_, "DEADLOCK"_, "EBFONT"_, "NOSTR"_, "NODATA"_, "TIME"_, "NOSR"_, "NONET"_, "NOPKG"_, "REMOTE"_, "NOLINK"_, "ADV"_, "SRMNT"_, "COMM"_, "PROTO"_, "MULTIHO"_, "DOTDOT"_, "BADMSG"_, "OVERFLOW"_, "NOTUNIQ"_, "BADFD"_, "REMCHG"_, "LIBACC"_, "LIBBAD"_, "LIBSCN"_, "LIBMAX"_, "LIBEXEC"_, "ILSEQ"_, "RESTART"_, "STRPIPE"_, "USERS"_, "NOTSOCK"_, "DESTADDRREQ"_, "MSGSIZE"_, "PROTOTYPE"_, "NOPROTOOPT"_, "PROTONOSUPPORT"_, "SOCKTNOSUPPORT"_, "OPNOTSUPP"_, "PFNOSUPPORT"_, "AFNOSUPPORT"_, "ADDRINUSE"_, "ADDRNOTAVAIL"_, "NETDOWN"_, "NETUNREACH"_, "NETRESET"_, "CONNABORTED"_, "CONNRESET"_, "NOBUFS"_, "ISCONN"_, "NOTCONN"_, "SHUTDOWN"_, "TOOMANYREFS"_, "TIMEDOUT"_, "CONNREFUSED"_, "HOSTDOWN"_, "HOSTUNREACH"_, "ALREADY"_, "INPROGRESS"_, "STALE"_, "UCLEAN"_, "NOTNAM"_, "NAVAIL"_, "ISNAM"_, "REMOTEIO"_, "DQUOT"_, "NOMEDIUM"_, "MEDIUMTYPE"_, "CANCELED"_, "NOKEY"_, "KEYEXPIRED"_, "KEYREVOKED"_, "KEYREJECTED"_, "OWNERDEAD"_, "NOTRECOVERABLE"_, "RFKILL"_, "HWPOISON"_};
/// Aborts if \a expr is negative and logs corresponding error code
#define check(expr, message...) ({ long e=(long)expr; if(e<0 && -e<LAST) warn(#expr ""_, errno[-e], ##message); e; })
/// Aborts if \a expr is negative and logs corresponding error code (unused result)
#define check_(expr, message...) ({ long unused e=expr; if(e<0 && -e<LAST) warn(#expr ""_, errno[-e], ##message); })
/// Aborts if \a expr is negative and logs corresponding error code (unless EINTR or EAGAIN)
#define check__(expr, message...) ({ long unused e=expr; if(e<0 && -e<LAST && -e!=INTR) warn(#expr ""_, errno[-e], ##message); e; })
