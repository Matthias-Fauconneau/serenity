#pragma once

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
#define clobber "rcx", "r11"
#define rR "rax"
#elif __arm
#define attribute
#define rN r7
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
    asm volatile(kernel: "=r" (r): "r"(rN), ## args : clobber); \
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

enum class sys : long {
#if __x86_64
    read, write, open, close, stat, fstat, lstat, poll, lseek, mmap, mprotect, munmap, brk, sigaction, sigprocmask, ioctl=16, sched_yield=24, shmget=29, shmat, shmctl, pause=34,
    getpid=39, socket=41, connect, clone=56, fork, execve=59, exit, wait4, kill, shmdt=67, fcntl=72, getdents=78, setpriority=141, mlock=149, mlockall=151, setrlimit=160,
    gettid=186, futex=202, clock_gettime=228, exit_group=231, tgkill=234, openat=257, mkdirat, unlinkat=263, symlinkat=266, utimensat=280, timerfd_create=283,
    timerfd_settime=286, eventfd2=290
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

syscall3(int, read, int,fd, void*,buf, long,size)
syscall3(int, write, int,fd, const void*,buf, long,size)
syscall3(int, open, const char*,name, int,oflag, int,perms)
syscall1(int, close, int,fd)
syscall2(int, fstat, int,fd, struct stat*,buf)
syscall3(int, poll, struct pollfd*,fds, long,nfds, int,timeout)
syscall3(int, lseek, int,fd, long,offset, int,whence)
enum {PROT_READ=1, PROT_WRITE=2}; enum {MAP_FILE, MAP_SHARED, MAP_PRIVATE, MAP_ANONYMOUS=0x20}; //FIXME
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
syscall0(int, pause)
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
syscall2(int, kill, int,pid, int,sig)
syscall3(int, fcntl, int,fd, int,cmd, int,param)
syscall3(int, getdents, int,fd, void*,entry, long,size)
syscall3(int, setpriority, int,which, int,who, int,prio)
syscall2(int, mlock,const void*,addr, long,len)
syscall1(int, mlockall, int,flags)
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

inline __attribute((noreturn)) int exit(int status) {r(r0,status) r(rN,sys::exit); asm volatile(kernel:: "r"(rN), "r"(r0)); __builtin_unreachable();}
inline __attribute((noreturn)) int exit_group(int status) {r(r0,status)  r(rN,sys::exit_group); asm volatile(kernel:: "r"(rN), "r"(r0)); __builtin_unreachable();}
inline __attribute((noinline)) long clone(int (*fn)(void*), void* stack, int flags, void* arg) {
    r(r0,flags) r(r1,0) r(r2,0) r(r3,0) r(r4,0) r(rN,sys::clone);
    register long r13 asm("r13")=(long)stack;
    register long r14 asm("r14")=(long)arg;
    register long r15 asm("r15")=(long)fn;
    register long r asm(rR);
    asm volatile("syscall": "=r"(r): "r"(arg),"r"(fn), "r"(rN), "r"(r0), "r"(r1), "r"(r2), "r"(r3), "r"(r4) : "rcx", "r9", "r11");
    if(r!=0) return r;
    asm("movq %0, %%rsp; movq $0, %%rbp; movq %1, %%rdi; call *%2; movq %%rax, %%rdi; call exit":: "r"(r13), "r"(r14),"r"(r15):/*"rbp",*/"rsp","rdi","rax");
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
