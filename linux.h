#pragma once

#if __arm__
#define NR "7"
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

#define syscall(type, name, args ...) long res; __asm __volatile(kernel : rA (res) : NR (sys::name), ## args ); return (type)res;
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
    type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) __attribute((optimize("-fomit-frame-pointer"))); \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) { \
    register long r5 __asm("ebp") = (long)arg5; \
    syscall(type, name, r0 ((long)arg0), r1 ((long)arg1), r2 ((long)arg2), r3 ((long)arg3), r4 ((long)arg4), "r" (r5)) \
}
#else
#define syscall6(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5) \
    inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) \
{syscall(type, name, r0 ((long)arg0), r1 ((long)arg1), r2 ((long)arg2), r3 ((long)arg3), r4 ((long)arg4), r5 ((long)arg5))}
#endif

enum class sys : long {
    exit=1, fork, read, write, open, close, execve=11, brk=45, ioctl=54, munmap=91, setpriority=97,
    getdents=141, poll=168, sigaction=174, mmap=192, fstat=197,
#if __arm__
    openat=322, mkdirat, fstatat, unlinkat, symlinkat=331
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

enum { O_RDONLY, O_WRONLY, O_RDWR, O_CREAT=0100, O_TRUNC=01000, O_APPEND=02000, O_NONBLOCK=04000, O_DIRECTORY=0200000/*040000*/ };
enum { PROT_READ=1, PROT_WRITE };
enum { MAP_SHARED=1, MAP_PRIVATE };

#pragma GCC system_header
int exit(int code) __attribute((noreturn));
syscall1(int, exit, int,code)

#undef NR
#undef rA
#undef r0
#undef r1
#undef r2
#undef r3
#undef r4
#undef r5
