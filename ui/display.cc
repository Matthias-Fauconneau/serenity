#include "display.h"
#include "x.h"
#include <sys/socket.h>
#include "data.h"

String str(XEvent::Error e) {
    uint8 code = e.code;
    ref<string> requests = X11::requests;
    ref<string> errors = X11::errors;
    uint16 request = e.minor;
    /***/  if(e.major==Shm::EXT) {
        requests = Shm::requests;
        if(code >= Shm::errorBase && code <= ref<string>(Shm::errors).size) {
            code -= Shm::errorBase;
            errors = Shm::errors;
        }
    } else if(e.major==XRender::EXT) {
        requests = XRender::requests;
        if(code >= XRender::errorBase && code <= ref<string>(XRender::errors).size) {
            code -= XRender::errorBase;
            errors = XRender::errors;
        }
    } else request = e.major;
    return str(code<errors.size?errors[code]:str(code), "error for request", request<requests.size?requests[request]:str(request),
               e.code, e.seq, e.id, e.major, e.minor);
}

String str(XEvent e) {
    if(e.type==Error) return str(e.error);
    else error(e.type);
}

// Globals
namespace Shm { int EXT, event, errorBase; } using namespace Shm;
namespace XRender { int EXT, event, errorBase; } using namespace XRender;
namespace Present { int EXT, event, errorBase; }

Display::Display() : Socket(PF_LOCAL, SOCK_STREAM), Poll(Socket::fd,POLLIN) {
    String path = "/tmp/.X11-unix/X"_+getenv("DISPLAY"_,":0"_).slice(1,1);
    struct sockaddr_un { uint16 family=1; char path[108]={}; } addr; copy(mref<char>(addr.path,path.size),path);
    if(check(connect(Socket::fd,(const sockaddr*)&addr,2+path.size),path)) error("X connection failed");
    {ConnectionSetup r;
        if(existsFile(".Xauthority"_,home()) && File(".Xauthority"_,home()).size()) {
            BinaryData s (readFile(".Xauthority"_,home()), true);
            string name, data;
            uint16 family unused = s.read();
            {uint16 length = s.read(); string host unused = s.read<byte>(length); }
            {uint16 length = s.read(); string port unused = s.read<byte>(length); }
            {uint16 length = s.read(); name = s.read<byte>(length); r.nameSize=name.size; }
            {uint16 length = s.read(); data = s.read<byte>(length); r.dataSize=data.size; }
            write(raw(r)+pad(String(name))+pad(String(data)));
        } else write(raw(r));
    }
    {ConnectionSetupReply1 unused r=read<ConnectionSetupReply1>(); assert(r.status==1);}
    {ConnectionSetupReply2 r=read<ConnectionSetupReply2>();
        read(align(4,r.vendorLength));
        read<XFormat>(r.numFormats);
        for(int i=0;i<r.numScreens;i++){ Screen screen=read<Screen>();
            for(int i=0;i<screen.numDepths;i++) { XDepth depth = read<XDepth>();
                if(depth.numVisualTypes) for(VisualType visualType: read<VisualType>(depth.numVisualTypes)) {
                    if(!visual && depth.depth==32) {
                        root = screen.root;
                        visual=visualType.id;
                        screenX=screen.width, screenY=screen.height;
                    }
                }
            }
        }
        id=r.ridBase;
        minKeyCode=r.minKeyCode, maxKeyCode=r.maxKeyCode;
    }
    assert(visual);

    {auto r = request(QueryExtension{.length="MIT-SHM"_.size, .size=uint16(2+align(4,"MIT-SHM"_.size)/4)}, "MIT-SHM"_);
        Shm::EXT=r.major; Shm::event=r.firstEvent; Shm::errorBase=r.firstError;}
    {auto r = request(QueryExtension{.length="RENDER"_.size, .size=uint16(2+align(4,"RENDER"_.size)/4)}, "RENDER"_);
        XRender::EXT=r.major; XRender::event=r.firstEvent; XRender::errorBase=r.firstError; }
    {auto r = request(QueryExtension{.length="Present"_.size, .size=uint16(2+align(4,"RENDER"_.size)/4)}, "Present"_);
        Present::EXT=r.major; XRender::event=r.firstEvent; XRender::errorBase=r.firstError; }
}

void Display::event() {
    for(;;) { // Process any pending events
        for(;;) { // Process any queued events
            buffer<byte> e;
            {Locker lock(this->lock);
                if(!events) break;
                e = events.take(0);
            }
            onEvent(e);
        }
        array<byte> o;
        if(!poll()) break;
        {Locker lock(this->lock);
            XEvent e = read<XEvent>();
            if(e.type==Error) { log(e); continue; }
            o = array<byte>(raw(e));
            if(e.type==GenericEvent) o << read(e.genericEvent.size*4);
        }
        onEvent(o);
    }
}

buffer<byte> Display::readReply(uint16 sequence, uint elementSize) {
    for(;;) {
        XEvent e = read<XEvent>();
        if(e.type==Reply) {
            assert_(e.seq==sequence);
            array<byte> r (raw(e.reply));
            if(e.reply.size) { assert_(elementSize); r << read(e.reply.size*elementSize); }
            return move(r);
        }
        if(e.type==Error) { log(e); assert_(e.seq!=sequence); continue; }
        array<byte> o (raw(e));
        if(e.type==GenericEvent) o << read(e.genericEvent.size*4);
        events << move(o);
        queue(); // Queue event to process after unwinding back to event loop
    }
}

// Keyboard
uint Display::keySym(uint8 code, uint8 state) {
    ::buffer<uint> keysyms;
    auto r = request(GetKeyboardMapping{.keycode=code}, keysyms);
    assert_(keysyms.size == r.numKeySymsPerKeyCode, keysyms.size, r.numKeySymsPerKeyCode, r.size);
    assert_(keysyms, "No KeySym for code", code, "in state",state);
    if(keysyms.size>=2 && keysyms[1]>=0xff80 && keysyms[1]<=0xffbd) state|=1;
    return keysyms[state&1 && keysyms.size>=2];
}

uint8 Display::keyCode(uint sym) {
    uint8 keycode=0;
    for(uint8 i: range(minKeyCode,maxKeyCode+1)) if(keySym(i,0)==sym) { keycode=i; break;  }
    if(!keycode) {
        if(sym==0x1008ff14/*Play*/) return 172; //FIXME
        if(sym==0x1008ff32/*Media*/) return 234; //FIXME
        log("Unknown KeySym",int(sym)); return sym; }
    return keycode;
}

function<void()>& Display::globalAction(uint key) {
    auto code = keyCode(key);
    if(code) send(GrabKey{.window=root, .keycode=code});
    else error("No such key", key);
    return actions.insert(key, []{});
}

uint Display::Atom(const string& name) {
    return request(InternAtom{.size=uint16(2+align(4,name.size)/4),  .length=uint16(name.size)}, name).atom;
}
