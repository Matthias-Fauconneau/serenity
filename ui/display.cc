#include "display.h"
#include "x.h"
#include <sys/socket.h>

using namespace X11;

static String str(X11::Event::Error e) {
 uint8 code = e.code;
 ref<string> requests (X11::requests);
 ref<string> errors (X11::errors);
 uint16 request = e.minor;
 /***/ if(e.major==DRI3::EXT) {
  requests = ref<string>(DRI3::requests);
 } else request = e.major;
 return str(request<requests.size?requests[request]:str(request), code<errors.size?errors[code]:str(code));
}

template<> String str(const X11::Event& e) {
 if(e.type==X11::Error) return str(e.error);
 else error(e.type);
}

// Globals
namespace Shm { int EXT, event, errorBase; }
namespace DRI3 { int EXT; }
namespace Present { int EXT; }
namespace XRender { int EXT, errorBase; }

bool XDisplay::hasServer() {
 return existsFile("/tmp/.X11-unix/X"+environmentVariable("DISPLAY",":0").slice(1,1));
}

#if GL
extern "C" {
_XDisplay* XOpenDisplay(const char*);
struct XVisualInfo { struct Visual* visual; ulong visualid; int screen, depth, cClass; ulong redMask, greenMask, blueMask; int colormapSize, bitsPerRGB; };
int glXGetConfig(_XDisplay*, XVisualInfo* vis, int attrib, int* value);
}
static bool isGLAlphaVisual(_XDisplay* glDisplay, VisualType visualType, XDepth depth) {
    XVisualInfo visualInfo {0, visualType.id, 0, depth.depth, 0, 0,0,0,0,0};
    enum { GLX_USE_GL=1, GLX_ALPHA_SIZE=11 };
    int USE_GL; glXGetConfig(glDisplay, &visualInfo, GLX_USE_GL, &USE_GL);
    int ALPHA_SIZE; glXGetConfig(glDisplay, &visualInfo, GLX_ALPHA_SIZE, &ALPHA_SIZE);
    return USE_GL && ALPHA_SIZE==8;
}
#endif

XDisplay::XDisplay(Thread& thread) : Socket(PF_LOCAL, SOCK_STREAM), Poll(Socket::fd,POLLIN,thread) {
 {String path = "/tmp/.X11-unix/X"+environmentVariable("DISPLAY",":0").slice(1,1);
  struct sockaddr_un { uint16 family=1; char path[108]={}; } addr; mref<char>(addr.path,path.size).copy(path);
  if(check(connect(Socket::fd, (const sockaddr*)&addr,2+uint(path.size)), path)) error("X connection failed"); }
 {ConnectionSetup r;
  if(existsFile(".Xauthority",home()) && File(".Xauthority",home()).size()) {
   BinaryData s (readFile(".Xauthority",home()), true);
   string name, data;
   unused uint16 family = s.read();
   {uint16 length = s.read(); unused string host = s.read<byte>(length); }
   {uint16 length = s.read(); unused string port = s.read<byte>(length); }
   {uint16 length = s.read(); name = s.read<byte>(length); r.nameSize=uint16(name.size); }
   {uint16 length = s.read(); data = s.read<byte>(length); r.dataSize=uint16(data.size); }
   write(raw(r)+pad(name)+pad(data));
  } else write(raw(r));
 }
 {unused ConnectionSetupReply1 r=read<ConnectionSetupReply1>(); assert(r.status==1);}
 {ConnectionSetupReply2 r=read<ConnectionSetupReply2>();
  read(align(4,r.vendorLength));
  read<XFormat>(r.numFormats);
#if GL
  glDisplay = XOpenDisplay(strz(environmentVariable("DISPLAY"_,":0"_)));
#endif
  for(auto_: range(r.numScreens)){ Screen screen=read<Screen>();
      for(auto_: range(screen.numDepths)) { XDepth depth = read<XDepth>();
          buffer<VisualType> visualTypes = read<VisualType>(depth.numVisualTypes);
          if(visual) continue; // Visual already found, cannot break as we still need to parse to end of variable length structure
          if(depth.depth!=32) continue;
          for(VisualType visualType: visualTypes) {
              if(visualType.class_ != 4) continue;
#if GL
              //log(visualType.id);
              if(!isGLAlphaVisual(glDisplay, visualType, depth)) continue;
#endif
              root = screen.root;
              visual = visualType.id;
              size = uint2(screen.width, screen.height);
              break;
          }
      }
  }
  id=r.ridBase;
  minKeyCode=r.minKeyCode; maxKeyCode=r.maxKeyCode;
  assert(visual);
 }

 {QueryExtension r; auto re = request(({ r.length="MIT-SHM"_.size; r.size=uint16(2+align(4,"MIT-SHM"_.size)/4); r;}), "MIT-SHM"_);
  Shm::EXT=re.major; Shm::event=re.firstEvent; Shm::errorBase=re.firstError; assert_(Shm::EXT); }
 {auto r = request(({QueryExtension r; r.length="Present"_.size; r.size=uint16(2+align(4,"Present"_.size)/4); r;}), "Present"_);
        Present::EXT=r.major; assert_(Present::EXT); }
}

void XDisplay::event() {
 for(;;) { // Process any pending events
  for(;;) { // Process any queued events
   array<byte> e;
   {Locker lock(this->lock);
    if(!xEvents) break;
    e = xEvents.take(0);
   }
   event(e);
  }
  array<byte> o;
  if(!poll()) break;
  {Locker lock(this->lock);
   X11::Event e = read<X11::Event>();
   if(e.type==Error) { error(e); continue; }
   o.append(raw(e));
   if(e.type==GenericEvent) o.append( read(e.genericEvent.size*4) );
  }
  event(o);
 }
}

void XDisplay::event(const ref<byte> ge) {
 const X11::Event& e = *(X11::Event*)ge.data;
 uint8 type = e.type&0b01111111; //msb set if sent by SendEvent
 if(type==KeyPress) {
  function<void()>* action = actions.find( keySym(e.key, e.state) );
  if(action) { (*action)(); return; } // Global window action
 }
 onEvent(ge);
}

uint16 XDisplay::send(ref<byte> data, int fd) {
 if(fd==-1) write(data);
 else {
  iovec iov {.iov_base = (byte*)data.data, .iov_len = data.size};
  union { cmsghdr header; char control[CMSG_SPACE(sizeof(int))]; } cmsgu;
  msghdr msg{.msg_name=0, .msg_namelen=0, .msg_iov=&iov, .msg_iovlen=1, .msg_control=&cmsgu, .msg_controllen = sizeof(cmsgu.control), .msg_flags=0};
  /*if(fd==-1) { msg.msg_control = NULL, msg.msg_controllen = 0; }
 else*/ {
   cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
   cmsg->cmsg_len = CMSG_LEN(sizeof (int));
   cmsg->cmsg_level = SOL_SOCKET;
   cmsg->cmsg_type = SCM_RIGHTS;
   *((int*)CMSG_DATA(cmsg)) = fd;
  }
  ssize_t size = sendmsg(Socket::fd, &msg, 0);
  assert_(size == ssize_t(data.size));
 }
 sequence++;
 return sequence;
}

array<byte> XDisplay::readReply(uint16 sequence, uint elementSize, buffer<int>& fds) {
 for(;;) {
  X11::Event e;
  iovec iov {.iov_base = &e, .iov_len = sizeof(e)};
  union { cmsghdr header; char control[CMSG_SPACE(sizeof(int))]; } cmsgu;
  msghdr msg{.msg_name=0, .msg_namelen=0, .msg_iov=&iov, .msg_iovlen=1, .msg_control=&cmsgu, .msg_controllen = sizeof(cmsgu.control), .msg_flags=0};
  ssize_t size = recvmsg(Socket::fd, &msg, 0);
  assert_(size==sizeof(e));
  if(e.type==Reply) {
   assert_(e.seq==sequence);
   array<byte> reply;
   reply.append(raw(e.reply));
   if(e.reply.size) { /*assert_(elementSize);*/ reply.append(read(align(4, e.reply.size*elementSize)).slice(0, e.reply.size*elementSize)); }
   cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
   if(cmsg) {
    assert_(cmsg && cmsg->cmsg_len == CMSG_LEN(sizeof(int)));
    assert_(cmsg->cmsg_level == SOL_SOCKET);
    assert_(cmsg->cmsg_type == SCM_RIGHTS);
    assert_(e.reply.padOrFdCount == 1);
    fds = buffer<int>(1);
    fds[0] = *((int*)CMSG_DATA(cmsg));
   }
   return reply;
  }
  if(e.type==Error) { error(e); assert_(e.seq!=sequence, e.seq, sequence); continue; }
  array<byte> o;
  o.append(raw(e));
  if(e.type==GenericEvent) o.append(read(e.genericEvent.size*4));
  xEvents.append(move(o));
  queue(); // Queues event to process after unwinding back to event loop
 }
}

void XDisplay::waitEvent(uint8 type) {
 for(;;) {
  // TODO: check if already in queue
  X11::Event e; array<byte> o;
  {Locker lock(this->lock);
   e = read<X11::Event>();
   if((e.type&0b01111111)==type) return;
   if(e.type==Error) { error(e); continue; }
   o.append(raw(e));
   if(e.type==GenericEvent) o.append(read(e.genericEvent.size*4));
  }
  if(e.type==SelectionRequest) event(o); // Prevent deadlock waiting for SelectionNotify owned on this connection
  else {
   xEvents.append(move(o));
   queue(); // Queues event to process event queue after unwinding back to main event loop
  }
 }
}

// Keyboard
uint XDisplay::keySym(uint8 code, uint8 state) {
 ::buffer<uint> keysyms;
 GetKeyboardMapping rq; auto r = request(({ rq.keycode=code; rq;}), keysyms);
 assert_(keysyms.size == r.numKeySymsPerKeyCode);
 if(keysyms.size == 1) keysyms = copyRef<uint>({keysyms[0],keysyms[0],keysyms[0],keysyms[0]});
 if(keysyms.size == 2) keysyms = copyRef<uint>({keysyms[0],keysyms[1],keysyms[0],keysyms[1]});
 if(keysyms.size == 3) keysyms = copyRef<uint>({keysyms[0],keysyms[1],keysyms[2],keysyms[2]});
 assert_(keysyms.size >= 4, "No KeySym for code", code, "in state",state, keysyms);
 if(keysyms[1]==0) keysyms[1]=keysyms[0];
 if(keysyms[3]==0) keysyms[3]=keysyms[2];
 int group = (state&(Mod1Mask|Mod3Mask|Mod4Mask|Mod5Mask)?1:0)<<1;
 bool numlock = state&Mod2Mask;
 bool keypad = keysyms[group]>=0xff80 && keysyms[group]<=0xffbd;
 bool shift = state&(ShiftMask|LockMask);
 return keysyms[group | (shift ^ (numlock && keypad))];
}

uint8 XDisplay::keyCode(uint sym) {
 uint8 keycode=0;
 for(uint8 i: range(minKeyCode,maxKeyCode+1)) if(keySym(i,0)==sym) { keycode=i; break;  }
 if(!keycode) {
  if(sym==0x1008ff14/*Play*/) return 172; //FIXME
  if(sym==0x1008ff32/*Media*/) return 234; //FIXME
  error("Unknown KeySym",int(sym)); return sym; }
 return keycode;
}

function<void()>& XDisplay::globalAction(uint key) { return actions.insert(key, []{}); }

uint XDisplay::Atom(const string name) {
 InternAtom r;
 return request(({r.size=uint16(2+align(4,name.size)/4); r.length=uint16(name.size); r;}), name).atom;
}

template<class T> buffer<T> XDisplay::getProperty(uint window, string name, size_t length) {
 {GetProperty r; send(({ r.window=window; r.property=Atom(name); r.length=uint(length); r;}));}
 buffer<int> fds;
 array<byte> replyData = readReply(sequence, 0, fds);
 auto r = *(GetProperty::Reply*)replyData.data;
 uint size = r.length*r.format/8;
 buffer<T> property;
 if(size) property = read<T>(size/sizeof(T));
 int pad=align(4,size)-size; if(pad) read(pad);
 return property;
}
template buffer<uint> XDisplay::getProperty(uint window, string name, size_t length);
template buffer<byte> XDisplay::getProperty(uint window, string name, size_t length);
