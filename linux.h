#pragma once
#pragma GCC system_header

#if __arm__
#define NR "7"
#define rR "r0"
#define rA "=r0"
#define r0 "r0"
#define r1 "r1"
#define r2 "r2"
#define r3 "r3"
#define r4 "r4"
#define r5 "r5"
#define kernel "swi 0"
#elif __x86_64__ || __i386__
#define NR "a"
#define rR "eax"
#define rA "=a"
#define r0 "b"
#define r1 "c"
#define r2 "d"
#define r3 "S"
#define r4 "D"
#define kernel "int $0x80"
#else
#error Unsupported architecture
#endif

#define syscall(type, name, args ...) register long r asm(rR); asm volatile(kernel : rA (r): NR (sys::name), ## args ); return (type)r;
#define syscall0(type,name) \
    inline type name() \
{syscall(type, name)}
#define syscall1(type,name,type0,arg0) \
    inline type name(type0 arg0) \
{syscall(type, name, r0 ((long)arg0))}
#define syscall2(type,name,type0,arg0,type1,arg1) \
    inline type name(type0 arg0, type1 arg1) \
{syscall(type, name, r0 ((long)arg0), r1 ((long)arg1))}
#define syscall3(type,name,type0,arg0,type1,arg1,type2,arg2) \
    inline type name(type0 arg0, type1 arg1, type2 arg2) \
{syscall(type, name, r0 ((long)arg0), r1 ((long)arg1), r2 ((long)arg2))}
#define syscall4(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3) \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3) \
{syscall(type, name, r0 ((long)arg0), r1 ((long)arg1), r2 ((long)arg2), r3 ((long)arg3))}
#define syscall5(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4) \
{syscall(type, name, r0 ((long)arg0), r1 ((long)arg1), r2 ((long)arg2), r3 ((long)arg3), r4 ((long)arg4))}
#if __x86_64__ || __i386__
#define syscall6(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5) \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) { \
    register long r5 asm("ebp") = (long)arg5; \
    syscall(type, name, r0 ((long)arg0), r1 ((long)arg1), r2 ((long)arg2), r3 ((long)arg3), r4 ((long)arg4), "r" (r5)) \
}
#else
#define syscall6(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5) \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{syscall(type, name, r0 ((long)arg0), r1 ((long)arg1), r2 ((long)arg2), r3 ((long)arg3), r4 ((long)arg4), r5 ((long)arg5))}
#endif

enum class sys : long {
    exit=1, fork, read, write, open, close, execve=11, brk=45, ioctl=54, fcntl, setrlimit=75, munmap=91, setpriority=97, socketcall=102,
    ipc = 117, socket=281, connect=283, getdents=141, poll=168, sigaction=174, mmap=192, fstat=197,
#if __arm__
    clock_gettime=263, openat=322, mkdirat, fstatat, unlinkat, symlinkat=331,
    timerfd_create=350, timerfd_settime=353
#elif __x86_64__ || __i386__
    clock_gettime=265, openat=295, mkdirat, fstatat=300, unlinkat, symlinkat=304,
    timerfd_create=322, timerfd_settime=325
#endif
};

typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;
struct sockaddr { short family; ushort port; uint ip; int pad[2]; };
struct sockaddr_un { ushort family=1; byte path[108]; };
struct timespec { ulong sec,nsec; };
struct rlimit { ulong cur,max; };
struct stat { uint64 dev; uint pad1; uint ino; uint mode; uint16 nlink; uint uid,gid; uint64 rdev; uint pad2;
              uint64 size; uint blksize; uint64 blocks; timespec atime,mtime,ctime; uint64 ino64; };
struct dirent { long ino, off; short len; char name[]; };
struct ipc_perm { int key; uint uid,gid,cuid,cgid; uint16 mode,pad1,seq,pad2; ulong pad3[2]; };
struct shmid_ds { ipc_perm perm; ulong size; ulong atime,pad1,dtime,pad2,ctime,pad3; int cpid,lpid; ulong count,pad4,pad5; };
enum {POLLIN = 1, POLLOUT=4, POLLHUP = 16};
enum {O_RDONLY, O_WRONLY, O_RDWR, O_CREAT=0100, O_TRUNC=01000, O_APPEND=02000, O_NONBLOCK=04000,
#if __arm__
      O_DIRECTORY=040000
  #else
      O_DIRECTORY=0200000
  #endif
    };
enum {PROT_READ=1, PROT_WRITE};
enum {MAP_SHARED=1, MAP_PRIVATE};
enum {DT_DIR=4, DT_REG=8 };
enum {F_SETFL=4};
enum {PF_LOCAL=1, PF_INET};
enum {SOCK_STREAM=1, SOCK_DGRAM};
enum {RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_RSS, RLIMIT_NOFILE, RLIMIT_AS };
enum {IPC_NEW=0, IPC_RMID=0, IPC_CREAT=01000};
int exit(int code) __attribute((noreturn));
syscall1(int, exit, int,code)
syscall0(int, fork)
syscall3(int, read, int,fd, void*,buf, long,size)
syscall3(int, write, int,fd, const void*,buf, long,size)
syscall3(int, open, const char*,name, int,oflag, int,perms)
syscall1(int, close, int,fd)
syscall3(int, execve, const char*,path, const char**,argv, const char**,envp)
syscall1(void*, brk, void*,new_brk)
syscall3(int, ioctl, int,fd, long,request, void*,buf)
syscall3(int, fcntl, int,fd, int,cmd, int,param)
syscall2(int, munmap, void*,addr, long,len)
syscall3(int, setpriority, int,which, int,who, int,prio)
syscall3(int, getdents, int,fd, dirent*,entry, long,size)
syscall3(int, poll, struct pollfd*,fds, long,nfds, int,timeout)
syscall4(int, sigaction, int,sig, const void*,act, void*,old, int, sigsetsize)
syscall6(void*, mmap, void*,addr, long,len, int,prot, int,flags, int,fd, long,offset)
syscall2(int, fstat, int,fd, stat*,buf)
syscall2(int, clock_gettime, int,type, timespec*,ts)
syscall4(int, openat, int,fd, const char*,name, int,oflag, int,perms)
syscall3(int, mkdirat, int,fd, const char*,name, int,mode)
syscall3(int, unlinkat, int,fd, const char*,name, int,flag)
syscall3(int, symlinkat, const char*,target, int,fd, const char*,name)
syscall2(int, timerfd_create, int,clock_id, int,flags)
syscall4(int, timerfd_settime, int,ufd, int,flags, timespec*,utmr, timespec*,otmr)
syscall2(int, setrlimit, int,resource, rlimit*,limit)

#if __i386__
syscall2(int, socketcall, int,call, long*,args)
inline int socket(int domain, int type, int protocol) { long a[]={domain,type,protocol}; return socketcall(1,a); }
inline int connect(int fd, struct sockaddr* addr, int len) { long a[]={fd,(long)addr,len}; return socketcall(3,a); }
#else
syscall3(int, socket, int,domain, int,type, int,protocol)
syscall3(int, connect, int,fd, struct sockaddr*,addr, int,len)
#endif

syscall6(int, ipc, uint,call, long,first, long,second, long,third, const void*,ptr, long,fifth)
inline long shmat(int id, const void* ptr, int flag) { long addr; return ipc(21,id,flag,(long)&addr,ptr,0)<0 ?: addr; }
inline int shmdt(const void* ptr) { return ipc(22,0,0,0,ptr,0); }
inline int shmget(int key, long size, int flag) { return ipc(23,key,size,flag,0,0); }
inline int shmctl(int id, int cmd, struct shmid_ds* buf) { return ipc(24,id,cmd,0,buf,0); }

#undef NR
#undef rA
#undef r0
#undef r1
#undef r2
#undef r3
#undef r4
#undef r5
