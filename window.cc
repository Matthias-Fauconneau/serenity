#include "window.h"
#include "x.h"
#include "display.h"
#include "widget.h"
#include "file.h"
#include "stream.h"
#include "linux.h"

/// Reads a raw value from \a fd
template<class T> T read(int fd) {
    T t;
    int unused size = read(fd,(byte*)&t,sizeof(T));
    assert(size==sizeof(T),size,sizeof(T));
    return t;
}
/// Reads \a size raw values from \a fd
template<class T> array<T> read(int fd, uint capacity) {
    array<T> buffer(capacity);
    int unused size = check( read(fd,(byte*)buffer.data(),sizeof(T)*capacity) );
    assert((uint)size==capacity*sizeof(T),size,sizeof(T));
    buffer.setSize(capacity);
    return buffer;
}

Window::Window(Widget* widget, int2 size, const ref<byte>& title, const Image<byte4>& icon) : widget(widget),
    x(socket(PF_LOCAL, SOCK_STREAM, 0)) {
    // Setups X connection
    ref<byte> path = "/tmp/.X11-unix/X0"_;
    sockaddr_un addr; copy(addr.path,path.data,path.size);
    int e=connect(x,(sockaddr*)&addr,2+path.size); if(e) error("No X server",errno[-e],ref<byte>(addr.path,path.size));
    {ConnectionSetup r; write(x, string(raw(r)+readFile("root/.Xauthority"_).slice(18,align(4,r.nameSize)+r.dataSize)));}
    uint visual=0;
    {ConnectionSetupReply r=read<ConnectionSetupReply>(x); assert(r.status==1,ref<byte>((byte*)&r.release,r.reason-1));
        read(x,align(4,r.vendorLength));
        read<Format>(x,r.numFormats);
        for(int i=0;i<r.numScreens;i++){ Screen screen=read<Screen>(x);
            for(int i=0;i<screen.numDepths;i++) { Depth depth = read<Depth>(x);
                for(VisualType visualType: read<VisualType>(x,depth.numVisualTypes)) {
                    if(!visual && depth.depth==32) { display=int2(screen.width,screen.height); root = screen.root; visual=visualType.id; }
                }
            }
        }
        id=r.ridBase;
    }
    assert(visual);

    // Creates X window
    if(size.x<=0) size.x=display.x+size.x;
    if(size.y<=0) size.y=display.y+size.y;
    widget->size=size; widget->update();
    {CreateColormap r; r.colormap=id+Colormap; r.window=root; r.visual=visual; write(x,raw(r));}
    {CreateWindow r; r.id=id+XWindow; r.parent=root; r.width=size.x, r.height=size.y; r.visual=visual; r.colormap=id+Colormap;
        r.backgroundPixel=r.borderPixel=0xF0F0F0F0; r.eventMask=StructureNotifyMask|KeyPressMask|ButtonPressMask|LeaveWindowMask|PointerMotionMask|ExposureMask; write(x,raw(r));}
    {ChangeProperty r; r.id=id+XWindow; r.property=Atom("WM_PROTOCOLS"_); r.type=Atom("ATOM"_); r.format=32;
    r.length=1; r.size+=r.length; write(x,string(raw(r)+raw(Atom("WM_DELETE_WINDOW"_))));}
    {CreateGC r; r.context=id+GContext; r.window=id+XWindow; write(x,raw(r));}
    setTitle(title);
    setIcon(icon);
    registerPoll(__(x,POLLIN));
}

void Window::event(const pollfd& poll) {
    if(poll.fd==0) render();
    if(poll.fd==x) { uint8 type = read<uint8>(x); readEvent(type); }
}

void Window::readEvent(uint8 type) {
    /***/ if(type==0) { XError e=read<XError>(x);
        error("Error",e.code<sizeof(xerror)/sizeof(*xerror)?xerror[e.code]:dec(e.code),"seq:",e.seq,"id",e.id,"request",
              e.major<sizeof(xrequest)/sizeof(*xrequest)?xrequest[e.major]:dec(e.major),"minor",e.minor);
    } else if(type==1) { error("Unexpected reply");
    } else { XEvent unused e=read<XEvent>(x); type&=0b01111111; //msb set if sent by SendEvent
        if(type==Motion) {
            if(widget->mouseEvent(int2(e.x,e.y), Motion, (e.state&Button1Mask)?LeftButton:None)) wait();
        } else if(type==ButtonPress) {
            if(widget->mouseEvent(int2(e.x,e.y), ButtonPress, (Key)e.key)) wait();
        } else if(type==KeyPress) {
            uint key = KeySym(e.key);
            signal<>* shortcut = localShortcuts.find(key);
            if(shortcut) (*shortcut)(); //local window shortcut
            else if(focus) if( focus->keyPress((Key)key) ) wait(); //normal keyPress event
        } else if(type==Enter || type==Leave) {
            signal<>* shortcut = localShortcuts.find(Leave);
            if(shortcut) (*shortcut)(); //local window shortcut
            if( widget->mouseEvent(int2(e.x,e.y), (Event)type, (e.state&Button1Mask)?LeftButton:None) ) wait();
        } else if(type==Expose) { if(!e.expose.count && e.expose.w>1 && e.expose.h>1) wait();
        } else if(type==UnmapNotify) { mapped=false;
        } else if(type==MapNotify) { mapped=true;
        } else if(type==ReparentNotify) {
        } else if(type==ConfigureNotify) { int2 size(e.configure.w,e.configure.h); if(widget->size!=size) { widget->size=size; widget->update(); }
        } else if(type==ClientMessage) {
            signal<>* shortcut = localShortcuts.find(Escape);
            if(shortcut) (*shortcut)(); //local window shortcut
            else widget->keyPress(Escape);
        } else log("Event", type<sizeof(xevent)/sizeof(*xevent)?xevent[type]:str(type));
    }
}

template<class T> T Window::readReply() { for(;;) { uint8 type = read<uint8>(x); if(type==1) return read<T>(x); else readEvent(type); } }
uint Window::Atom(const ref<byte>& name) {
    {InternAtom r; r.length=name.size; r.size+=align(4,r.length)/4; write(x,string(raw(r)+name+pad(4,r.length)));}
    {InternAtomReply r=readReply<InternAtomReply>(); return r.atom; }
}
uint Window::KeySym(uint8 code) {
    {GetKeyboardMapping r; r.keycode=code; write(x,raw(r));}
    {GetKeyboardMappingReply r=readReply<GetKeyboardMappingReply>();
        array<uint> keysyms = read<uint>(x,r.numKeySymsPerKeyCode);
        return keysyms.first();
    }
}

void Window::setWidget(Widget* widget) {
    widget->size=this->widget->size;
    this->widget=widget;
    widget->update();
}

void Window::setPosition(int2 position) {
    if(position.x<0) position.x=display.x+position.x;
    if(position.y<0) position.y=display.y+position.y;
    {ConfigureWindow r; r.id=id+XWindow; r.mask=1+2; r.x=position.x, r.y=position.y; write(x,raw(r));}
}
void Window::setSize(int2 size) {
    if(size.x<=0) size.x=display.x+size.x;
    if(size.y<=0) size.y=display.y+size.y;
    {ConfigureWindow r; r.id=id+XWindow; r.mask=4+8; r.x=size.x, r.y=size.y; write(x,raw(r));}
}
void Window::setTitle(const ref<byte>& title) {
    ChangeProperty r; r.id=id+XWindow; r.property=Atom("_NET_WM_NAME"_); r.type=Atom("UTF8_STRING"_); r.format=8;
    r.length=title.size; r.size+=align(4, r.length)/4; write(x,string(raw(r)+title+pad(4,title.size)));
}
void Window::setIcon(const Image<byte4>& icon) {
    ChangeProperty r; r.id=id+XWindow; r.property=Atom("_NET_WM_ICON"_); r.type=Atom("CARDINAL"_); r.format=32;
    r.length=2+icon.width*icon.height; r.size+=r.length; write(x,string(raw(r)+raw(icon.width)+raw(icon.height)+(ref<byte>)icon));
}

void Window::show() { if(mapped) return; {MapWindow r; r.id=id; write(x,raw(r));}}
void Window::hide() { if(!mapped) return;{UnmapWindow r; r.id=id; write(x,raw(r));}}

void Window::update() { widget->update(); render(); }
void Window::render() {
    assert(mapped); assert(widget->size);
    if(buffer.width != (uint)widget->size.x || buffer.height != (uint)widget->size.y) {
        if(shm) {
            {Shm::Detach r; r.seg=id+Segment; write(x, raw(r));}
            shmdt(buffer.data);
            shmctl(shm, IPC_RMID, 0);
        }
        buffer.stride=buffer.width= widget->size.x, buffer.height = widget->size.y;
        shm = check( shmget(IPC_NEW, sizeof(pixel)*buffer.width*buffer.height , IPC_CREAT | 0777) );
        buffer.data = (pixel*)shmat(shm, 0, 0); assert(buffer.data);
        {Shm::Attach r; r.seg=id+Segment; r.shm=shm; write(x,raw(r));}
    }
    framebuffer = share(buffer);
    currentClip=Rect(framebuffer.size());
    widget->render(int2(0,0));
    assert(!clipStack);
    {Shm::PutImage r; r.window=id+XWindow; r.context=id+GContext; r.seg=id+Segment;
        r.totalWidth=r.width=buffer.width; r.totalHeight=r.height=buffer.height; write(x,raw(r)); }
}

signal<>& Window::localShortcut(Key key) { return localShortcuts.insert((uint16)key); }
signal<>& Window::globalShortcut(Key key) { return globalShortcuts.insert((uint16)key); }

string Window::getSelection() { return string(); }
