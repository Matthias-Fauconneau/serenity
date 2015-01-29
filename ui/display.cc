#include "display.h"
#include "x.h"
#include <sys/socket.h>
#include "data.h"
//FIXME: undefined reference

#include "gl.h"
#undef packed
#define Time XTime
#define Cursor XCursor
#define Depth XXDepth
#define Window XWindow
#define Screen XScreen
#define XEvent XXEvent
#define Display XDisplay
#include <GL/glx.h> //X11
#undef Time
#undef Cursor
#undef Depth
#undef Window
#undef Screen
#undef XEvent
#undef Display
#undef None

String str(XEvent::Error e) {
    uint8 code = e.code;
    ref<string> requests (X11::requests);
    ref<string> errors (X11::errors);
    uint16 request = e.minor;
    /***/  if(e.major==Shm::EXT) {
        requests = ref<string>(Shm::requests);
        if(code >= Shm::errorBase && code <= ref<string>(Shm::errors).size) {
            code -= Shm::errorBase;
            errors = ref<string>(Shm::errors);
        }
    } else if(e.major==XRender::EXT) {
        requests = ref<string>(XRender::requests);
        if(code >= XRender::errorBase && code <= ref<string>(XRender::errors).size) {
            code -= XRender::errorBase;
            errors = ref<string>(XRender::errors);
        }
    } else request = e.major;
    return str(requests[request], code<errors.size?errors[code]:str(code));
}

String str(XEvent e) {
    if(e.type==Error) return str(e.error);
    else error(e.type);
}

// Globals
namespace Shm { int EXT, event, errorBase; } using namespace Shm;
namespace XRender { int EXT, event, errorBase; } using namespace XRender;
namespace Present { int EXT, event, errorBase; }

Display::Display(bool GL, Thread& thread) : Socket(PF_LOCAL, SOCK_STREAM), Poll(Socket::fd,POLLIN,thread) {
    {String path = "/tmp/.X11-unix/X"+getenv("DISPLAY",":0").slice(1,1);
        struct sockaddr_un { uint16 family=1; char path[108]={}; } addr; mref<char>(addr.path,path.size).copy(path);
        if(check(connect(Socket::fd, (const sockaddr*)&addr,2+path.size), path)) error("X connection failed"); }
    {ConnectionSetup r;
        if(existsFile(".Xauthority",home()) && File(".Xauthority",home()).size()) {
            BinaryData s (readFile(".Xauthority",home()), true);
            string name, data;
            uint16 family unused = s.read();
            {uint16 length = s.read(); string host unused = s.read<byte>(length); }
            {uint16 length = s.read(); string port unused = s.read<byte>(length); }
            {uint16 length = s.read(); name = s.read<byte>(length); r.nameSize=name.size; }
            {uint16 length = s.read(); data = s.read<byte>(length); r.dataSize=data.size; }
            write(raw(r)+pad(name)+pad(data));
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
                        size = int2(screen.width, screen.height);
                    }
                }
            }
        }
        id=r.ridBase;
        minKeyCode=r.minKeyCode, maxKeyCode=r.maxKeyCode;
        assert(visual);
    }

	{auto r = request(QueryExtension{.length="MIT-SHM"_.size, .size=uint16(2+align(4,"MIT-SHM"_.size)/4)}, "MIT-SHM"_);
        Shm::EXT=r.major; Shm::event=r.firstEvent; Shm::errorBase=r.firstError;}
    {auto r = request(QueryExtension{.length="RENDER"_.size, .size=uint16(2+align(4,"RENDER"_.size)/4)}, "RENDER"_);
        XRender::EXT=r.major; XRender::event=r.firstEvent; XRender::errorBase=r.firstError; }
    {auto r = request(QueryExtension{.length="Present"_.size, .size=uint16(2+align(4,"RENDER"_.size)/4)}, "Present"_);
        Present::EXT=r.major; XRender::event=r.firstEvent; XRender::errorBase=r.firstError; }

	if(GL) { // libgl-xlib
		assert_(!glDisplay && !glContext);
		glDisplay = XOpenDisplay(strz(getenv("DISPLAY"_,":0"_))); assert_(glDisplay);
		const int fbAttribs[] = {GLX_DOUBLEBUFFER, 1, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 24, GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, 8,
								 /*GLX_FRAMEBUFFER_SRGB_CAPABLE_EXT, 1,*/ 0};
		int fbCount=0; GLXFBConfig* fbConfigs = glXChooseFBConfig(glDisplay, 0, fbAttribs, &fbCount); assert(fbConfigs && fbCount);
		const int contextAttribs[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3, 0};
		glContext = ((PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddress((const GLubyte*)"glXCreateContextAttribsARB"))
				(glDisplay, fbConfigs[0], 0, 1, contextAttribs);
		assert_(glContext);
	}
}

void Display::event() {
    for(;;) { // Process any pending events
        for(;;) { // Process any queued events
            array<byte> e;
            {Locker lock(this->lock);
                if(!events) break;
                e = events.take(0);
            }
            event(e);
        }
		array<byte> o;
        if(!poll()) break;
        {Locker lock(this->lock);
            XEvent e = read<XEvent>();
			if(e.type==Error) { error(e); continue; }
			o.append(raw(e));
            if(e.type==GenericEvent) o.append( read(e.genericEvent.size*4) );
        }
        event(o);
    }
}

void Display::event(const ref<byte> ge) {
    const XEvent& e = *(XEvent*)ge.data;
    uint8 type = e.type&0b01111111; //msb set if sent by SendEvent
    if(type==KeyPress) {
        function<void()>* action = actions.find( keySym(e.key, e.state) );
        if(action) { (*action)(); return; } // Global window action
    }
    onEvent(ge);
}

array<byte> Display::readReply(uint16 sequence, uint elementSize) {
    for(;;) {
        XEvent e = read<XEvent>();
        if(e.type==Reply) {
            assert_(e.seq==sequence);
			array<byte> reply;
			reply.append(raw(e.reply));
            if(e.reply.size) { assert_(elementSize); reply.append(read(e.reply.size*elementSize)); }
            return reply;
        }
		if(e.type==Error) { error(e); assert_(e.seq!=sequence, e.seq, sequence); continue; }
		array<byte> o;
		o.append(raw(e));
        if(e.type==GenericEvent) o.append(read(e.genericEvent.size*4));
        events.append(move(o));
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
		error("Unknown KeySym",int(sym)); return sym; }
    return keycode;
}

function<void()>& Display::globalAction(uint key) {
    auto code = keyCode(key);
    if(code) { send(GrabKey{.window=root, .keycode=code}); }
    else error("No such key", key);
    return actions.insert(key, []{});
}

uint Display::Atom(const string name) {
    return request(InternAtom{.size=uint16(2+align(4,name.size)/4),  .length=uint16(name.size)}, name).atom;
}
