#include "window.h"
#include "graphics.h"
#include "png.h"

static thread_local Window* window; // Current window for Widget event and render methods
void setWindow(Window* window) { ::window = window; }
void setFocus(Widget* widget) { assert(window); window->focus=widget; }
bool hasFocus(Widget* widget) { assert(window); return window->focus==widget; }
void setDrag(Widget* widget) { assert(window); window->drag=widget; }
String getSelection(bool clipboard) { assert(window); return window->getSelection(clipboard); }
void setCursor(Rect region, Cursor cursor) { assert(window); if(region.contains(window->cursorPosition)) window->cursor=cursor; }
void putImage(Rect region) { assert(window); window->putImage(region); }
#include "trace.h"
void putImage(const Image& target) {
    assert_(window);
    if(target.buffer != window->target.buffer) { log("target.buffer == window->target.buffer"); return; }
    assert_(target.buffer == window->target.buffer);
    uint delta = target.data - window->target.data;
    if(!target.stride) return; // FIXME
    assert_(target.stride);
    uint x = delta % target.stride, y = delta / target.stride;
    assert_(x+target.width<=window->target.width && y+target.height<=window->target.height);
    window->putImage(int2(x,y)+Rect(target.size()));
}

#if X11
#include "file.h"
#include "data.h"
#include "time.h"
#include "image.h"
#include "x.h"
#include <sys/socket.h>
#include <sys/shm.h>

// Globals
namespace BigRequests { int EXT, event, errorBase; } using namespace BigRequests;
namespace Shm { int EXT, event, errorBase; } using namespace Shm;
namespace XRender { int EXT, event, errorBase; } using namespace XRender;
static uint maximumRequestLength;

Window::Window(Widget* widget, const string& title _unused, int2 size, const Image& icon _unused) :
    Socket(section(getenv("DISPLAY"_,":0"_),':')?PF_INET:PF_LOCAL, SOCK_STREAM),
    Poll(Socket::fd,POLLIN), widget(widget), remote(section(getenv("DISPLAY"_,":0"_),':')) {
    if(!remote) {
        String path = "/tmp/.X11-unix/X"_+section(getenv("DISPLAY"_,":0"_),':',1,2);
        struct sockaddr_un { uint16 family=1; char path[108]={}; } addr; copy(mref<char>(addr.path,path.size),path);
        if(check(connect(Socket::fd,(const sockaddr*)&addr,2+path.size),path)) error("X connection failed");
    } else {
        uint display = fromInteger( section(getenv("DISPLAY"_,":0"_),':',1,2) );
        struct sockaddress { uint16 family; uint16 port; uint8 host[4]; int pad[2]={}; } addr = {PF_INET, bswap(uint16(6000+display)), {127,0,0,1}};
        if(check(connect(Socket::fd,(const sockaddr*)&addr,sizeof(addr)))) error("X connection failed");
    }
    {ConnectionSetup r;
        if(existsFile(".Xauthority"_,home()) && File(".Xauthority"_,home()).size()>0) {
            BinaryData s (readFile(".Xauthority"_,home()), true);
            string name, data;
            while(s) { // FIXME: Assumes last entry is correct
                uint16 family _unused = s.read();
                {uint16 length = s.read(); if(length>s.available()) break;/*FIXME*/ string host _unused = s.read<byte>(length); }
                {uint16 length = s.read(); string port _unused = s.read<byte>(length); }
                {uint16 length = s.read(); name = s.read<byte>(length); r.nameSize=name.size; }
                {uint16 length = s.read(); data = s.read<byte>(length); r.dataSize=data.size; }
            }
            send(String(raw(r)+name+pad(4, name.size)+data+pad(4,data.size)));
        } else send(raw(r));
    }
    {ConnectionSetupReply1 _unused r=read<ConnectionSetupReply1>(); assert(r.status==1);}
    {ConnectionSetupReply2 r=read<ConnectionSetupReply2>();
        read(align(4,r.vendorLength));
        read<XFormat>(r.numFormats);
        for(int i=0;i<r.numScreens;i++){ Screen screen=read<Screen>();
            for(int i=0;i<screen.numDepths;i++) { XDepth depth = read<XDepth>();
                if(depth.numVisualTypes) for(VisualType visualType: read<VisualType>(depth.numVisualTypes)) {
                    if(!visual && depth.depth==32) {
                        displaySize=int2(screen.width,screen.height); root = screen.root; visual=visualType.id;
                    }
                }
            }
        }
        id=r.ridBase;
        minKeyCode=r.minKeyCode, maxKeyCode=r.maxKeyCode;
    }
    assert(visual);

    {QueryExtensionReply r=readReply<QueryExtensionReply>((
        {QueryExtension r; r.length="BIG-REQUESTS"_.size; r.size+=align(4,r.length)/4; String(raw(r)+"BIG-REQUESTS"_+pad(4,r.length));}));
        BigRequests::EXT=r.major; BigRequests::event=r.firstEvent; BigRequests::errorBase=r.firstError;}
    maximumRequestLength = readReply<BigRequests::BigReqEnableReply>(raw(BigRequests::BigReqEnable())).maximumRequestLength;

    {QueryExtensionReply r=readReply<QueryExtensionReply>((
        {QueryExtension r; r.length="MIT-SHM"_.size; r.size+=align(4,r.length)/4; String(raw(r)+"MIT-SHM"_+pad(4,r.length));}));
        Shm::EXT=r.major; Shm::event=r.firstEvent; Shm::errorBase=r.firstError;}

    {QueryExtensionReply r=readReply<QueryExtensionReply>((
        {QueryExtension r; r.length="RENDER"_.size; r.size+=align(4,r.length)/4; String(raw(r)+"RENDER"_+pad(4,r.length));}));
        XRender::EXT=r.major; XRender::event=r.firstEvent; XRender::errorBase=r.firstError; }
    {QueryPictFormatsReply r=readReply<QueryPictFormatsReply>(raw(QueryPictFormats()));
        array<PictFormInfo> formats = read<PictFormInfo>( r.numFormats);
        for(uint _unused i: range(r.numScreens)) { PictScreen screen = read<PictScreen>();
            for(uint _unused i: range(screen.numDepths)) { PictDepth depth = read<PictDepth>();
                array<PictVisual> visuals = read<PictVisual>(depth.numPictVisuals);
                if(depth.depth==32) for(PictVisual pictVisual: visuals) if(pictVisual.visual==visual) format=pictVisual.format;
            }
        }
        assert(format);
        read<uint>(r.numSubpixels);
    }
    {CreateColormap r; r.colormap=id+Colormap; r.window=root; r.visual=visual; send(raw(r));}

    if((size.x<0||size.y<0) && widget) {
        int2 hint=widget->sizeHint();
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=displaySize.x;
    if(size.y==0) size.y=displaySize.y-16;
    if(anchor==Bottom) position.y=displaySize.y-size.y;
    this->size=size;
    {CreateWindow r; r.id=id+XWindow; r.parent=root; r.x=position.x; r.y=position.y; r.width=size.x, r.height=size.y;
        r.visual=visual; r.colormap=id+Colormap; r.overrideRedirect=overrideRedirect;
        r.eventMask=StructureNotifyMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask
                |EnterWindowMask|LeaveWindowMask|PointerMotionMask|ExposureMask;
        send(raw(r));
    }
    {CreateGC r; r.context=id+GContext; r.window=id+XWindow; send(raw(r));}
    {ChangeProperty r; r.window=id+XWindow; r.property=Atom("WM_PROTOCOLS"_); r.type=Atom("ATOM"_); r.format=32;
        r.length=1; r.size+=r.length; send(String(raw(r)+raw(Atom("WM_DELETE_WINDOW"_))));}
    {ChangeProperty r; r.window=id+XWindow; r.property=Atom("_KDE_OXYGEN_BACKGROUND_GRADIENT"_); r.type=Atom("CARDINAL"_); r.format=32;
        r.length=1; r.size+=r.length; send(String(raw(r)+raw(1)));}
    setTitle(title);
    setIcon(icon);
    actions[Escape] = []{ exit(); };
    actions[PrintScreen] = [this]{ Locker lock(renderLock); writeFile(this->title+".png"_,encodePNG(target)); };
}
Window::~Window() {
    {FreeGC r; r.context=id+GContext; send(raw(r));}
    {DestroyWindow r; r.id=id+XWindow; send(raw(r));}
}

// Render
void Window::event() {
    /*if(revents!=IDLE)*/ for(;;) { // Always process any pending X input events before rendering
        lock.lock();
        if(!poll()) { lock.unlock(); break; }
        uint8 type = read<uint8>();
        XEvent e = read<XEvent>();
        lock.unlock();
        processEvent(type, e);
    }
    while(semaphore.tryAcquire(1)) { lock.lock(); QEvent e = eventQueue.take(0); lock.unlock(); processEvent(e.type, e.event); }
    if(motionPending) processEvent(LastEvent, XEvent());
    if(needRender) { render(); needRender=false; }
}

void Window::render() {
    Locker lock(renderLock);
    if(target.size() != size) {
        if(remote) target = Image(size.x, size.y);
        else {
            if(shm) {
                {Shm::Detach r; r.seg=id+Segment; send(raw(r));}
                shmdt(target.data);
                shmctl(shm, IPC_RMID, 0);
            }
            target.width=size.x, target.height=size.y;
            target.stride = remote ? size.x : align(16,size.x);
            target.buffer.size = target.height*target.stride;
            assert_(target.buffer.size < 4096*4096, target.width, target.height);
            shm = check( shmget(0, target.buffer.size*sizeof(byte4), IPC_CREAT | 0777) );
            target.buffer.data = target.data = (byte4*)check( shmat(shm, 0, 0) ); assert(target.data);
            Shm::Attach r; r.seg=id+Segment; r.shm=shm; send(raw(r));
        }
    }

    renderBackground(target);
    assert_(!window);
    window=this;
    widget->render(target);
    window=0;
    putImage();
}

void Window::putImage(Rect rect) {
    if(rect==Rect(0)) rect=Rect(target.size());
    assert_(id); assert_(rect.size());
    if(remote) {
        Image target = share(this->target);
        if(rect != Rect(target.size())) {
            target = Image(rect.size());
            copy(target, clip(this->target, rect));
        }
        assert_(target.buffer.size == (size_t)target.size().x*target.size().y);
        ::PutImage r; r.drawable=id+XWindow; r.context=id+GContext; r.w=target.size().x, r.h=target.size().y; r.x=rect.min.x, r.y=rect.min.y; r.size += target.buffer.size;
        assert_(raw(r).size + cast<byte>(target.buffer).size == r.size*4);
        send(String(raw(r)+cast<byte>(target.buffer)));
        assert_(r.size <= maximumRequestLength, r.size, maximumRequestLength);
    } else {
        Shm::PutImage r; r.window=id+XWindow; r.context=id+GContext; r.seg=id+Segment;
        r.totalW=target.stride; r.totalH=target.height;
        r.srcX = rect.position().x, r.srcY = rect.position().y, r.srcW=rect.size().x; r.srcH=rect.size().y;
        r.dstX = rect.position().x, r.dstY = rect.position().y;
        send(raw(r));
    }
}

// Events
void Window::processEvent(uint8 type, const XEvent& event) {
    if(type==0) { const XError& e=(const XError&)event; uint8 code=e.code;
        if(e.major==XRender::EXT) {
            int reqSize=sizeof(XRender::requests)/sizeof(*XRender::requests);
            if(code>=XRender::errorBase && code<=XRender::errorBase+XRender::errorCount) { code-=XRender::errorBase;
                assert(code<sizeof(XRender::xErrors)/sizeof(*XRender::xErrors));
                log("XError",XRender::xErrors[code],"request",e.minor<reqSize?String(XRender::requests[e.minor]):dec(e.minor));
            } else {
                assert(code<sizeof(::errors)/sizeof(*::errors));
                log("XError",::errors[code],"request",e.minor<reqSize?String(XRender::requests[e.minor]):dec(e.minor));
            }
        } else if(e.major==Shm::EXT) {
            int reqSize=sizeof(Shm::requests)/sizeof(*Shm::requests);
            if(code>=Shm::errorBase && code<=Shm::errorBase+Shm::errorCount) { code-=Shm::errorBase;
                assert(code<sizeof(Shm::xErrors)/sizeof(*Shm::xErrors));
                log("XError",Shm::xErrors[code],"request",e.minor<reqSize?String(Shm::requests[e.minor]):dec(e.minor));
            } else {
                assert(code<sizeof(::errors)/sizeof(*::errors));
                log("XError",::xErrors[code],"request",e.minor<reqSize?String(Shm::requests[e.minor]):dec(e.minor));
            }
        } else {
            assert(code<sizeof(::errors)/sizeof(*::errors),code,e.major);
            int reqSize=sizeof(::requests)/sizeof(*::requests);
            log("XError",::xErrors[code],"request",e.major<reqSize?String(::requests[e.major]):dec(e.major),"minor",e.minor);
        }
    }
    else if(type==1) error("Unexpected reply");
    else { const XEvent& e=event; type&=0b01111111; //msb set if sent by SendEvent
        assert_(!window);
        window=this;
        /**/ if(type==MotionNotify) {
            cursorPosition = int2(e.x,e.y);
            cursorState = e.state;
            motionPending = true;
        }
        else if(type==Shm::event+Shm::Completion) { assert_(!remote); if(displayed) displayed(); }
        else {
            if(motionPending) {
                motionPending = false;
                Cursor lastCursor = cursor; cursor=Cursor::Arrow;
                if(drag && cursorState&Button1Mask && drag->mouseEvent(cursorPosition, size, Widget::Motion, Widget::LeftButton)) {} //needUpdate=true; //FIXME: Assumes all widgets supports partial updates
                else if(widget->mouseEvent(cursorPosition, size, Widget::Motion, (cursorState&Button1Mask)?Widget::LeftButton:Widget::None)) {} //needUpdate=true; //FIXME: Assumes all widgets supports partial updates
                if(cursor!=lastCursor) setCursor(cursor);
            }
            if(type==ButtonPress) {
                Widget* focus=this->focus; this->focus=0;
                dragStart=int2(e.rootX,e.rootY), dragPosition=position, dragSize=size;
                if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Press, (Widget::Button)e.key) || this->focus!=focus) {} //needUpdate=true; //FIXME: Assumes all widgets supports partial updates
            }
            else if(type==ButtonRelease) {
                drag=0;
                if(e.key <= Widget::RightButton && widget->mouseEvent(int2(e.x,e.y), size, Widget::Release, (Widget::Button)e.key)) {} //needUpdate=true; //FIXME: Assumes all widgets supports partial updates
            }
            else if(type==KeyPress) keyPress(KeySym(e.key, focus==directInput ? 0 : e.state), (Modifiers)e.state);
            else if(type==KeyRelease) keyRelease(KeySym(e.key, focus==directInput ? 0 : e.state), (Modifiers)e.state);
            else if(type==EnterNotify || type==LeaveNotify) {
                if(type==LeaveNotify && hideOnLeave) hide();
                if(widget->mouseEvent( int2(e.x,e.y), size, type==EnterNotify?Widget::Enter:Widget::Leave,
                                       e.state&Button1Mask?Widget::LeftButton:Widget::None) ) {} //needUpdate=true; //FIXME: Assumes all widgets supports partial updates
            }
            else if(type==Expose) { if(!e.expose.count && !(e.expose.x==0 && e.expose.w<=2)) needRender=true; }
            else if(type==UnmapNotify) mapped=false;
            else if(type==MapNotify) mapped=true;
            else if(type==ReparentNotify) {}
            else if(type==ConfigureNotify) {
                position=int2(e.configure.x,e.configure.y); int2 size=int2(e.configure.w,e.configure.h);
                if(this->size!=size) { this->size=size; needRender=true; }
            }
            else if(type==GravityNotify) {}
            else if(type==ClientMessage) {
                function<void()>* action = actions.find(Escape); // Translates to Escape keyPress event
                if(action) (*action)(); // Local window action
                else if(focus && focus->keyPress(Escape, NoModifiers)) ; //needUpdate=true; //FIXME: Assumes all widgets supports partial updates
                else exit(0); // Exits application by default
            }
            else if( type==DestroyNotify || type==MappingNotify || type==LastEvent) {}
            else log("Event", type<sizeof(::events)/sizeof(*::events)?::events[type]:str(type));
        }
        window=0;
    }
}
uint Window::send(const ref<byte>& request) { write(request); return ++sequence; }
template<class T> T Window::readReply(const ref<byte>& request) {
    Locker lock(this->lock); // Prevents a concurrent thread from reading the reply
    uint sequence = send(request);
    bool pendingEvents = false;
    for(;;) { uint8 type = read<uint8>();
        if(type==0) {
            XError e=read<XError>(); processEvent(0,(XEvent&)e);
            if(e.seq==sequence) { if(pendingEvents) queue(); T t; raw(t).clear(); return t; }
        }
        else if(type==1) {
            T reply = read<T>();
            assert(reply.seq==sequence);
            if(pendingEvents) queue();
            return reply;
        }
        else { eventQueue << QEvent{type, unique<XEvent>(read<XEvent>())}; semaphore.release(1); pendingEvents=true; } // Queues events to avoid reentrance
    }
}

void Window::show() { if(mapped) return; {MapWindow r; r.id=id; send(raw(r));} {RaiseWindow r; r.id=id; send(raw(r));} }
void Window::hide() { UnmapWindow r; r.id=id; send(raw(r)); }
// Configuration
void Window::setPosition(int2 position) {
    if(position.x<0) position.x=displaySize.x+position.x;
    if(position.y<0) position.y=displaySize.y+position.y;
    setGeometry(position,this->size);
}
void Window::setSize(int2 size) {
    if(size.x<0||size.y<0) {
        int2 hint=widget->sizeHint();
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=displaySize.x;
    if(size.x>displaySize.x) size.x=max(1280,displaySize.x);
    if(size.y==0 || size.y>displaySize.y-16) size.y=displaySize.y-16;
    setGeometry(this->position,size);
}
void Window::setGeometry(int2 position, int2 size) {
    if(anchor&Left && anchor&Right) position.x=(displaySize.x-size.x)/2;
    else if(anchor&Left) position.x=0;
    else if(anchor&Right) position.x=displaySize.x-size.x;
    if(anchor&Top && anchor&Bottom) position.y=16+(displaySize.y-16-size.y)/2;
    else if(anchor&Top) position.y=16;
    else if(anchor&Bottom) position.y=displaySize.y-size.y;
    if(position!=this->position && size!=this->size) {
        SetGeometry r; r.id=id+XWindow; r.x=position.x; r.y=position.y; r.w=size.x, r.h=size.y; send(raw(r));
    }
    else if(position!=this->position) {SetPosition r; r.id=id+XWindow; r.x=position.x, r.y=position.y; send(raw(r));}
    else if(size!=this->size) {SetSize r; r.id=id+XWindow; r.w=size.x, r.h=size.y; send(raw(r));}
}

// Keyboard
Key Window::KeySym(uint8 code, uint8 state) {
    //FIXME: not atomic
    GetKeyboardMapping req; GetKeyboardMappingReply r=readReply<GetKeyboardMappingReply>(({req.keycode=code; raw(req);}));
    ::buffer<uint> keysyms = read<uint>(r.numKeySymsPerKeyCode);
    if(!keysyms) error(code,state);
    if(keysyms.size>=2 && keysyms[1]>=0xff80 && keysyms[1]<=0xffbd) state|=1;
    return (Key)keysyms[state&1 && keysyms.size>=2];
}
uint Window::KeyCode(Key sym) {
    uint keycode=0;
    for(uint i: range(minKeyCode,maxKeyCode+1)) if(KeySym(i,0)==sym) { keycode=i; break;  }
    if(!keycode) { if(sym==0x1008ff14) return 172; /*FIXME*/ log("Unknown KeySym",int(sym)); return sym; }
    return keycode;
}

function<void()>& Window::globalAction(Key key) {
    uint code = KeyCode(key);
    if(code){GrabKey r; r.window=root; r.keycode=code; send(raw(r));}
    return actions.insert(key, []{});
}

// Properties
uint Window::Atom(const string& name) {
    InternAtom r; r.length=name.size; r.size+=align(4,r.length)/4;
    return readReply<InternAtomReply>(String(raw(r)+name+pad(4,r.length))).atom;
}
template<class T> array<T> Window::getProperty(uint window, const string& name, uint size) {
    //FIXME: not atomic
    GetProperty r; GetPropertyReply reply=readReply<GetPropertyReply>(({r.window=window; r.property=Atom(name); r.length=size; raw(r); }));
    {uint size=reply.length*reply.format/8;  array<T> a; if(size) a=read<T>(size/sizeof(T)); int pad=align(4,size)-size; if(pad) read(pad); return a; }
}
template array<uint> Window::getProperty(uint window, const string& name, uint size);
template array<byte> Window::getProperty(uint window, const string& name, uint size);

void Window::setType(const string& type) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_WINDOW_TYPE"_); r.type=Atom("ATOM"_); r.format=32;
    r.length=1; r.size+=r.length; send(String(raw(r)+raw(Atom(type))));
}
void Window::setTitle(const string& title) {
    this->title = copy(String(title));
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_NAME"_); r.type=Atom("UTF8_STRING"_); r.format=8;
    r.length=title.size; r.size+=align(4, r.length)/4; send(String(raw(r)+title+pad(4,title.size)));
}
void Window::setIcon(const Image& icon) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_ICON"_); r.type=Atom("CARDINAL"_); r.format=32;
    r.length=2+icon.width*icon.height; r.size+=r.length; send(String(raw(r)+raw(icon.width)+raw(icon.height)+(ref<byte>)icon));
}

String Window::getSelection(bool clipboard) {
    GetSelectionOwner r;
    uint owner = readReply<GetSelectionOwnerReply>(({ if(clipboard) r.selection=Atom("CLIPBOARD"_); raw(r); })).owner;
    if(!owner) return String();
    {ConvertSelection r; r.requestor=id; if(clipboard) r.selection=Atom("CLIPBOARD"_); r.target=Atom("UTF8_STRING"_); send(raw(r));}
    bool pendingEvents = false;
    for(Locker lock(this->lock);;) { // Lock prevents a concurrent thread from reading the SelectionNotify
        uint8 type = read<uint8>();
        if((type&0b01111111)==SelectionNotify) { read<XEvent>(); break; }
        eventQueue << QEvent{type, unique<XEvent>(read<XEvent>())};
        semaphore.release(1);
        pendingEvents = true;
    }
    if(pendingEvents) queue();
    return getProperty<byte>(id,"UTF8_STRING"_);
}

void Window::setCursor(Cursor) {}

// Snapshot
Image Window::getSnapshot() {
    assert_(!remote);
    Image buffer;
    buffer.stride=buffer.width=displaySize.x, buffer.height=displaySize.y;
    int shm = check( shmget(0, buffer.height*buffer.stride*sizeof(byte4) , IPC_CREAT | 0777) );
    buffer.data = (byte4*)check( shmat(shm, 0, 0) ); assert(buffer.data);
    {Shm::Attach r; r.seg=id+SnapshotSegment; r.shm=shm; send(raw(r));}
    {Shm::GetImage r; r.window=root; r.w=buffer.width, r.h=buffer.height; r.seg=id+SnapshotSegment; readReply<Shm::GetImageReply>(raw(r));}
    {Shm::Detach r; r.seg=id+SnapshotSegment; send(raw(r));}
    Image image = copy(buffer);
    for(uint y: range(image.height)) for(uint x: range(image.width)) {byte4& p=image(x,y); p.a=0xFF;}
    shmdt(buffer.data);
    shmctl(shm, IPC_RMID, 0);
    return image;
}

void Window::setDisplay(bool displayState) { log("Unimplemented X11 setDisplay", displayState); }
#endif

void Window::renderBackground(Image& target) {
    assert_(target.data >= this->target.buffer.begin() && target.data+target.height*target.stride <= this->target.buffer.end());
    if(background==Oxygen) { // Oxygen-like radial gradient background
        assert_(target.size() == this->target.size());
        const int y0 = -32-8, splitY = min(300, 3*size.y/4);
        const vec3 radial = vec3(246./255); // linear
        const vec3 top = vec3(221, 223, 225); // sRGB
        const vec3 bottom = vec3(184, 187, 194); // sRGB
        const vec3 middle = (bottom+top)/2.f; //FIXME
        // Draws upper linear gradient
        for(int y: range(0, max(0, y0+splitY/2))) {
            float t = (float) (y-y0) / (splitY/2);
            for(int x: range(size.x)) target(x,y) = byte4(byte3(round((1-t)*top + t*middle)), 0xFF);
        }
        for(int y: range(max(0, y0+splitY/2), min(size.y, y0+splitY))) {
            float t = (float) (y- (y0 + splitY/2)) / (splitY/2);
            byte4 verticalGradient (byte3((1-t)*middle + t*bottom), 0xFF); // mid -> dark
            for(int x: range(size.x)) target(x,y) = verticalGradient;
        }
        // Draws lower flat part
        for(int y: range(max(0, y0+splitY), size.y)) for(int x: range(size.x)) target(x,y) = byte4(byte3(bottom), 0xFF);
        // Draws upper radial gradient (600x64)
        const int w = min(600, size.x), h = 64;
        for(int y: range(0, min(size.y, y0+h))) for(int x: range((size.x-w)/2, (size.x+w)/2)) {
            const float cx = size.x/2, cy = y0+h/2;
            float r = sqrt(sq((x-cx)/(w/2)) + sq((y-cy)/(h/2)));
            const float r0 = 0./4, r1 = 2./4, r2 = 3./4, r3 = 4./4;
            const float a0 = 255./255, a1 = 101./255, a2 = 37./255, a3 = 0./255;
            /***/ if(r < r1) { float t = (r-r0) / (r1-r0); blend(target, x, y, radial, (1-t)*a0 + t*a1); }
            else if(r < r2) { float t = (r-r1) / (r2-r1); blend(target, x, y, radial, (1-t)*a1 + t*a2); }
            else if(r < r3) { float t = (r-r2) / (r3-r2); blend(target, x, y, radial, (1-t)*a2 + t*a3); }
        }
    }
    else if(background==White) fill(target, Rect(target.size()), white);
    else if(background==Black)  fill(target, Rect(target.size()), black);
}

void Window::keyPress(Key key, Modifiers modifiers) {
    if(focus && focus->keyPress(key, modifiers)) ; //needUpdate=true; //FIXME: Assumes all widgets supports partial updates; Normal keyPress event
    else {
        function<void()>* action = actions.find(key);
        function<void()>* longAction = longActions.find(key);
        if(longAction) { // Schedules long action
            longActionTimers.insert(key, unique<Timer>(1000, [this,key,longAction]{longActionTimers.remove(key); (*longAction)();}));
        }
        else if(action) (*action)(); // Local window action
    }
}


void Window::keyRelease(Key key, Modifiers modifiers) {
    if(focus && focus->keyRelease(key, modifiers)) ; //needUpdate=true; //FIXME: Assumes all widgets supports partial updates; Normal keyRelease event
    else if(longActionTimers.contains(key)) {
        longActionTimers.remove(key); // Removes long action before it triggers
        function<void()>* action = actions.find(key);
        if(action) (*action)(); // Executes any short action instead
    }
}
