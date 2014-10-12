#include "thread.h"
#include "string.h"
#include "data.h"
#include "trace.h"
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/resource.h>
#define signal signal_
#include <signal.h>
#undef signal
#include <fcntl.h>
#include <sys/wait.h>
#include <pwd.h>
#include <sys/syscall.h>

// Log
void log_(const string buffer) { check_(write(2, buffer.data, buffer.size)); }
template<> void log(const string& buffer) { log_(buffer+'\n'); }

// Poll
void Poll::registerPoll() {
    Locker lock(thread.lock);
    if(thread.contains(this)) { thread.unregistered.remove(this); return; }
    assert_(!thread.unregistered.contains(this));
    thread.append( this );
    if(thread.tid) thread.post(); // Resets poll to include this new descriptor (FIXME: only if not current)
}
void Poll::unregisterPoll() {
    Locker lock(thread.lock);
    if(thread.contains(this) && !thread.unregistered.contains(this)) thread.unregistered.append(this);
}
void Poll::queue() {Locker lock(thread.lock); if(!thread.queue.contains(this)) thread.queue.append(this); thread.post();}

EventFD::EventFD():Stream(eventfd(0,EFD_SEMAPHORE)){}

// Threads

// Lock access to thread list
static Lock threadsLock __attribute((init_priority(102)));
// Process-wide thread list to trace all threads when one fails and cleanly terminates all threads before exiting
static array<Thread*> threads __attribute((init_priority(102)));
// Handle for the main thread (group leader)
Thread mainThread __attribute((init_priority(102))) (20);
// Flag to cleanly terminate all threads
static bool terminate = false;
// Exit status to return for process (group)
static int exitStatus = 0;

Thread::Thread(int priority) : Poll(EventFD::fd,POLLIN,*this), priority(priority) {
    Locker lock(threadsLock); threads.append(this); // Adds this thread to global thread list
}
void Thread::setPriority(int priority) { setpriority(0,0,priority); }
static void* run(void* thread) { ((Thread*)thread)->run(); return 0; }
void Thread::spawn() { assert(!thread); pthread_create(&thread,0,&::run,this); }

static int gettid() { return syscall(SYS_gettid); }

void Thread::run() {
    tid=gettid();
    if(priority) setpriority(0,0,priority);
    while(!terminate) {
        if(size==1 && !queue && !(this==&mainThread && threads.size>1)) break; // Terminates when no Poll objects are registered (except main)

        pollfd pollfds[size];
        for(uint i: range(size)) pollfds[i]=*at(i); //Copy pollfds as objects might unregister while processing in the loop
        if((LinuxError)check( ::poll(pollfds,size,-1) ) != LinuxError::Interrupted) {
            for(uint i: range(size)) {
                if(terminate) break;
                Poll* poll=at(i); int revents=pollfds[i].revents;
                if(revents && !unregistered.contains(poll)) {
                    poll->revents = revents;
                    poll->event();
                }
            }
        }
        while(unregistered){Locker locker(lock); Poll* poll=unregistered.pop(); remove(poll); queue.tryRemove(poll);}
    }
    Locker lock(threadsLock); threads.remove(this);
    thread = 0;
}

void Thread::event() {
    EventFD::read();
    if(queue){
        Poll* poll;
        {Locker locker(lock);
            poll=queue.take(0);
            if(unregistered.contains(poll)) return;
        }
        poll->revents=IDLE;
        poll->event();
    }
}

// Debugger

static int tgkill(int tgid, int tid, int sig) { return syscall(SYS_tgkill,tgid,tid,sig); }
static void traceAllThreads() {
    Locker lock(threadsLock);
    for(Thread* thread: threads) if(thread->tid!=gettid()) tgkill(getpid(),thread->tid,SIGTRAP); // Logs stack trace of all threads
}
static void handler(int sig, siginfo_t* info, void* ctx) {
    if(sig==SIGSEGV) log("Segmentation fault");
    if(threads.size>1) log("Thread #"+dec(gettid())+':');
    log_(trace(1, (void*) ((ucontext_t*)ctx)->uc_mcontext.gregs[REG_RIP]));
    if(sig!=SIGTRAP) traceAllThreads();
    if(sig==SIGABRT) log("Aborted");
    static constexpr string fpErrors[] = {"", "Integer division", "Integer overflow", "Division by zero", "Overflow",
                                          "Underflow", "Precision", "Invalid", "Denormal"};
    if(sig==SIGFPE) log("Floating-point exception (",fpErrors[info->si_code],")", *(float*)info->si_addr);
    if(sig==SIGSEGV) log("Segmentation fault at "+str(info->si_addr));
    if(sig==SIGTERM) log("Terminated");
    pthread_exit((void*)-1);
}

// Configures floating-point exceptions
void setExceptions(uint except) { int r; asm volatile("stmxcsr %0":"=m"(*&r)); r|=0b111111<<7; r &= ~((except&0b111111)<<7); asm volatile("ldmxcsr %0" : : "m" (*&r)); }

void __attribute((constructor(102))) setup_signals() {
    /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
    struct sigaction sa; sa.sa_sigaction=&handler; sa.sa_flags=SA_SIGINFO|SA_RESTART; sa.sa_mask={{}};
    check_(sigaction(SIGABRT, &sa, 0));
    check_(sigaction(SIGSEGV, &sa, 0));
    check_(sigaction(SIGTERM, &sa, 0));
    check_(sigaction(SIGTRAP, &sa, 0));
    check_(sigaction(SIGFPE, &sa, 0));
    setExceptions(Invalid /*| Denormal*/ | DivisionByZero | Overflow /*| Underflow *//*| Precision*/);
}

static void __attribute((noreturn)) exit_thread(int status) { syscall(SYS_exit, status); __builtin_unreachable(); }

template<> void __attribute((noreturn)) error(const string& message) {
    //log(message); // In case, tracing crashes
    static bool reentrant = false;
    if(!reentrant) { // Avoid hangs if tracing errors
        reentrant = true;
        traceAllThreads();
        if(threads.size>1) log(String("Thread #"+dec(gettid())+':'));
        log_(trace(1,0));
        reentrant = false;
    }
    log(message);
    exit(-1); // Signals all threads to terminate
    {Locker lock(threadsLock);
        for(Thread* thread: threads) if(thread->tid==gettid()) { threads.remove(thread); break; } } // Removes this thread from list
#if !__arm__
      __builtin_trap(); //TODO: detect if running under debugger
#endif
    exit_thread(-1); // Exits this thread
}

static int __attribute((noreturn)) exit_group(int status) { syscall(SYS_exit_group, status); __builtin_unreachable(); }

// Entry point
int main() {
    if(mainThread.size>1 || mainThread.queue || threads.size>1) mainThread.run();
    for(Thread* thread: threads) if(thread->thread) { void* status; pthread_join(thread->thread,&status); } // Waits for all threads to terminate
    return exitStatus; // Destroys all file-scope objects (libc atexit handlers) and terminates using exit_group
}

void exit(int status) {
    exitStatus = status;
    terminate = true;
    Locker lock(threadsLock);
    assert_(threads);
    for(Thread* thread: threads) thread->post();
}

// Locates an executable
String which(string name) {
    if(!name) return {};
    if(existsFile(name)) return String(name);
    for(string folder: split(getenv("PATH","/usr/bin"),":")) if(existsFolder(folder) && existsFile(name, folder)) return folder+'/'+name;
    return {};
}

int execute(const string path, const ref<string> args, bool wait, const Folder& workingDirectory) {
    if(!existsFile(path)) { error("Executable not found",path); return -1; }

    array<String> args0(1+args.size);
    args0.append( path+'\0' );
    for(const auto& arg: args) args0.append( arg+'\0' );
    const char* argv[args0.size+1];
    for(uint i: range(args0.size)) argv[i] = args0[i].data;
    argv[args0.size]=0;

    array<string> env0;
    static String environ = File("/proc/self/environ").readUpTo(4096);
    for(TextData s(environ);s;) env0.append( s.until('\0') );

    const char* envp[env0.size+1];
    for(uint i: range(env0.size)) envp[i]=env0[i].data;
    envp[env0.size]=0;

    int cwd = workingDirectory.fd;
    int pid = fork();
    if(pid==0) {
        if(cwd!=AT_FDCWD) check_(fchdir(cwd));
        if(!execve(strz(path), (char*const*)argv, (char*const*)envp)) exit_group(-1);
        __builtin_unreachable();
    }
    else if(wait) return ::wait(pid);
    else { wait4(pid,0,WNOHANG,0); return pid; }
}
int wait() { return wait4(-1,0,0,0); }
int64 wait(int pid) { void* status=0; wait4(pid,&status,0,0); return (int64)status; }

string getenv(const string name, string value) {
    static String environ = File("/proc/self/environ").readUpTo(8192);
    for(TextData s(environ);s;) {
        string key=s.until('='); string value=s.until('\0');
        if(key==name) return value;
    }
    return value;
}

array<string> arguments() {
    static String cmdline = File("/proc/self/cmdline").readUpTo(4096);
    assert(cmdline.size<4096);
    return split(section(cmdline,0,1,-1),"\0");
}

const Folder& home() { static Folder home(getenv("HOME",str((const char*)getpwuid(geteuid())->pw_dir))); return home; }
