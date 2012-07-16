#pragma once

#define syscall0(type,name) inline type name() {  \
    register long index __asm("r7") = (long)sys::name; \
    register long r0 __asm("r0"); \
    __asm __volatile("swi\t0" : "=r" (r0) : "r" (index) ); \
    return (type)r0; \
}
#define syscall1(type,name,type0,arg0) inline type name(type0 arg0) {  \
    register long index __asm("r7") = (long)sys::name; \
    register long r0 __asm("r0") = (long)arg0; \
    __asm __volatile("swi\t0" : "=r" (r0) : "r" (index), "0" (r0) ); \
    return (type)r0; \
}
#define syscall2(type,name,type0,arg0,type1,arg1) inline type name(type0 arg0, type1 arg1) {  \
    register long index __asm("r7") = (long)sys::name; \
    register long r0 __asm("r0") = (long)arg0; \
    register long r1 __asm("r1") = (long)arg1; \
    __asm __volatile("swi\t0" : "=r" (r0) : "r" (index), "0" (r0), "r" (r1) ); \
    return (type)r0; \
}
#define syscall3(type,name,type0,arg0,type1,arg1,type2,arg2) inline type name(type0 arg0, type1 arg1, type2 arg2) {  \
    register long index __asm("r7") = (long)sys::name; \
    register long r0 __asm("r0") = (long)arg0; \
    register long r1 __asm("r1") = (long)arg1; \
    register long r2 __asm("r2") = (long)arg2; \
    __asm __volatile("swi\t0" : "=r" (r0) : "r" (index), "0" (r0), "r" (r1), "r" (r2) ); \
    return (type)r0; \
}
#define syscall4(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3) \
inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3) { \
    register long index __asm("r7") = (long)sys::name; \
    register long r0 __asm("r0") = (long)arg0; \
    register long r1 __asm("r1") = (long)arg1; \
    register long r2 __asm("r2") = (long)arg2; \
    register long r3 __asm("r3") = (long)arg3; \
    __asm __volatile("swi\t0" : "=r" (r0) : "r" (index), "0" (r0), "r" (r1), "r" (r2), "r" (r3) ); \
    return (type)r0; \
}
#define syscall5(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4) \
inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4) { \
    register long index __asm("r7") = (long)sys::name; \
    register long r0 __asm("r0") = (long)arg0; \
    register long r1 __asm("r1") = (long)arg1; \
    register long r2 __asm("r2") = (long)arg2; \
    register long r3 __asm("r3") = (long)arg3; \
    register long r4 __asm("r4") = (long)arg4; \
    __asm __volatile("swi\t0" : "=r" (r0) : "r" (index), "0" (r0), "r" (r1), "r" (r2), "r" (r3), "r" (r4) ); \
    return (type)r0; \
}
#define syscall6(type,name,type0,arg0,type1,arg1,type2,arg2,type3,arg3,type4,arg4,type5,arg5) \
inline type name(type0 arg0,type1 arg1,type2 arg2,type3 arg3,type4 arg4,type5 arg5) { \
    register long index __asm("r7") = (long)sys::name; \
    register long r0 __asm("r0") = (long)arg0; \
    register long r1 __asm("r1") = (long)arg1; \
    register long r2 __asm("r2") = (long)arg2; \
    register long r3 __asm("r3") = (long)arg3; \
    register long r4 __asm("r4") = (long)arg4; \
    register long r5 __asm("r5") = (long)arg5; \
    __asm __volatile("swi\t0" : "=r" (r0) : "r" (index), "0" (r0), "r" (r1), "r" (r2), "r" (r3), "r" (r4), "r" (r5) ); \
    return (type)r0; \
}

enum class sys { exit=1, fork, read, write, open, close, execve=11, brk=45, ioctl=54, fcntl, setrlimit=75, munmap=91, setpriority=97,
                 socket=281, connect=283, getdents=141, poll=168, sigaction=174, mmap=192, fstat=197, clock_gettime=263, openat=322,
                 mkdirat, unlinkat, symlinkat=331, timerfd_create=350, timerfd_settime=353 };

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
syscall3(int, getdents, int,fd, struct dirent*,entry, long,size)
syscall3(int, poll, struct pollfd*,fds, long,nfds, int,timeout)
syscall4(int, sigaction, int,sig, const void*,act, struct action*,old, int, sigsetsize)
syscall6(void*, mmap, void*,addr, long,len, int,prot, int,flags, int,fd, long,offset)
syscall2(int, fstat, int,fd, struct stat*,buf)
syscall2(int, clock_gettime, int,type, struct timespec*,ts);
syscall4(int, openat, int,fd, const char*,name, int,oflag, int,perms)
syscall3(int, mkdirat, int,fd, const char*,name, int,mode)
syscall3(int, unlinkat, int,fd, const char*,name, int,flag)
syscall3(int, symlinkat, const char*,target, int,fd, const char*,name)
syscall3(int, socket, int,domain, int,type, int,protocol)
syscall3(int, connect, int,fd, struct sockaddr*,addr, int,len)
syscall2(int, timerfd_create, int,clock_id, int,flags);
syscall4(int, timerfd_settime, int,ufd, int,flags, struct timespec*,utmr, struct timespec*,otmr);
syscall2(int, setrlimit, int,resource, struct rlimit*,limit)
enum {O_RDONLY, O_WRONLY, O_RDWR, O_CREAT=0100, O_TRUNC=01000, O_APPEND=02000, O_NONBLOCK=04000, O_DIRECTORY=040000};
enum {PROT_READ=1, PROT_WRITE};
enum {MAP_SHARED=1, MAP_PRIVATE};
struct timespec { ulong sec,nsec; };

#pragma GCC system_header
int exit(int code) __attribute((noreturn));
syscall1(int, exit, int,code)
