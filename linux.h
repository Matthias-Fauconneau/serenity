#pragma once

#if __x86_64
#define attribute
#define rN "rax"
#define r0 rdi
#define r1 rsi
#define r2 rdx
#define r3 r10
#define r4 r8
#define r5 r9
#define kernel "syscall"
#define rR "rax"
#define clobber "rcx", "r11"
#elif __arm
#define attribute
#define rN "r7"
#define kernel "swi 0"
#define rR "r0"
#define clobber
#elif __i386
#define attribute __attribute((noinline))
#define rN "eax"
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
#define r(reg,arg) volatile register long reg asm(str(reg)) = (long)arg;
#define syscall(type, name, args ...) \
    volatile register long n asm(rN) = (long)sys::name; \
    volatile register long r asm(rR); \
    asm volatile(kernel: "=r" (r): "r"(n), ## args : clobber); \
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

//#include "asm/unistd_64.h"
enum class sys : long {
#if __x86_64
    read, write, open, close, stat, fstat, lstat, poll, lseek, mmap, mprotect, munmap, brk, sigaction, ioctl=16, shmget=29, shmat, shmctl,
    socket=41, connect, fork=57, vfork, execve, exit, wait4, kill, shmdt=67, fcntl=72, getdents=78, setpriority=141, mlock=149, setrlimit=160, clock_gettime=228,
    openat=257, mkdirat, unlinkat=263, symlinkat=266, utimensat=280, timerfd_create=283, timerfd_settime=286
#else
    exit=1, fork, read, write, open, close, execve=11, brk=45, ioctl=54, fcntl, setrlimit=75, munmap=91, setpriority=97, socketcall=102,
    ipc = 117, socket=281, connect=283, getdents=141, poll=168, sigaction=174, mmap=192, fstat=197,
#if __arm__
    clock_gettime=263,
    shmat=305,shmdt,shmget,shmctl,
    openat=322, mkdirat, fstatat, unlinkat, symlinkat=331,
    timerfd_create=350, timerfd_settime=353
#elif __i386__
    clock_gettime=265,
    openat=295, mkdirat, fstatat=300, unlinkat, symlinkat=304,
    timerfd_create=322, timerfd_settime=325
#endif
#endif
};

typedef unsigned short uint16;
typedef unsigned int uint;
struct sockaddr { uint16 family; uint16 port; uint ip; int pad[2]; };
struct sockaddr_un { uint16 family=1; char path[108]; };
struct timespec { long sec,nsec; };
struct rlimit { long cur,max; };
#if __x86_64
struct stat { long dev; long ino; long nlink; uint mode,uid,gid; long rdev,size,blksize,blocks; timespec atime,mtime,ctime; long pad[3]; };
#else
typedef unsigned long long uint64;
struct stat { uint64 dev; uint pad1; uint ino; uint mode; uint16 nlink; uint uid,gid; uint64 rdev; uint pad2;
              uint64 size; uint blksize; uint64 blocks; timespec atime,mtime,ctime; uint64 ino64; };
#endif

struct dirent { long ino, off; short len; char name[]; };

enum {O_RDONLY, O_WRONLY, O_RDWR, O_CREAT=0100, O_TRUNC=01000, O_APPEND=02000, O_NONBLOCK=04000,
#if __arm
      O_DIRECTORY=040000
#else
      O_DIRECTORY=0200000
#endif
    };
enum {PROT_READ=1, PROT_WRITE=2};
enum {MAP_FILE, MAP_SHARED, MAP_PRIVATE};
enum {DT_DIR=4, DT_REG=8};
enum {F_SETFL=4};
enum {PF_LOCAL=1, PF_INET};
enum {SOCK_STREAM=1, SOCK_DGRAM};
enum {RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_RSS, RLIMIT_NOFILE, RLIMIT_AS};
enum {IPC_NEW=0, IPC_RMID=0, IPC_CREAT=01000};
enum {CLOCK_REALTIME=0, CLOCK_THREAD_CPUTIME_ID=3};
enum {TFD_CLOEXEC = 02000000};
enum {SIGABRT=6, SIGBUS, SIGFPE, SIGKILL, SIGUSR1, SIGSEGV, SIGUSR2, SIGPIPE, SIGALRM, SIGTERM};

constexpr uint IO(uint major, uint minor) { return major<<8 | minor; }
template<class T> constexpr uint IOW(uint major, uint minor) { return 1<<30 | sizeof(T)<<16 | major<<8 | minor; }
template<class T> constexpr uint IOR(uint major, uint minor) { return 2<<30 | sizeof(T)<<16 | major<<8 | minor; }
template<class T> constexpr uint IOWR(uint major, uint minor) { return 3<<30 | sizeof(T)<<16 | major<<8 | minor; }

syscall3(int, read, int,fd, void*,buf, long,size)
syscall3(int, write, int,fd, const void*,buf, long,size)
syscall3(int, open, const char*,name, int,oflag, int,perms)
syscall1(int, close, int,fd)
syscall2(int, fstat, int,fd, stat*,buf)
syscall3(int, poll, struct pollfd*,fds, long,nfds, int,timeout)
syscall3(int, lseek, int,fd, long,offset, int,whence)

syscall6(void*, mmap, void*,addr, long,len, int,prot, int,flags, int,fd, long,offset)
syscall2(int, munmap, void*,addr, long,len)
syscall1(void*, brk, void*,new_brk)
syscall4(int, sigaction, int,sig, const void*,act, void*,old, int, sigsetsize)

syscall3(int, ioctl, int,fd, long,request, void*,arguments)

#if __i386
syscall6(int, ipc, uint,call, long,first, long,second, long,third, const void*,ptr, long,fifth)
inline long shmat(int id, const void* ptr, int flag) { long addr; return ipc(21,id,flag,(long)&addr,ptr,0)<0 ?: addr; }
inline int shmdt(const void* ptr) { return ipc(22,0,0,0,ptr,0); }
inline int shmget(int key, long size, int flag) { return ipc(23,key,size,flag,0,0); }
inline int shmctl(int id, int cmd, struct shmid_ds* buf) { return ipc(24,id,cmd,0,buf,0); }
#else
syscall3(long, shmat, int,id, const void*,ptr, int,flag)
syscall1(int, shmdt, const void*,ptr)
syscall3(int, shmget, int,key, long,size, int,flag)
syscall3(int, shmctl, int,id, int,cmd, struct shmid_ds*,buf)
#endif

syscall0(int, fork)
syscall3(int, execve, const char*,path, const char**,argv, const char**,envp)
inline __attribute((noreturn)) int exit(int code) {r(r0,code) volatile register long n asm(rN) = (long)sys::exit; asm volatile(kernel:: "r"(n), "r"(r0)); __builtin_unreachable();}
syscall4(int, wait4, int,pid, int*,status, int,options, struct rusage*, rusage)
syscall2(int, kill, int,pid, int,sig)

syscall3(int, fcntl, int,fd, int,cmd, int,param)
syscall3(int, getdents, int,fd, void*,entry, long,size)

syscall4(int, openat, int,fd, const char*,name, int,oflag, int,perms)
syscall3(int, mkdirat, int,fd, const char*,name, int,mode)
syscall3(int, unlinkat, int,fd, const char*,name, int,flag)
syscall3(int, symlinkat, const char*,target, int,fd, const char*,name)
syscall4(int, utimensat, int,fd, const char*,name, const struct timespec*,times, int,flags)

syscall3(int, setpriority, int,which, int,who, int,prio)
syscall2(int, mlock,const void*,addr, long,len)
syscall2(int, setrlimit, int,resource, rlimit*,limit)
syscall2(int, clock_gettime, int,type, timespec*,ts)
syscall2(int, timerfd_create, int,clock_id, int,flags)
syscall4(int, timerfd_settime, int,ufd, int,flags, const timespec*,utmr, timespec*,otmr)

#if __i386
syscall2(int, socketcall, int,call, long*,args)
inline int socket(int domain, int type, int protocol) { long a[]={domain,type,protocol}; return socketcall(1,a); }
inline int connect(int fd, struct sockaddr* addr, int len) { long a[]={fd,(long)addr,len}; return socketcall(3,a); }
#else
syscall3(int, socket, int,domain, int,type, int,protocol)
syscall3(int, connect, int,fd, struct sockaddr*,addr, int,len)
#endif

#undef str
#undef r
#undef rN
#undef rR
#undef r0
#undef r1
#undef r2
#undef r3
#undef r4
#undef r5
#undef kernel
