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
#define syscall1_nr(type,name,type0,arg0) inline type name(type0 arg0) {  \
    register long index __asm("r7") = (long)sys::name; \
    register long r0 __asm("r0") = (long)arg0; \
    __asm __volatile("swi\t0" : "=r" (r0) : "r" (index), "0" (r0) ); \
    while(*(volatile byte*)0 || 1) {}; \
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

enum class sys { exit=1, fork, read, write, open, close, execve=11, brk=45, ioctl=54, munmap=91, setpriority=97, sigaction=134,
                 getdents=141, poll=168, mmap=192, fstat=197, openat=322, mkdirat, fstatat, unlinkat, symlinkat=331 };

int exit(int code) __attribute((noreturn));
syscall1_nr(int, exit, int,code)
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
syscall3(int, sigaction, int,sig, const struct action*,act, struct action*,old)
syscall3(int, getdents, int,fd, struct dirent*,entry, long,size)
syscall3(int, poll, struct pollfd*,fds, long,nfds, int,timeout)
syscall6(void*, mmap, void*,addr, long,len, int,prot, int,flags, int,fd, long,offset)
syscall2(int, fstat, int,fd, struct stat*,buf)
syscall4(int, openat, int,fd, const char*,name, int,oflag, int,perms)
syscall3(int, mkdirat, int,fd, const char*,name, int,mode)
syscall4(int, fstatat, int,fd, const char*,name, struct stat*,buf, int,flag)
syscall3(int, unlinkat, int,fd, const char*,name, int,flag)
syscall3(int, symlinkat, const char*,target, int,fd, const char*,name)

enum { O_RDONLY, O_WRONLY, O_RDWR, O_CREAT=0100, O_TRUNC=01000, O_APPEND=02000, O_NONBLOCK=04000, O_DIRECTORY=040000 };
enum { PROT_READ=1, PROT_WRITE };
enum { MAP_SHARED=1, MAP_PRIVATE };
