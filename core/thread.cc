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
#include <sys/syscall.h>

// Log
void log_(string message) { ssize_t unused written = ::write(2, message.data, message.size); }
void log(string message) { log_(message+'\n');  }

// Poll
void Poll::registerPoll() {
 Locker lock(thread);
 if(thread.contains(this)) { thread.unregistered.tryRemove(this); return; }
 assert_(!thread.unregistered.contains(this));
 thread.append(this);
 if(thread.tid) thread.post(); // Resets poll to include this new descriptor (FIXME: only if not current)
}
void Poll::unregisterPoll() {
 Locker lock(thread);
 if(thread.contains(this) && !thread.unregistered.contains(this)) thread.unregistered.append(this);
}
void Poll::queue() {Locker lock(thread); if(!thread.queue.contains(this)) thread.queue.append(this); thread.post();}

EventFD::EventFD() : Stream(check(eventfd(0, EFD_SEMAPHORE))) {}

// Threads

// Lock access to thread list
static Lock threadsLock __attribute((init_priority(102)));
// Process-wide thread list to trace all threads when one fails and cleanly terminates all threads before exiting
static array<Thread*> threads __attribute((init_priority(102)));
// Handle for the main thread (group leader)
Thread mainThread __attribute((init_priority(102))) (0);
// Flag to cleanly terminate all threads
static bool terminationRequested = false;
// Exit status to return for process (group)
int groupExitStatus = 0;

//generic T* addressOf(T& arg)  { return reinterpret_cast<T*>(&const_cast<char&>(reinterpret_cast<const volatile char&>(arg))); }
Thread::Thread(int priority, bool spawn) : Poll(0,POLLIN,*this), priority(priority) {
 Poll::fd = EventFD::fd; registerPoll();
 //if(this == addressOf(mainThread)) tid = gettid();
#if !NO_PTHREAD
 if(spawn) this->spawn();
#endif
}
void Thread::setPriority(int priority) { setpriority(0,0,priority); }
#if !NO_PTHREAD
static void* run(void* thread) { ((Thread*)thread)->run(); return 0; }
void Thread::spawn() { assert_(!thread); pthread_create(&thread,0,&::run,this); }
#endif

int32 gettid() { return syscall(SYS_gettid); }

void Thread::run() {
 tid=gettid();
 if(priority) setpriority(0,0,priority);
 {Locker lock(threadsLock); threads.append(this);} // Adds this thread to global running thread list
 while(!::terminationRequested && !this->terminationRequested) {
  assert_(size>=1);
  if(size==1 && !queue) break; // Terminates if no Poll objects (except thread queue EventFD) are registered and no job is queued)
  while(unregistered){Locker locker(*this); Poll* poll=unregistered.pop(); remove(poll); queue.tryRemove(poll);}

  pollfd pollfds[size];
  for(uint i: range(size)) pollfds[i]=*at(i); //Copy pollfds as objects might unregister while processing in the loop
  if((LinuxError)check( ::poll(pollfds,size,-1) ) != LinuxError::Interrupted) {
   for(uint i: range(size)) {
    if(::terminationRequested || this->terminationRequested) break;
    Poll* poll=at(i); int revents=pollfds[i].revents;
    if(revents && !unregistered.contains(poll)) {
     poll->revents = revents;
     Locker lock(runLock);
     poll->event();
    }
   }
  }
 }
 {Locker lock(threadsLock); threads.remove(this);} // Removes this thread from global running thread list
 tid = 0; thread = 0;
}

void Thread::event() {
 EventFD::read();
 if(queue){
  Poll* poll;
  {Locker locker(*this);
   poll=queue.take(0);
   if(unregistered.contains(poll)) return;
  }
  poll->revents=IDLE;
  poll->event();
 }
}

#if !NO_PTHREAD
void Thread::wait() {
 if(!thread) return;
 terminationRequested = true;
 post();
 void* status; pthread_join(thread, &status);
 terminationRequested = false;
 queue.clear();
}
#endif

// Debugger

static int tgkill(int tgid, int tid, int sig) { return syscall(SYS_tgkill,tgid,tid,sig); }
static void traceAllThreads() {
 Locker lock(threadsLock);
 for(Thread* thread: threads) if(thread->tid!=gettid()) tgkill(getpid(),thread->tid,SIGTRAP); // Logs stack trace of all threads
}
static void handler(int sig, siginfo_t* info, void* ctx) {
 if(sig==SIGSEGV) log_("Segmentation fault\n");
 else if(sig==SIGFPE) log("Floating-point exception\n"_);
 else if(sig==SIGABRT) log_("Aborted\n");
 else if(sig==SIGTERM) log_("Terminated\n");
 else if(sig==SIGTRAP) { log_("Trapped\n"); exit_group(-1); }
 else { log_("Unknown signal "); log_(/*str(sig)*/string(&"0123456789"[sig], 1)); log_("\n"); }
 if(threads.size>1) log("Thread #"+str(gettid())+':');
#if __x86_64
 log(trace(1, (void*) ((ucontext_t*)ctx)->uc_mcontext.gregs[REG_RIP]));
#else
 log(trace(1, (void*) ((ucontext_t*)ctx)->uc_mcontext.gregs[REG_EIP]));
#endif
 if(sig!=SIGTRAP) traceAllThreads();
 static constexpr string fpErrors[] = {"", "Integer division", "Integer overflow", "Division by zero", "Overflow",
                                       "Underflow", "Precision", "Invalid", "Denormal"};
 if(sig==SIGFPE) log("Floating-point exception (",fpErrors[info->si_code],")", *(float*)info->si_addr);
 if(sig==SIGSEGV) log("Segmentation fault at "+str(info->si_addr));
 if(sig==SIGTERM) log("Terminated");
 pthread_exit((void*)-1);
}

// Configures floating-point exceptions
void setExceptions(uint except) {
 int r; asm volatile("stmxcsr %0":"=m"(*&r));
 r|=0b111111<<7; r &= ~((except&0b111111)<<7);
 asm volatile("ldmxcsr %0" : : "m" (*&r));
}

void __attribute((constructor(102))) setup_signals() {
 //{rlimit limit; getrlimit(RLIMIT_CORE,&limit); limit.rlim_cur=0; setrlimit(RLIMIT_CORE,&limit);}
 /// Setup signal handlers to log trace on {ABRT,SEGV,TERM,PIPE}
 struct sigaction sa; sa.sa_sigaction=&handler; sa.sa_flags=SA_SIGINFO|SA_RESTART; sa.sa_mask={{}};
 check(sigaction(SIGABRT, &sa, 0));
 check(sigaction(SIGSEGV, &sa, 0));
 check(sigaction(SIGTERM, &sa, 0));
 check(sigaction(SIGTRAP, &sa, 0));
 check(sigaction(SIGFPE, &sa, 0));
 //check(sigaction(SIGUSR1, &sa, 0));
 enum { Invalid=1<<0, Denormal=1<<1, DivisionByZero=1<<2, Overflow=1<<3, Underflow=1<<4, Precision=1<<5 };
 //setExceptions(/*Invalid|*//*Denormal|*//*DivisionByZero|*//*Overflow*//*|Underflow*//*|Precision*/);
}

//static void __attribute((noreturn)) exit_thread(int status) { syscall(SYS_exit, status); __builtin_unreachable(); }
int __attribute((noreturn)) exit_group(int status) { syscall(SYS_exit_group, status); __builtin_unreachable(); }

template<> void __attribute((noreturn)) error(const string& message) {
 log(message);
 static bool reentrant = false;
 if(!reentrant) { // Avoid hangs if tracing errors
  reentrant = true;
  traceAllThreads();
  if(threads.size>1) log("Thread #"+str(gettid())+':');
  log(trace(1,0));
  reentrant = false;
 }
 log(message);
 __builtin_trap(); //TODO: detect if running under debugger
 exit_group(-1); // Exits this group (process)
}

// Entry point
int argc; char** argv;
int main(int argc, char** argv) {
 ::argc = argc, ::argv = argv;
 //mainThread.tid=gettid(); // ->Thread
 /*unique<Application> application;
 Interface<Application>::AbstractFactory* factory = Interface<Application>::factories().value("");
 for(string argument: arguments())
  if(Interface<Application>::factories().contains(argument)) factory = Interface<Application>::factories().at(argument);
 if(factory) application = factory->constructNewInstance();*/
 mainThread.run(); // Reuses main thread as default event loop runner when not overriden in Poll constructor
 return groupExitStatus; // Destroys all file-scope objects (libc atexit handlers) and terminates using exit_group
}

void requestTermination(int status) {
 if(status) groupExitStatus = status;
 terminationRequested = true;
 Locker lock(threadsLock);
 assert_(threads);
 for(Thread* thread: threads) thread->post();
}

// Locates an executable
String which(string name) {
 if(!name) return {};
 if(existsFile(name)) return copyRef(name);
 for(string folder: split(environmentVariable("PATH","/usr/bin"),":")) if(existsFolder(folder) && existsFile(name, folder)) return folder+'/'+name;
 return {};
}

int execute(const string path, const ref<string> args, bool wait, const Folder& workingDirectory, Handle* stdout, Handle* stderr) {
 //log(path, args);
 if(!existsFile(path)) { error("Executable not found",path); return -1; }

 buffer<String> args0(1+args.size, 0);
 args0.append( path+'\0' );
 for(const auto& arg: args) args0.append( arg+'\0' );
 const char* argv[args0.size+1];
 for(uint i: range(args0.size)) argv[i] = args0[i].data;
 argv[args0.size]=0;

 array<string> env0;
 static String environ = File("/proc/self/environ").readUpTo(16384);
 assert_(environ.size < environ.capacity);
 for(TextData s(environ);s;) env0.append( s.until('\0') );

 const char* envp[env0.size+1];
 for(uint i: range(env0.size)) envp[i]=env0[i].data;
 envp[env0.size]=0;

 int pipeOut[2], pipeErr[2];
 if(stdout) check( ::pipe(pipeOut) );
 if(stderr) check( ::pipe(pipeErr) );

 int cwd = workingDirectory.fd;
 int pid = fork();
 if(pid==0) {
  if(stdout) {
   close(pipeOut[0]); // Child does not read
   dup2(pipeOut[1], 1); // Redirect stdout to pipe
  }
  if(stderr) {
   close(pipeErr[0]); // Child does not read
   dup2(pipeErr[1], 2); // Redirect stderr to pipe
  }
  if(cwd!=AT_FDCWD) check(fchdir(cwd));
  if(!execve(strz(path), (char*const*)argv, (char*const*)envp)) exit_group(-1);
  __builtin_unreachable();
 } else {
  if(stdout) {
   close(pipeOut[1]); // Parent does not write
   stdout->fd = pipeOut[0];
  }
  if(stderr) {
   close(pipeErr[1]); // Parent does not write
   stderr->fd = pipeErr[0];
  }
  if(wait) return ::wait(pid);
  else { isRunning(pid); return pid; }
 }
}
int wait() { return waitpid(-1,0,0); }
int wait(int pid) { int status=0; waitpid(pid,&status,0); return status; }
bool isRunning(int pid) { int status=0; waitpid(pid,&status,WNOHANG); return (status&0x7f); }
