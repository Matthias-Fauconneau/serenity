#include "display.h"
#include "x.h"
#include <sys/socket.h>
#include "data.h"
#include "gl.h"

String str(XEvent::Error e) {
    uint8 code = e.code;
    ref<string> requests (X11::requests);
    ref<string> errors (X11::errors);
    uint16 request = e.minor;
	/***/ if(e.major==DRI3::EXT) {
		requests = ref<string>(DRI3::requests);
	} else request = e.major;
	return str(request<requests.size?requests[request]:str(request), code<errors.size?errors[code]:str(code));
}

String str(XEvent e) {
    if(e.type==Error) return str(e.error);
    else error(e.type);
}

// Globals
namespace Shm { int EXT, event, errorBase; };
namespace DRI3 { int EXT; }
namespace Present { int EXT; }

Display::Display(Thread& thread) : Socket(PF_LOCAL, SOCK_STREAM), Poll(Socket::fd,POLLIN,thread) {
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

uint16 Display::send(ref<byte> data, int fd) {
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

array<byte> Display::readReply(uint16 sequence, uint elementSize, buffer<int>& fds) {
    for(;;) {
		XEvent e;
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
