#include "display.h"
#include "x.h"
#include <sys/socket.h>
#include "data.h"
#include "gl.h"

using namespace X11;

String str(X11::Event::Error e) {
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
namespace Shm { int EXT, event, errorBase; };
namespace DRI3 { int EXT; }
namespace Present { int EXT; }

XDisplay::XDisplay(Thread& thread) : Socket(PF_LOCAL, SOCK_STREAM), Poll(Socket::fd,POLLIN,thread) {
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
    {auto r = request(QueryExtension{.length="Present"_.size, .size=uint16(2+align(4,"RENDER"_.size)/4)}, "Present"_);
	Present::EXT=r.major; assert_(Present::EXT); }
}

void XDisplay::event() {
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
	iovec iov {.iov_base = (byte*)data.data, .iov_len = data.size};
	union { cmsghdr cmsghdr; char control[CMSG_SPACE(sizeof(int))]; } cmsgu;
	msghdr msg{.msg_name=0, .msg_namelen=0, .msg_iov=&iov, .msg_iovlen=1, .msg_control=&cmsgu, .msg_controllen = sizeof(cmsgu.control)};
	if(fd==-1) { msg.msg_control = NULL, msg.msg_controllen = 0; }
	else {
		cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
		cmsg->cmsg_len = CMSG_LEN(sizeof (int));
		cmsg->cmsg_level = SOL_SOCKET;
		cmsg->cmsg_type = SCM_RIGHTS;
		*((int *)CMSG_DATA(cmsg)) = fd;
	}
	ssize_t size = sendmsg(Socket::fd, &msg, 0);
	assert_(size == ssize_t(data.size));
	sequence++;
	return sequence;
}

array<byte> XDisplay::readReply(uint16 sequence, uint elementSize, buffer<int>& fds) {
    for(;;) {
		X11::Event e;
		iovec iov {.iov_base = &e, .iov_len = sizeof(e)};
		union { cmsghdr cmsghdr; char control[CMSG_SPACE(sizeof(int))]; } cmsgu;
		msghdr msg{.msg_name=0, .msg_namelen=0, .msg_iov=&iov, .msg_iovlen=1, .msg_control=&cmsgu, .msg_controllen = sizeof(cmsgu.control)};
		ssize_t size = recvmsg(Socket::fd, &msg, 0);
		assert_(size==sizeof(e));
        if(e.type==Reply) {
            assert_(e.seq==sequence);
			array<byte> reply;
			reply.append(raw(e.reply));
            if(e.reply.size) { assert_(elementSize); reply.append(read(e.reply.size*elementSize)); }
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
        events.append(move(o));
		queue(); // Queues event to process after unwinding back to event loop
    }
}

// Keyboard
uint XDisplay::keySym(uint8 code, uint8 state) {
    ::buffer<uint> keysyms;
    auto r = request(GetKeyboardMapping{.keycode=code}, keysyms);
    assert_(keysyms.size == r.numKeySymsPerKeyCode, keysyms.size, r.numKeySymsPerKeyCode, r.size);
    assert_(keysyms, "No KeySym for code", code, "in state",state);
    if(keysyms.size>=2 && keysyms[1]>=0xff80 && keysyms[1]<=0xffbd) state|=1;
    return keysyms[state&1 && keysyms.size>=2];
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

function<void()>& XDisplay::globalAction(uint key) {
    auto code = keyCode(key);
    if(code) { send(GrabKey{.window=root, .keycode=code}); }
    else error("No such key", key);
    return actions.insert(key, []{});
}

uint XDisplay::Atom(const string name) {
    return request(InternAtom{.size=uint16(2+align(4,name.size)/4),  .length=uint16(name.size)}, name).atom;
}

#include "window.h"
#include <unistd.h>
#include <xf86drm.h> // drm
#include <xf86drmMode.h>
#include <gbm.h> // gbm
struct input_event { long sec,usec; uint16 type, code; int32 value; };
enum { EV_SYN, EV_KEY, EV_REL, EV_ABS };
#include "png.h"
ICON(cursor);

Keyboard::Keyboard(Thread& thread) : Device("event3", "/dev/input"_, Flags(ReadOnly|NonBlocking)), Poll(Device::fd, POLLIN, thread) {}
void Keyboard::event() {
	for(input_event e; ::read(Device::fd, &e, sizeof(e)) > 0;) {
		if(e.type == EV_KEY && e.value) {
			enum {
				None, Escape, _1,_2,_3,_4,_5,_6,_7,_8,_9,_0, Minus, Equal, Backspace, Tab, Q,W,E,R,T,Y,U,I,O,P, LeftBrace, RightBrace, Return, LeftCtrl,
				A,S,D,F,G,H,J,K,L, Semicolon, Apostrophe, Grave, LeftShift, BackSlash, Z,X,C,V,B,N,M, Comma, Dot, Slash, RightShift, KP_Asterisk, LeftAlt,
				Space, KP_7 = 71, KP_8, KP_9, KP_Minus, KP_4, KP_5, KP_6, KP_Plus, KP_1, KP_2, KP_3, KP_0, KP_Slash=98,
				Home=102, UpArrow, PageUp, LeftArrow, RightArrow, End, DownArrow, PageDown, Insert, Delete, Macro, Mute, VolumeDown, VolumeUp,
				Power=116
			};
			keyPress(Key(e.code)); // FIXME: map evdev to X11
		}
	}
}
Mouse::Mouse(Thread& thread) : Device("event4", "/dev/input"_, Flags(ReadOnly|NonBlocking)), Poll(Device::fd, POLLIN, thread) {}
void Mouse::event() {
	for(input_event e; ::read(Device::fd, &e, sizeof(e)) > 0;) {
		   if(e.type==EV_REL) { int i = e.code; assert(i<2); cursor[i]+=e.value; cursor[i]=clamp(0,cursor[i], max[i]); } //TODO: acceleration
		   if(e.type==EV_SYN) mouseEvent(cursor, Motion, button);
		   if(e.type == EV_KEY) {
			   enum { LeftButton=0x110, RightButton, MiddleButton, WheelDown=0x150, WheelUp };
			   button = Button(e.code); // FIXME: map evdev to X11
			   mouseEvent(cursor, e.value?Press:Release, button);
			   if(!e.value) button = NoButton;
		   }
	}
}

Display::Display(Thread& thread) : Device("/dev/dri/card0"), Poll(0,0,thread), keyboard(thread), mouse(mouseThread) {
	{  drmModeConnector* c = 0;
		{  drmModeRes* res = drmModeGetResources(Device::fd);
			for(uint connector: ref<uint32>(res->connectors, res->count_connectors)) {
				c = drmModeGetConnector(Device::fd, connector);
				if(c->connection != DRM_MODE_CONNECTED) { drmModeFreeConnector(c); continue; }
				if(c->count_modes == 0) { drmModeFreeConnector(c); continue; }
				break;
			}
			drmModeFreeResources(res);
		}
		connector = c->connector_id;
		mode = unique<_drmModeModeInfo>(c->modes[0]);
		{  drmModeEncoder *enc = drmModeGetEncoder(Device::fd, c->encoder_id);
			crtc = enc->crtc_id;
			drmModeFreeEncoder(enc);
		}
		drmModeFreeConnector(c);
	}
	previousMode = drmModeGetCrtc(Device::fd, crtc);

	for(auto& buffer: buffers) {
		typedef IOWR<'d', 0xB2, drm_mode_create_dumb> CREATE_DUMB;
		drm_mode_create_dumb creq {.width = mode->hdisplay, .height = mode->vdisplay, .bpp=32, .flags = 0};
		iowr<CREATE_DUMB>(creq);
		buffer.handle = creq.handle;
		drmModeAddFB(Device::fd, mode->hdisplay, mode->vdisplay, 24, 32, creq.pitch, buffer.handle, &buffer.fb);

		drm_mode_map_dumb mreq {.handle = buffer.handle};
		typedef IOWR<'d', 0xB3, drm_mode_map_dumb> MAP_DUMB;
		iowr<MAP_DUMB>(mreq);
		buffer.map = Map(Device::fd, mreq.offset, creq.size);
		buffer.target = Image(unsafeRef(cast<byte4>(buffer.map)), int2(mode->hdisplay, mode->vdisplay), creq.pitch/4);
	}

	drmModeSetCrtc(Device::fd, crtc, buffers[0].fb, 0, 0, &connector, 1, mode.pointer);

	mouse.max = buffers[0].target.size;
	mouse.mouseEvent = {this, &Display::mouseEvent};
	keyboard.keyPress = {this, &Display::keyPress};
	Image cursor (64);
	cursor.clear(0);
	Image source = cursorIcon();
	for(int y: range(source.size.y)) for(int x: range(source.size.x)) cursor(x, y) = source(x, y);
	gbm_device* gbm= gbm_create_device(Device::fd);
	gbm_bo* bo = gbm_bo_create(gbm, cursor.width, cursor.height, GBM_FORMAT_ARGB8888, GBM_BO_USE_CURSOR|GBM_BO_USE_WRITE);
	assert_(bo);
	check( gbm_bo_write(bo, cursor.data, cursor.height * cursor.width * 4) );
	drmModeSetCursor(Device::fd, crtc, gbm_bo_get_handle(bo).u32, cursor.width, cursor.height);
	mouseThread.spawn();
	registerPoll();
}

void Display::swapBuffers() {
	swap(buffers[0], buffers[1]);
	drmModeSetCrtc(Device::fd, crtc, buffers[0].fb, 0, 0, &connector, 1, mode.pointer);
}

Display::~Display() {
	drmModeSetCrtc(Device::fd, previousMode->crtc_id, previousMode->buffer_id, previousMode->x, previousMode->y, &connector, 1, &previousMode->mode);
	drmModeFreeCrtc(previousMode);
	for(auto& buffer: buffers) {
		drmModeRmFB(Device::fd, buffer.fb);
		typedef IOWR<'d', 0xB4, drm_mode_destroy_dumb> DESTROY_DUMB;
		struct drm_mode_destroy_dumb dreq {.handle = buffer.handle};
		iowr<DESTROY_DUMB>(dreq);
	}
}

void Display::mouseEvent(int2 cursor, ::Event event, Button button) {
	drmModeMoveCursor(Device::fd, crtc, cursor.x, cursor.y);
	{Locker lock(this->lock); eventQueue.append({cursor, event, button});}
	queue();
}
void Display::event() {
	MouseEvent e;
	{Locker lock(this->lock); e = eventQueue.take(0);}
	for(DRMWindow* window: windows) window->mouseEvent(e.cursor, e.event, e.button);
}
void Display::keyPress(Key key) { for(DRMWindow* window: windows) window->keyPress(key);  }
