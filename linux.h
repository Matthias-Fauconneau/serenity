#pragma once

#if __arm__
#define r0 r0
#define r1 r1
#define r2 r2
#define r3 r3
#define r4 r4
#define r5 r5
#define syscall(type, name, args...) reg(r7,sys::name); asm volatile("swi 0":"=r"(r0):"r"(r7),##args); return (type)r0;
#elif __x86_64__ || __i386__
#define r0 ebx
#define r1 ecx
#define r2 edx
#define r3 esi
#define r4 edi
#define r5 ebp
#define syscall(type, name, args ...) reg(eax,sys::name); asm volatile("int $0x80":"=r"(eax):"r"(eax),##args); return (type)eax;
#else
#error Unsupported architecture
#endif
#define reg(r,a) register long r asm(#r) = (long)a
#define syscall0(type,name) \
    inline type name() \
{reg(r0,0);syscall(type, name)}
#define syscall1(type,name,type0,arg0) \
    inline type name(type0 arg0) \
{reg(r0,arg0);syscall(type, name, "0"(r0))}
#define syscall2(type,name,type0,arg0,type1,arg1) \
    inline type name(type0 arg0, type1 arg1) \
{reg(r0,arg0);reg(r1,arg1);syscall(type, name, "0"(r0), "r"(r1))}
#define syscall3(type,name,type0,arg0,type1,arg1,type2,arg2) \
    inline type name(type0 arg0, type1 arg1, type2 arg2) \
{reg(r0,arg0);reg(r1,arg1);reg(r2,arg2);syscall(type, name, "0"(r0), "r"(r1), "r"(r2))}
#define syscall4(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3) \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3) \
{reg(r0,arg0);reg(r1,arg1);reg(r2,arg2);reg(r3,arg3);syscall(type, name, "r"(r0), "r"(r1), "r"(r2), "r"(r3))}
#define syscall5(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4) \
{reg(r0,arg0);reg(r1,arg1);reg(r2,arg2);reg(r3,arg3);reg(r4,arg4);syscall(type, name, "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4))}
#define syscall6(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5) \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{reg(r0,arg0);reg(r1,arg1);reg(r2,arg2);reg(r3,arg3);reg(r4,arg4);reg(r5,arg5);syscall(type, name, "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5))}

enum class sys {
    exit=1, fork, read, write, open, close, execve=11, brk=45, ioctl=54, munmap=91, setpriority=97, socketcall=102,
    getdents=141, poll=168, sigaction=174, mmap=192, fstat=197,
#if __arm__
    socket=281, connect=283, openat=322, mkdirat, fstatat, unlinkat, symlinkat=331
#elif __x86_64__ || __i386__
    openat=295, mkdirat, fstatat=300, unlinkat, symlinkat=304
#endif
};

syscall0(int, fork)
syscall3(int, read, int,fd, void*,buf, long,size)
syscall3(int, write, int,fd, const void*,buf, long,size)
syscall3(int, open, const char*,name, int,oflag, int,perms)
syscall1(int, close, int,fd)
syscall3(int, execve, const char*,path, const char**,argv, const char**,envp)
syscall1(void*, brk, void*,new_brk)
syscall3(int, ioctl, int,fd, long,request, void*,buf)
syscall2(int, munmap, void*,addr, long,len)
syscall3(int, setpriority, int,which, int,who, int,prio)
syscall3(int, getdents, int,fd, struct dirent*,entry, long,size)
syscall3(int, poll, struct pollfd*,fds, long,nfds, int,timeout)
syscall4(int, sigaction, int,sig, const void*,act, struct action*,old, int, sigsetsize)
syscall6(void*, mmap, void*,addr, long,len, int,prot, int,flags, int,fd, long,offset)
syscall2(int, fstat, int,fd, struct stat*,buf)
syscall4(int, openat, int,fd, const char*,name, int,oflag, int,perms)
syscall3(int, mkdirat, int,fd, const char*,name, int,mode)
syscall4(int, fstatat, int,fd, const char*,name, struct stat*,buf, int,flag)
syscall3(int, unlinkat, int,fd, const char*,name, int,flag)
syscall3(int, symlinkat, const char*,target, int,fd, const char*,name)

#include "asm/unistd.h"
#if __i386__
syscall2(int, socketcall, int,call, long*,args)
inline int socket(int domain, int type, int protocol) { long a[]={domain,type,protocol}; return socketcall(1,a); }
inline int connect (int fd, struct sockaddr* addr, int len) { long a[]={fd,(long)addr,len}; return socketcall(3,a); }
#else
syscall3(int, socket, int,domain, int,type, int,protocol)
syscall3(int, connect, int,fd, struct sockaddr*,addr, int,len)
#endif

enum { O_RDONLY, O_WRONLY, O_RDWR, O_CREAT=0100, O_TRUNC=01000, O_APPEND=02000, O_NONBLOCK=04000, O_DIRECTORY=
#if __arm__
            040000
#else
            0200000
#endif
};
enum { PROT_READ=1, PROT_WRITE };
enum { MAP_SHARED=1, MAP_PRIVATE };

enum { SOCK_STREAM=1 };
enum { AF_UNIX=1, PF_UNIX=1 };
struct sockaddr_un { short family=AF_UNIX; unsigned char zero=0; unsigned char path[108]; };

#pragma GCC system_header
int exit(int code) __attribute((noreturn));
syscall1(int, exit, int,code)

#undef r0
#undef r1
#undef r2
#undef r3
#undef r4
#undef r5
#undef reg
#undef syscall
#undef syscall0
#undef syscall1
#undef syscall2
#undef syscall3
#undef syscall4
#undef syscall5
#undef syscall6
