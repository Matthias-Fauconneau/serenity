#include "window.h"
#include "display.h"
#include "widget.h"
#include "file.h"
#include "data.h"
#include "time.h"
#include "png.h"
#include "linux.h"
#include "x.h"

// Globals
namespace Shm { int EXT, event, errorBase; } using namespace Shm;
namespace XRender { int EXT, event, errorBase; } using namespace XRender;
int2 displaySize;

static __thread Window* window; // Current window for Widget event and render methods
void setFocus(Widget* widget) { assert(window); window->focus=widget; }
bool hasFocus(Widget* widget) { assert(window); return window->focus==widget; }
void setDrag(Widget* widget) { assert(window); window->drag=widget; }
String getSelection(bool clipboard) { assert(window); return window->getSelection(clipboard); }
void setCursor(Rect region, Cursor cursor) { assert(window); return window->setCursor(region,cursor); }

Window::Window(Widget* widget, int2 size, const string& title, const Image& icon, const string& type, Thread& thread)
    : Socket(PF_LOCAL, SOCK_STREAM), Poll(Socket::fd,POLLIN,thread), widget(widget), overrideRedirect(title.size?false:true) {
    String path = "/tmp/.X11-unix/X"_+getenv("DISPLAY"_).slice(1,1);
    struct sockaddr_un { uint16 family=1; char path[108]={}; } addr; copy(addr.path,path.data,path.size);
    if(check(connect(Socket::fd,(const sockaddr*)&addr,2+path.size),path)) error("X connection failed");
    {ConnectionSetup r;
        if(existsFile(".Xauthority"_,home())) {
            BinaryData s(readFile(".Xauthority"_,home())); s.advance(4); s.untilNull();/*host*/ s.untilNull();/*display*/
            send(String(raw(r)+s.read<byte>(align(4,(r.nameSize=18))+(r.dataSize=16))));
        } else send(raw(r));
    }
    {ConnectionSetupReply r=read<ConnectionSetupReply>(); assert(r.status==1,string((byte*)&r.release,r.reason-1));
        read(align(4,r.vendorLength));
        read<XFormat>(r.numFormats);
        for(int i=0;i<r.numScreens;i++){ Screen screen=read<Screen>();
            for(int i=0;i<screen.numDepths;i++) { Depth depth = read<Depth>();
                if(depth.numVisualTypes) for(VisualType visualType: read<VisualType>(depth.numVisualTypes)) {
                    if(!visual && depth.depth==32) { displaySize=int2(screen.width,screen.height); root = screen.root; visual=visualType.id; }
                }
            }
        }
        id=r.ridBase;
        minKeyCode=r.minKeyCode, maxKeyCode=r.maxKeyCode;
    }
    assert(visual);

    {QueryExtensionReply r=readReply<QueryExtensionReply>(({QueryExtension r; r.length="MIT-SHM"_.size; r.size+=align(4,r.length)/4; String(raw(r)+"MIT-SHM"_+pad(4,r.length));}));
        Shm::EXT=r.major; Shm::event=r.firstEvent; Shm::errorBase=r.firstError;}

    {QueryExtensionReply r=readReply<QueryExtensionReply>(({QueryExtension r; r.length="RENDER"_.size; r.size+=align(4,r.length)/4; String(raw(r)+"RENDER"_+pad(4,r.length));}));
        XRender::EXT=r.major; XRender::event=r.firstEvent; XRender::errorBase=r.firstError; }
    {QueryPictFormatsReply r=readReply<QueryPictFormatsReply>(raw(QueryPictFormats()));
        array<PictFormInfo> formats = read<PictFormInfo>( r.numFormats);
        for(uint unused i: range(r.numScreens)) { PictScreen screen = read<PictScreen>();
            for(uint unused i: range(screen.numDepths)) { PictDepth depth = read<PictDepth>();
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
    create();
    setTitle(title);
    setIcon(icon);
    setType(type);
}

void Window::create() {
    assert(!created);
    {CreateWindow r; r.id=id+XWindow; r.parent=root; r.x=position.x; r.y=position.y; r.width=size.x, r.height=size.y; r.visual=visual;
        r.colormap=id+Colormap;
        r.overrideRedirect=overrideRedirect;
        r.eventMask=StructureNotifyMask|KeyPressMask|KeyReleaseMask|ButtonPressMask|ButtonReleaseMask|EnterWindowMask|LeaveWindowMask|PointerMotionMask|ExposureMask; send(raw(r));}
    {CreateGC r; r.context=id+GContext; r.window=id+XWindow; send(raw(r));}
    {ChangeProperty r; r.window=id+XWindow; r.property=Atom("WM_PROTOCOLS"_); r.type=Atom("ATOM"_); r.format=32;
        r.length=1; r.size+=r.length; send(String(raw(r)+raw(Atom("WM_DELETE_WINDOW"_))));}
    created = true;
    registerPoll();
}
void Window::destroy() {
    if(!created) return;
    {FreeGC r; r.context=id+GContext; send(raw(r));}
    {DestroyWindow r; r.id=id+XWindow; send(raw(r));}
    created = false;
    unregisterPoll();
}

// Render
void Window::event() {
    window=this;
    if(revents==IDLE) {
        if(autoResize) {
            int2 hint = widget->sizeHint();
            if(hint != size) { setSize(hint); return; }
        }
        assert(size);
        currentClip=Rect(size);

        if(state!=Idle) { state=Wait; return; }
        if(buffer.width != (uint)size.x || buffer.height != (uint)size.y) {
            if(shm) {
                {Shm::Detach r; r.seg=id+Segment; send(raw(r));}
                shmdt(buffer.data);
                shmctl(shm, IPC_RMID, 0);
            }
            buffer.width=size.x, buffer.height=size.y, buffer.stride=align(16,size.x);
            shm = check( shmget(0, buffer.height*buffer.stride*sizeof(byte4) , IPC_CREAT | 0777) );
            buffer.data = (byte4*)check( shmat(shm, 0, 0) ); assert(buffer.data);
            {Shm::Attach r; r.seg=id+Segment; r.shm=shm; send(raw(r));}
        }
        framebuffer=share(buffer);
        currentClip=Rect(size);

        if(clearBackground) {
            if(backgroundCenter==backgroundColor) {
                fill(Rect(size),vec4(backgroundColor,backgroundColor,backgroundColor,backgroundOpacity));
            } else { // Oxygen-like radial gradient background
                const int radius=256;
                int w=size.x, cx=w/2, x0=max(0,cx-radius), x1=min(w,cx+radius), h=min(radius,size.y),
                        a=0xFF*backgroundOpacity, scale = (radius*radius)/a;
                if(x0>0 || x1<w || h<size.y) fill(Rect(size),vec4(backgroundColor,backgroundColor,backgroundColor,backgroundOpacity));
                uint* dst=(uint*)framebuffer.data;
                for(int y=0;y<h;y++) for(int x=x0;x<x1;x++) {
                    int X=x-cx, Y=y, d=(X*X+Y*Y), t=min(0xFF,d/scale), g = (0xFF*backgroundColor*t+0xFF*backgroundCenter*(0xFF-t))/0xFF;
                    dst[y*framebuffer.stride+x]= a<<24 | g<<16 | g<<8 | g;
                }
            }
        }
        widget->render(0,size);
        assert(!clipStack);

        if(featherBorder) { //feather borders
            const bool corner = 1;
            if(position.y>16) for(int x=0;x<size.x;x++) framebuffer(x,0) /= 2;
            if(position.x>0) for(int y=corner;y<size.y-corner;y++) framebuffer(0,y) /= 2;
            if(position.x+size.x<displaySize.x-1) for(int y=corner;y<size.y-corner;y++) framebuffer(size.x-1,y) /= 2;
            if(position.y+size.y>16 && position.y+size.y<displaySize.y-1) for(int x=0;x<size.x;x++) framebuffer(x,size.y-1) /= 2;
        }
        Shm::PutImage r; r.window=id+XWindow; r.context=id+GContext; r.seg=id+Segment;
        r.totalW=framebuffer.stride; r.totalH=framebuffer.height;
        r.srcW=size.x; r.srcH=size.y; send(raw(r));
        state=Server;
        framebuffer=Image();
        frameReady();
    } else for(;;) {
        readLock.lock();
        if(!poll()) { readLock.unlock(); break; }
        uint8 type = read<uint8>();
        XEvent e = read<XEvent>();
        readLock.unlock();
        processEvent(type, e);
        while(eventQueue) { readLock.lock(); QEvent e=eventQueue.take(0); readLock.unlock(); processEvent(e.type, e.event); }
    };
    window=0;
}

// Events
void Window::processEvent(uint8 type, const XEvent& event) {
    if(type==0) { const XError& e=(const XError&)event; uint8 code=e.code;
        if(e.major==XRender::EXT) {
            int reqSize=sizeof(XRender::requests)/sizeof(*XRender::requests);
            if(code>=XRender::errorBase && code<=XRender::errorBase+XRender::errorCount) { code-=XRender::errorBase;
                assert(code<sizeof(XRender::errors)/sizeof(*XRender::errors));
                log("XError",XRender::errors[code],"seq:",e.seq,"id",e.id,"request",e.minor<reqSize?String(XRender::requests[e.minor]):dec(e.minor));
            } else {
                assert(code<sizeof(::errors)/sizeof(*::errors));
                log("XError",::errors[code],"seq:",e.seq,"id",e.id,"request",e.minor<reqSize?String(XRender::requests[e.minor]):dec(e.minor));
            }
        } else if(e.major==Shm::EXT) {
            int reqSize=sizeof(Shm::requests)/sizeof(*Shm::requests);
            if(code>=Shm::errorBase && code<=Shm::errorBase+Shm::errorCount) { code-=Shm::errorBase;
                assert(code<sizeof(Shm::errors)/sizeof(*Shm::errors));
                log("XError",Shm::errors[code],"seq:",e.seq,"id",e.id,"request",e.minor<reqSize?String(Shm::requests[e.minor]):dec(e.minor));
            } else {
                assert(code<sizeof(::errors)/sizeof(*::errors));
                log("XError",::errors[code],"seq:",e.seq,"id",e.id,"request",e.minor<reqSize?String(Shm::requests[e.minor]):dec(e.minor));
            }
        } else {
            assert(code<sizeof(::errors)/sizeof(*::errors),code,e.major);
            int reqSize=sizeof(::requests)/sizeof(*::requests);
            log("XError",::errors[code],"seq:",e.seq,"id",e.id,"request",e.major<reqSize?String(::requests[e.major]):dec(e.major),"minor",e.minor);
        }
    }
    else if(type==1) error("Unexpected reply");
    else { const XEvent& e=event; type&=0b01111111; //msb set if sent by SendEvent
        /**/ if(type==MotionNotify) {
            cursorPosition = int2(e.x,e.y);
            Cursor lastCursor = cursor; cursor=Cursor::Arrow;
            if(drag && e.state&Button1Mask && drag->mouseEvent(int2(e.x,e.y), size, Widget::Motion, Widget::LeftButton)) render();
            else if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Motion, (e.state&Button1Mask)?Widget::LeftButton:Widget::None)) render();
            else if(anchor==Float) {
                if(!(e.state&Button1Mask)) { dragStart=int2(e.rootX,e.rootY); dragPosition=position; dragSize=size; }
                bool top = dragStart.y<dragPosition.y+1, bottom = dragStart.y>=dragPosition.y+dragSize.y-1;
                bool left = dragStart.x<dragPosition.x+1, right = dragStart.x>=dragPosition.x+dragSize.x-1;
                if(e.state&Button1Mask) {
                    int2 position=dragPosition, size=dragSize, delta=int2(e.rootX,e.rootY)-dragStart;
                    if(top && left) position+=delta, size-=delta;
                    else if(top && right) position.y+=delta.y, size+=int2(delta.x,-delta.y);
                    else if(bottom && left) position.x+=delta.x, size+=int2(-delta.x,delta.y);
                    else if(bottom && right) size+=delta;
                    else if(top) position.y+=delta.y, size.y-=delta.y;
                    else if(bottom) size.y+=delta.y;
                    else if(left) position.x+=delta.x, size.x-=delta.x;
                    else if(right) size.x+=delta.x;
                    else position+=delta;
                    position=clip(int2(0,16),position,displaySize), size=clip(int2(16,16),size,displaySize-int2(0,16));
                    setGeometry(position,size);
                } else {
                    if((top && left)||(bottom && right)) cursor=Cursor::FDiagonal;
                    else if((top && right)||(bottom && left)) cursor=Cursor::BDiagonal;
                    else if(top || bottom) cursor=Cursor::Vertical;
                    else if(left || right) cursor=Cursor::Horizontal;
                }
            }
            if(cursor!=lastCursor) setCursor(cursor);
        }
        else if(type==ButtonPress) {
            Widget* focus=this->focus; this->focus=0;
            dragStart=int2(e.rootX,e.rootY), dragPosition=position, dragSize=size;
            if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Press, (Widget::Button)e.key) || this->focus!=focus) render();
        }
        else if(type==ButtonRelease) {
            drag=0;
            if(e.key <= Widget::RightButton && widget->mouseEvent(int2(e.x,e.y), size, Widget::Release, (Widget::Button)e.key)) render();
        } else if(type==KeyPress) {
            Key key = KeySym(e.key, e.state);
            if(focus && focus->keyPress(key, (Modifiers)e.state)) render(); //normal keyPress event
            else {
                signal<>* shortcut = shortcuts.find(key);
                if(shortcut) (*shortcut)(); //local window shortcut
            }
        }
        else if(type==KeyRelease) {
            Key key = KeySym(e.key, e.state);
            if(focus && focus->keyRelease(key, (Modifiers)e.state)) render();
        }
        else if(type==EnterNotify || type==LeaveNotify) {
            if(type==LeaveNotify && hideOnLeave) hide();
            signal<>* shortcut = shortcuts.find(Widget::Leave);
            if(shortcut) (*shortcut)(); //local window shortcut
            if(widget->mouseEvent( int2(e.x,e.y), size, type==EnterNotify?Widget::Enter:Widget::Leave,
                                   e.state&Button1Mask?Widget::LeftButton:Widget::None) ) render();
        }
        else if(type==Expose) { if(!e.expose.count) render(); }
        else if(type==UnmapNotify) mapped=false;
        else if(type==MapNotify) mapped=true;
        else if(type==ReparentNotify) {}
        else if(type==ConfigureNotify) {
            position=int2(e.configure.x,e.configure.y); int2 size=int2(e.configure.w,e.configure.h);
            if(this->size!=size) { this->size=size; render(); }
        }
        else if(type==GravityNotify) {}
        else if(type==ClientMessage) {
            signal<>* shortcut = shortcuts.find(Escape);
            if(shortcut) (*shortcut)(); //local window shortcut
            else widget->keyPress(Escape, NoModifiers);
        }
        else if(type==Shm::event+Shm::Completion) { if(state==Wait) render(); state=Idle; }
        else if( type==DestroyNotify || type==MappingNotify) {}
        else log("Event", type<sizeof(::events)/sizeof(*::events)?::events[type]:str(type));
    }
}
void Window::send(const ref<byte>& request) { write(request); sequence++; }
template<class T> T Window::readReply(const ref<byte>& request) {
    Locker lock(readLock);
    send(request);
    for(;;) { uint8 type = read<uint8>();
        if(type==0){XError e=read<XError>(); processEvent(0,(XEvent&)e); if(e.seq==sequence) { T t; clear((byte*)&t,sizeof(T)); return t; }}
        else if(type==1) return read<T>();
        else eventQueue << QEvent{type, read<XEvent>()}; //queue events to avoid reentrance
    }
}
void Window::render() { /*if(mapped)*/ queue(); }

void Window::show() { {MapWindow r; r.id=id; send(raw(r));} {RaiseWindow r; r.id=id; send(raw(r));} }
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
    if(position!=this->position && size!=this->size) {SetGeometry r; r.id=id+XWindow; r.x=position.x; r.y=position.y; r.w=size.x, r.h=size.y; send(raw(r));}
    else if(position!=this->position) {SetPosition r; r.id=id+XWindow; r.x=position.x, r.y=position.y; send(raw(r));}
    else if(size!=this->size) {SetSize r; r.id=id+XWindow; r.w=size.x, r.h=size.y; send(raw(r));}
}

// Keyboard
Key Window::KeySym(uint8 code, uint8 state) {
    GetKeyboardMapping req; GetKeyboardMappingReply r=readReply<GetKeyboardMappingReply>(({req.keycode=code; raw(req);}));
    array<uint> keysyms = read<uint>(r.numKeySymsPerKeyCode);
    if(!keysyms) return (Key)0;
    if(keysyms.size>=2 && keysyms[1]>=0xff80 && keysyms[1]<=0xffbd) state|=1;
    return (Key)keysyms[state&1 && keysyms.size>=2];
}
uint Window::KeyCode(Key sym) {
    uint keycode=0;
    for(uint i: range(minKeyCode,maxKeyCode)) if(KeySym(i,0)==sym) { keycode=i; break;  }
    if(!keycode) warn("Unknown KeySym",int(sym));
    return keycode;
}

signal<>& Window::localShortcut(Key key) { return shortcuts.insert((uint16)key); }
signal<>& Window::globalShortcut(Key key) {
    uint code = KeyCode(key);
    if(code){GrabKey r; r.window=root; r.keycode=code; send(raw(r));}
    return shortcuts.insert((uint16)key);
}

// Properties
uint Window::Atom(const string& name) {
    InternAtom r; r.length=name.size; r.size+=align(4,r.length)/4; return readReply<InternAtomReply>(String(raw(r)+name+pad(4,r.length))).atom;
}
template<class T> array<T> Window::getProperty(uint window, const string& name, uint size) {
    GetProperty r; GetPropertyReply reply=readReply<GetPropertyReply>(({r.window=window; r.property=Atom(name); r.length=size; raw(r); }));
    { uint size=reply.length*reply.format/8; array<T> a; if(size) a=read<T>(size/sizeof(T)); int pad=align(4,size)-size; if(pad) read(pad); return a; }
}
template array<uint> Window::getProperty(uint window, const string& name, uint size);
template array<byte> Window::getProperty(uint window, const string& name, uint size);

void Window::setType(const string& type) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_WINDOW_TYPE"_); r.type=Atom("ATOM"_); r.format=32;
    r.length=1; r.size+=r.length; send(String(raw(r)+raw(Atom(type))));
}
void Window::setTitle(const string& title) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_NAME"_); r.type=Atom("UTF8_STRING"_); r.format=8;
    r.length=title.size; r.size+=align(4, r.length)/4; send(String(raw(r)+title+pad(4,title.size)));
}
void Window::setIcon(const Image& icon) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_ICON"_); r.type=Atom("CARDINAL"_); r.format=32;
    r.length=2+icon.width*icon.height; r.size+=r.length; send(String(raw(r)+raw(icon.width)+raw(icon.height)+(ref<byte>)icon));
}

String Window::getSelection(bool clipboard) {
    GetSelectionOwner r; uint owner = readReply<GetSelectionOwnerReply>(({ if(clipboard) r.selection=Atom("CLIPBOARD"_); raw(r); })).owner;
    if(!owner) return String();
    {ConvertSelection r; r.requestor=id; if(clipboard) r.selection=Atom("CLIPBOARD"_); r.target=Atom("UTF8_STRING"_); send(raw(r));}
    for(;;) {
        readLock.lock();
        uint8 type = read<uint8>();
        if((type&0b01111111)==SelectionNotify) { read<XEvent>(); readLock.unlock(); return getProperty<byte>(id,"UTF8_STRING"_); }
        else eventQueue << QEvent{type, read<XEvent>()}; //queue events to avoid reentrance
        readLock.unlock();
    }
}

// Cursor
ICON(arrow) ICON(horizontal) ICON(vertical) ICON(fdiagonal) ICON(bdiagonal) ICON(move) ICON(text)
const Image& Window::cursorIcon(Cursor cursor) {
    static const Image& (*icons[])() = { arrowIcon, horizontalIcon, verticalIcon, fdiagonalIcon, bdiagonalIcon, moveIcon, textIcon };
    return icons[(uint)cursor]();
}
int2 Window::cursorHotspot(Cursor cursor) {
    static constexpr int2 hotspots[] = { int2(5,0), int2(11,11), int2(11,11), int2(11,11), int2(11,11), int2(16,15), int2(4,9) };
    return hotspots[(uint)cursor];
}
void Window::setCursor(Cursor cursor, uint window) {
    const Image& image = cursorIcon(cursor); int2 hotspot = cursorHotspot(cursor);
    Image premultiplied(image.width,image.height);
    for(uint y: range(image.height)) for(uint x: range(image.width)) {
        byte4 p=image(x,y); premultiplied(x,y)=byte4(p.b*p.a/255,p.g*p.a/255,p.r*p.a/255,p.a);
    }
    {::CreatePixmap r; r.pixmap=id+Pixmap; r.window=id; r.w=image.width, r.h=image.height; send(raw(r));}
    {::PutImage r; r.drawable=id+Pixmap; r.context=id+GContext; r.w=image.width, r.h=image.height; r.size+=r.w*r.h;
        send(String(raw(r)+ref<byte>(premultiplied)));}
    {XRender::CreatePicture r; r.picture=id+Picture; r.drawable=id+Pixmap; r.format=format; send(raw(r));}
    {XRender::CreateCursor r; r.cursor=id+XCursor; r.picture=id+Picture; r.x=hotspot.x; r.y=hotspot.y; send(raw(r));}
    {SetWindowCursor r; r.window=window?:id; r.cursor=id+XCursor; send(raw(r));}
    {FreeCursor r; r.cursor=id+XCursor; send(raw(r));}
    {FreePicture r; r.picture=id+Picture; send(raw(r));}
    {FreePixmap r; r.pixmap=id+Pixmap; send(raw(r));}
}
void Window::setCursor(Rect region, Cursor cursor) { if(region.contains(cursorPosition)) this->cursor=cursor; }

// Snapshot
Image Window::getSnapshot() {
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
