#include "window.h"
#include "x.h"
#include "display.h"
#include "widget.h"
#include "file.h"
#include "stream.h"
#include "linux.h"
#include "debug.h"
#include "time.h"
#include "png.h"

namespace Shm { int EXT, event, error; } using namespace Shm;
namespace Render { int EXT, event, error; } using namespace Render;
int2 display;
Widget* focus;
Widget* drag;
Window* current;

string getSelection() { assert(current); return current->getSelection(); }

Window::Window(Widget* widget, int2 size, const ref<byte>& title, const Image& icon, const ref<byte>& type) : widget(widget), overrideRedirect(title.size?false:true) {
    registerPoll(socket(PF_LOCAL, SOCK_STREAM, 0));
    string path = "/tmp/.X11-unix/X"_+getenv("DISPLAY"_).slice(1);
    sockaddr_un addr; copy(addr.path,path.data(),path.size());
    check_(connect(fd,(sockaddr*)&addr,2+path.size()),path);
    {ConnectionSetup r;
        string authority = getenv("HOME"_)+"/.Xauthority"_;
        if(existsFile(authority)) send(string(raw(r)+readFile(authority).slice(18,align(4,(r.nameSize=18))+(r.dataSize=16))));
        else send(raw(r)); }
    uint visual=0;
    {ConnectionSetupReply r=read<ConnectionSetupReply>(fd); assert(r.status==1,ref<byte>((byte*)&r.release,r.reason-1));
        read(fd,align(4,r.vendorLength));
        read<XFormat>(fd,r.numFormats);
        for(int i=0;i<r.numScreens;i++){ Screen screen=read<Screen>(fd);
            for(int i=0;i<screen.numDepths;i++) { Depth depth = read<Depth>(fd);
                if(depth.numVisualTypes) for(VisualType visualType: read<VisualType>(fd,depth.numVisualTypes)) {
                    if(!visual && depth.depth==32) { display=int2(screen.width,screen.height); root = screen.root; visual=visualType.id; }
                }
            }
        }
        id=r.ridBase;
    }
    assert(visual);

    {QueryExtension r; r.length="MIT-SHM"_.size; r.size+=align(4,r.length)/4; send(string(raw(r)+"MIT-SHM"_+pad(4,r.length)));}
    {QueryExtensionReply r=readReply<QueryExtensionReply>(); Shm::EXT=r.major; Shm::event=r.firstEvent; Shm::error=r.firstError; }

    {QueryExtension r; r.length="RENDER"_.size; r.size+=align(4,r.length)/4; send(string(raw(r)+"RENDER"_+pad(4,r.length)));}
    {QueryExtensionReply r=readReply<QueryExtensionReply>(); Render::EXT=r.major; Render::event=r.firstEvent; Render::error=r.firstError; }
    {Render::QueryVersion r; send(raw(r)); readReply<Render::QueryVersionReply>();}
    {QueryPictFormats r; send(raw(r));}
    {QueryPictFormatsReply r=readReply<QueryPictFormatsReply>();
        array<PictFormInfo> formats = read<PictFormInfo>(fd, r.numFormats);
        for(uint i=0;i<r.numScreens;i++){ PictScreen screen = read<PictScreen>(fd);
            for(uint i=0;i<screen.numDepths;i++){ PictDepth depth = read<PictDepth>(fd);
                array<PictVisual> visuals = read<PictVisual>(fd,depth.numPictVisuals);
                if(depth.depth==32) for(PictVisual pictVisual: visuals) if(pictVisual.visual==visual) format=pictVisual.format;
            }
        }
        assert(format);
        read<uint>(fd,r.numSubpixels);
    }

    // Creates X window
    if(size.x<0||size.y<0) {
        int2 hint=widget->sizeHint();
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=display.x;
    if(size.y==0) size.y=display.y-16;
    this->size=size;
    {CreateColormap r; r.colormap=id+Colormap; r.window=root; r.visual=visual; send(raw(r));}
    {CreateWindow r; r.id=id+XWindow; r.parent=root; r.width=size.x, r.height=size.y; r.visual=visual; r.colormap=id+Colormap;
        r.overrideRedirect=overrideRedirect;
        r.eventMask=StructureNotifyMask|KeyPressMask|ButtonPressMask|LeaveWindowMask|PointerMotionMask|ExposureMask; send(raw(r));}
    {CreateGC r; r.context=id+GContext; r.window=id+XWindow; send(raw(r));}
    {ChangeProperty r; r.window=id+XWindow; r.property=Atom("WM_PROTOCOLS"_); r.type=Atom("ATOM"_); r.format=32;
    r.length=1; r.size+=r.length; send(string(raw(r)+raw(Atom("WM_DELETE_WINDOW"_))));}
    setTitle(title);
    setIcon(icon);
    setType(type);
}
Window::~Window() { close(fd); }

void Window::event() {
    current=this;
    if(revents==IDLE) {
        if(autoResize) {
            int2 hint = widget->sizeHint();
            if(hint != size) { setSize(hint); return; }
        }
        assert(mapped); assert(size);
        if(state!=Idle) { state=Wait; return; }
        if(buffer.width != (uint)size.x || buffer.height != (uint)size.y) {
            if(shm) {
                {Shm::Detach r; r.seg=id+Segment; send(raw(r));}
                shmdt(buffer.data);
                shmctl(shm, IPC_RMID, 0);
            }
            buffer.stride=buffer.width=size.x, buffer.height=size.y;
            shm = check( shmget(IPC_NEW, sizeof(byte4)*buffer.width*buffer.height , IPC_CREAT | 0777) );
            buffer.data = (byte4*)check( shmat(shm, 0, 0) ); assert(buffer.data);
            {Shm::Attach r; r.seg=id+Segment; r.shm=shm; send(raw(r));}
        }
        framebuffer = share(buffer);
        currentClip=Rect(framebuffer.size());
#if 0
        fill(currentClip,white);
#else
        // Oxygen like radial gradient background
        int2 center = int2(size.x/2,0); int radius=256;
        for(uint y=0;y<framebuffer.height;y++) for(uint x=0;x<framebuffer.width;x++) {
            int2 pos = int2(x,y);
            const int bgCenter=0xF0,bgOuter=0xE0,opacity=0xFF/*0xF0*/;
            int g = mix(bgOuter,bgCenter,min(1.f,length(pos-center)/radius))*opacity/255;
            framebuffer(x,y) = byte4(g,g,g,opacity);
        }
        //feather edges //TODO: client side shadow
        if(position.y>16) for(int x=0;x<size.x;x++) framebuffer(x,0) /= 2;
        if(position.x>0) for(int y=0;y<size.y;y++) framebuffer(0,y) /= 2;
        if(position.x+size.x<display.x-1) for(int y=0;y<size.y;y++) framebuffer(size.x-1,y) /= 2;
        if(position.y+size.y>16 && position.y+size.y<display.y-1) for(int x=0;x<size.x;x++) framebuffer(x,size.y-1) /= 2;
        //feather corners
        if(position.x>0 && position.y>0) framebuffer(0,0) /= 2;
        if(position.x+size.x<display.x-1 && position.y>0) framebuffer(size.x-1,0) /= 2;
        if(position.x>0 && position.y+size.y<display.y-1) framebuffer(0,size.y-1) /= 2;
        if(position.x+size.x<display.x-1 && position.y+size.y<display.y-1) framebuffer(size.x-1,size.y-1) /= 2;
#endif
        widget->render(int2(0,0),size);
        assert(!clipStack);
        {Shm::PutImage r; r.window=id+XWindow; r.context=id+GContext; r.seg=id+Segment;
            r.totalWidth=r.width=buffer.width; r.totalHeight=r.height=buffer.height; send(raw(r)); }
        state=Server;
    } else {
        uint8 type = read<uint8>(fd);
        processEvent(type, read<Event>(fd));
        while(queue) { QEvent e=queue.takeFirst(); processEvent(e.type, e.event); }
    }
    current=0;
}

void Window::processEvent(uint8 type, const Event& event) {
    if(type==0) { const Error& e=(const Error&)event; uint8 code=e.code;
        if(e.major==Render::EXT) {
            int reqSize=sizeof(Render::requests)/sizeof(*Render::requests);
            if(code>=Render::error && code<=Render::error+Render::errorCount) { code-=Render::error;
                assert(code<sizeof(Render::errors)/sizeof(*Render::errors));
                trace(); warn("Error",Render::errors[code],"seq:",e.seq,"id",e.id,
                      "request",e.minor<reqSize?string(Render::requests[e.minor]):dec(e.minor));
            } else {
                assert(code<sizeof(::errors)/sizeof(*::errors));
                trace(); warn("Error",::errors[code],"seq:",e.seq,"id",e.id,
                      "request",e.minor<reqSize?string(Render::requests[e.minor]):dec(e.minor));
            }
        } else {
            assert(code<sizeof(::errors)/sizeof(*::errors));
            int reqSize=sizeof(::requests)/sizeof(*::requests);
            trace(); warn("Error",::errors[code],"seq:",e.seq,"id",e.id,
                  "request",e.major<reqSize?string(::requests[e.major]):dec(e.major),"minor",e.minor);
        }
    }
    else if(type==1) error("Unexpected reply");
    else { Event e=event; type&=0b01111111; //msb set if sent by SendEvent
        /**/ if(type==MotionNotify) {
            if(drag && e.state&Button1Mask && drag->mouseEvent(int2(e.x,e.y), size, Widget::Motion, LeftButton)) wait();
            else if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Motion, (e.state&Button1Mask)?LeftButton:None)) wait();
            else if(anchor==Float) {
                if(!(e.state&Button1Mask)) { dragStart=int2(e.rootX,e.rootY); dragPosition=position; dragSize=size; } //to reuse border intersection checks
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
                    position=clip(int2(0,16),position,display), size=clip(int2(16,16),size,display-int2(0,16));
                    setGeometry(position,size);
                } else {
                    if((top && left)||(bottom && right)) setCursor(FDiagonal);
                    else if((top && right)||(bottom && left)) setCursor(BDiagonal);
                    else if(top || bottom) setCursor(Vertical);
                    else if(left || right) setCursor(Horizontal);
                    else setCursor(Arrow);
                }
            }
        }
        else if(type==ButtonPress) {
            dragStart=int2(e.rootX,e.rootY), dragPosition=position, dragSize=size;
            {SetInputFocus r; r.window=id; send(raw(r));}
            if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Press, (Button)e.key)) wait();
        }
        else if(type==ButtonRelease) drag=0;
        else if(type==KeyPress) {
            uint key = KeySym(e.key);
            signal<>* shortcut = localShortcuts.find(key);
            if(shortcut) (*shortcut)(); //local window shortcut
            else if(focus) if( focus->keyPress((Key)key) ) wait(); //normal keyPress event
        }
        else if(type==EnterNotify || type==LeaveNotify) {
            if(hideOnLeave) hide();
            signal<>* shortcut = localShortcuts.find(Widget::Leave);
            if(shortcut) (*shortcut)(); //local window shortcut
            if(widget->mouseEvent(int2(e.x,e.y), size, type==EnterNotify?Widget::Enter:Widget::Leave, (e.state&Button1Mask)?LeftButton:None))
                wait();
        }
        else if(type==Expose) { if(!e.expose.count) wait(); }
        else if(type==UnmapNotify) mapped=false;
        else if(type==MapNotify) mapped=true;
        else if(type==ReparentNotify) {}
        else if(type==ConfigureNotify) {
            position=int2(e.configure.x,e.configure.y); int2 size=int2(e.configure.w,e.configure.h);
            if(this->size!=size) { this->size=size; if(mapped) wait(); }
        }
        else if(type==GravityNotify) {}
        else if(type==ClientMessage) {
            signal<>* shortcut = localShortcuts.find(Escape);
            if(shortcut) (*shortcut)(); //local window shortcut
            else widget->keyPress(Escape);
        }
        else if(type==Shm::event+Shm::Completion) { if(state==Wait && mapped) wait(); state=Idle; }
        else if(type==MappingNotify) {}
        else log("Event", type<sizeof(::events)/sizeof(*::events)?::events[type]:str(type));
    }
}

void Window::send(const ref<byte>& request) { write(fd, request); sequence++; }

template<class T> T Window::readReply() {
    for(;;) { uint8 type = read<uint8>(fd);
        if(type==0){Error e=read<Error>(fd); processEvent(0,(Event&)e);  if(e.seq==sequence) { T t; clear(t); return t; }}
        else if(type==1) return read<T>(fd);
        else queue << QEvent __(type, read<::Event>(fd)); //queue events to avoid reentrance
    }
}

uint Window::Atom(const ref<byte>& name) {
    {InternAtom r; r.length=name.size; r.size+=align(4,r.length)/4; send(string(raw(r)+name+pad(4,r.length)));}
    {InternAtomReply r=readReply<InternAtomReply>(); return r.atom; }
}
string Window::AtomName(uint atom) {
    {GetAtomName r; r.atom=atom; send(raw(r));}
    {GetAtomNameReply r=readReply<GetAtomNameReply>(); string name=read(fd,r.size); read(fd,align(4,r.size)-r.size); return name;}
}
uint Window::KeySym(uint8 code) {
    {GetKeyboardMapping r; r.keycode=code; send(raw(r));}
    {GetKeyboardMappingReply r=readReply<GetKeyboardMappingReply>();
        array<uint> keysyms = read<uint>(fd,r.numKeySymsPerKeyCode);
        return keysyms?keysyms.first():0;
    }
}
template<class T> array<T> Window::getProperty(uint window, const ref<byte>& name, uint size) {
    {GetProperty r; r.window=window; r.property=Atom(name); r.length=size; send(raw(r));}
    {GetPropertyReply r=readReply<GetPropertyReply>(); int size=r.length*r.format/8;
        array<T> a; if(size) a=read<T>(fd,size/sizeof(T)); int pad=align(4,size)-size; if(pad) read(fd,pad); return a; }
}
template array<uint> Window::getProperty(uint window, const ref<byte>& name, uint size);
template array<byte> Window::getProperty(uint window, const ref<byte>& name, uint size);

void Window::setPosition(int2 position) {
    if(position.x<0) position.x=display.x+position.x;
    if(position.y<0) position.y=display.y+position.y;
    setGeometry(position,this->size);
}
void Window::setSize(int2 size) {
    if(size.x<0||size.y<0) {
        int2 hint=widget->sizeHint();
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0 || size.x>display.x) size.x=display.x;
    if(size.y==0 || size.x>display.x-16) size.y=display.y-16;
    setGeometry(this->position,size);
}
void Window::setGeometry(int2 position, int2 size) {
    if(anchor&Left && anchor&Right) position.x=(display.x-size.x)/2;
    else if(anchor&Left) position.x=0;
    else if(anchor&Right) position.x=display.x-size.x;
    if(anchor&Top && anchor&Bottom) position.y=16+(display.y-16-size.y)/2;
    else if(anchor&Top) position.y=16;
    else if(anchor&Bottom) position.y=display.y-size.y;
    if(position!=this->position && size!=this->size) {SetGeometry r; r.id=id+XWindow; r.x=position.x; r.y=position.y; r.w=size.x, r.h=size.y; send(raw(r));}
    else if(position!=this->position) {SetPosition r; r.id=id+XWindow; r.x=position.x, r.y=position.y; send(raw(r));}
    else if(size!=this->size) {SetSize r; r.id=id+XWindow; r.w=size.x, r.h=size.y; send(raw(r));}
}

void Window::setType(const ref<byte>& type) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_TYPE"_); r.type=Atom("ATOM"_); r.format=32;
    r.length=1; r.size+=r.length; send(string(raw(r)+raw(Atom(type))));
}
void Window::setTitle(const ref<byte>& title) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_NAME"_); r.type=Atom("UTF8_STRING"_); r.format=8;
    r.length=title.size; r.size+=align(4, r.length)/4; send(string(raw(r)+title+pad(4,title.size)));
}
void Window::setIcon(const Image& icon) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_ICON"_); r.type=Atom("CARDINAL"_); r.format=32;
    r.length=2+icon.width*icon.height; r.size+=r.length; send(string(raw(r)+raw(icon.width)+raw(icon.height)+(ref<byte>)icon));
}

void Window::show() { if(mapped) return; {MapWindow r; r.id=id; send(raw(r));}{RaiseWindow r; r.id=id; send(raw(r));}}
void Window::hide() { if(!mapped) return; {UnmapWindow r; r.id=id; send(raw(r));}}
void Window::render() { if(mapped) Poll::wait(); }

signal<>& Window::localShortcut(Key key) { return localShortcuts.insert((uint16)key); }
signal<>& Window::globalShortcut(Key key) { return globalShortcuts.insert((uint16)key); }

string Window::getSelection() {
    send(raw(GetSelectionOwner())); uint owner = readReply<GetSelectionOwnerReply>().owner; if(!owner) return string();
    {ConvertSelection r; r.requestor=id; r.target=Atom("UTF8_STRING"_); send(raw(r));}
    for(;;) { uint8 type = read<uint8>(fd);
        if((type&0b01111111)==SelectionNotify) { read<Event>(fd); return getProperty<byte>(id,"UTF8_STRING"_); }
        else queue << QEvent __(type, read<::Event>(fd)); //queue events to avoid reentrance
    }
}

ICON(arrow)
ICON(horizontal)
ICON(vertical)
ICON(fdiagonal)
ICON(bdiagonal)
ICON(move)

const Image& Window::cursorIcon(Window::Cursor cursor) {
    if(cursor==Arrow) return arrowIcon();
    if(cursor==Horizontal) return horizontalIcon();
    if(cursor==Vertical) return verticalIcon();
    if(cursor==FDiagonal) return fdiagonalIcon();
    if(cursor==BDiagonal) return bdiagonalIcon();
    if(cursor==Move) return moveIcon();
    error("");
}
int2 Window::cursorHotspot(Window::Cursor cursor) {
    if(cursor==Arrow) return int2(5,0);
    if(cursor==Horizontal) return int2(11,11);
    if(cursor==Vertical) return int2(11,11);
    if(cursor==FDiagonal) return int2(11,11);
    if(cursor==BDiagonal) return int2(11,11);
    if(cursor==Move) return int2(16,15);
    error("");
}

void Window::setCursor(Cursor cursor, uint window) {
    if(cursor==this->cursor) return; this->cursor=cursor;
    const Image& image = cursorIcon(cursor); int2 hotspot = cursorHotspot(cursor);
    Image premultiplied(image.width,image.height);
    for(uint y=0;y<image.height;y++) for(uint x=0;x<image.height;x++) {
        byte4 p=image(x,y); premultiplied(x,y)=byte4(p.b*p.a/255,p.g*p.a/255,p.r*p.a/255,p.a);
    }
    {::CreatePixmap r; r.pixmap=id+Pixmap; r.window=id; r.w=image.width, r.h=image.height; send(raw(r));}
    {::PutImage r; r.drawable=id+Pixmap; r.context=id+GContext; r.w=image.width, r.h=image.height; r.size+=r.w*r.h;
        send(string(raw(r)+ref<byte>(premultiplied)));}
    {Render::CreatePicture r; r.picture=id+Picture; r.drawable=id+Pixmap; r.format=format; send(raw(r));}
    {Render::CreateCursor r; r.cursor=id+XCursor; r.picture=id+Picture; r.x=hotspot.x; r.y=hotspot.y; send(raw(r));}
    {SetWindowCursor r; r.window=window?:id; r.cursor=id+XCursor; send(raw(r));}
    {FreeCursor r; r.cursor=id+XCursor; send(raw(r));}
    {FreePicture r; r.picture=id+Picture; send(raw(r));}
    {FreePixmap r; r.pixmap=id+Pixmap; send(raw(r));}
}
