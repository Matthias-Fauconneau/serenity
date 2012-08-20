#include "window.h"
#include "x.h"
#include "display.h"
#include "widget.h"
#include "file.h"
#include "stream.h"
#include "linux.h"
#include "debug.h"
#include "time.h"

int2 display;
Widget* focus;
namespace Shm { int major, event, error; }

Window::Window(Widget* widget, int2 size, const ref<byte>& title, const Image<byte4>& icon)
    : widget(widget), overrideRedirect(title.size?false:true), x(socket(PF_LOCAL, SOCK_STREAM, 0)) {
    // Setups X connection
    string path = "/tmp/.X11-unix/X"_+getenv("DISPLAY"_).slice(1);
    sockaddr_un addr; copy(addr.path,path.data(),path.size());
    check_(connect(x,(sockaddr*)&addr,2+path.size()),path);
    {ConnectionSetup r;
        string authority = getenv("HOME"_)+"/.Xauthority"_;
        if(exists(authority)) write(x, string(raw(r)+readFile(authority).slice(18,align(4,(r.nameSize=18))+(r.dataSize=16))));
        else write(x, raw(r)); }
    uint visual=0;
    {ConnectionSetupReply r=read<ConnectionSetupReply>(x); assert(r.status==1,ref<byte>((byte*)&r.release,r.reason-1));
        read(x,align(4,r.vendorLength));
        read<XFormat>(x,r.numFormats);
        for(int i=0;i<r.numScreens;i++){ Screen screen=read<Screen>(x);
            for(int i=0;i<screen.numDepths;i++) { Depth depth = read<Depth>(x);
                if(depth.numVisualTypes) for(VisualType visualType: read<VisualType>(x,depth.numVisualTypes)) {
                    if(!visual && depth.depth==32) { display=int2(screen.width,screen.height); root = screen.root; visual=visualType.id; }
                }
            }
        }
        id=r.ridBase;
    }
    assert(visual);

    // Creates X window
    if(size.x<0||size.y<0) {
        int2 hint=widget->sizeHint();
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=display.x;
    if(size.y==0) size.y=display.y-16;
    this->size=size;
    {CreateColormap r; r.colormap=id+Colormap; r.window=root; r.visual=visual; write(x, raw(r));}
    {CreateWindow r; r.id=id+XWindow; r.parent=root; r.width=size.x, r.height=size.y; r.visual=visual; r.colormap=id+Colormap;
        r.overrideRedirect=overrideRedirect;
        r.eventMask=StructureNotifyMask|KeyPressMask|ButtonPressMask|LeaveWindowMask|PointerMotionMask|ExposureMask; write(x, raw(r));}
    {ChangeProperty r; r.window=id+XWindow; r.property=Atom("WM_PROTOCOLS"_); r.type=Atom("ATOM"_); r.format=32;
    r.length=1; r.size+=r.length; write(x,string(raw(r)+raw(Atom("WM_DELETE_WINDOW"_))));}
    {CreateGC r; r.context=id+GContext; r.window=id+XWindow; write(x, raw(r));}
    setTitle(title);
    setIcon(icon);
    registerPoll(__(x,POLLIN));
}

void Window::event(const pollfd& poll) {
    if(poll.fd==0) {
        assert(mapped); assert(size);
        if(state==Server) { state=Wait; return; }
        if(buffer.width != (uint)size.x || buffer.height != (uint)size.y) {
            if(shm) {
                {Shm::Detach r; r.seg=id+Segment; write(x, raw(r));}
                shmdt(buffer.data);
                shmctl(shm, IPC_RMID, 0);
            } else {
                {QueryExtension r; r.length="MIT-SHM"_.size; r.size+=align(4,r.length)/4; write(x,string(raw(r)+"MIT-SHM"_+pad(4,r.length)));}
                {QueryExtensionReply r=readReply<QueryExtensionReply>(); Shm::major=r.major; Shm::event=r.firstEvent; Shm::error=r.firstError; }
            }
            buffer.stride=buffer.width=size.x, buffer.height=size.y;
            shm = check( shmget(IPC_NEW, sizeof(pixel)*buffer.width*buffer.height , IPC_CREAT | 0777) );
            buffer.data = (pixel*)check( shmat(shm, 0, 0) ); assert(buffer.data);
            {Shm::Attach r; r.seg=id+Segment; r.shm=shm; write(x, raw(r));}
        }
        framebuffer = share(buffer);
        currentClip=Rect(framebuffer.size());
        // Oxygen like radial gradient background
        int2 center = int2(size.x/2,0); int radius=256;
        for(uint y=0;y<framebuffer.height;y++) for(uint x=0;x<framebuffer.width;x++) {
            int2 pos = int2(x,y);
            const int bgCenter=0xF0,bgOuter=0xE0,opacity=0xF0;
            int g = mix(bgOuter,bgCenter,min(1.f,length(pos-center)/radius))*opacity/255;
            framebuffer(x,y) = byte4(g,g,g,opacity);
        }
        if(overrideRedirect) {
            //feather edges //TODO: client side shadow
            if(position.y>16) for(int x=0;x<size.x;x++) framebuffer(x,0) /= 2;
            if(position.x>0) for(int y=0;y<size.y;y++) framebuffer(0,y) /= 2;
            if(position.x+size.x<display.x-1) for(int y=0;y<size.y;y++) framebuffer(size.x-1,y) /= 2;
            if(position.y+size.y>16 && position.y+size.y<display.y-1) for(int x=0;x<size.x;x++) framebuffer(x,size.y-1) /= 2;
            //feather corners
            if(overrideRedirect && position.x>0 && position.y>0) framebuffer(0,0) /= 2;
            if(overrideRedirect && position.x+size.x<display.x-1 && position.y>0) framebuffer(size.x-1,0) /= 2;
            if(position.x>0 && position.y+size.y<display.y-1) framebuffer(0,size.y-1) /= 2;
            if(position.x+size.x<display.x-1 && position.y+size.y<display.y-1) framebuffer(size.x-1,size.y-1) /= 2;
        }
        widget->render(int2(0,0),size);
        assert(!clipStack);
        {Shm::PutImage r; r.window=id+XWindow; r.context=id+GContext; r.seg=id+Segment;
            r.totalWidth=r.width=buffer.width; r.totalHeight=r.height=buffer.height; write(x, raw(r)); }
        state=Server;
    }
    if(poll.fd==x) do { uint8 type = read<uint8>(x); readEvent(type); } while(::poll((pollfd*)&poll,1,0));
}

void Window::readEvent(uint8 type) {
    /***/ if(type==0) { Error e=read<Error>(x);
        error("Error",e.code<sizeof(xerror)/sizeof(*xerror)?xerror[e.code]:dec(e.code),"seq:",e.seq,"id",e.id,"request",
              e.major<sizeof(xrequest)/sizeof(*xrequest)?xrequest[e.major]:dec(e.major),"minor",e.minor);
    } else if(type==1) { error("Unexpected reply");
    } else { Event e=read<Event>(x); type&=0b01111111; //msb set if sent by SendEvent
        if(type==MotionNotify) {
            if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Motion, (e.state&Button1Mask)?LeftButton:None)) wait();
        } else if(type==ButtonPress) {
            if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Press, (Button)e.key)) wait();
        } else if(type==KeyPress) {
            uint key = KeySym(e.key);
            signal<>* shortcut = localShortcuts.find(key);
            if(shortcut) (*shortcut)(); //local window shortcut
            else if(focus) if( focus->keyPress((Key)key) ) wait(); //normal keyPress event
        } else if(type==EnterNotify || type==LeaveNotify) {
            if(hideOnLeave) hide();
            signal<>* shortcut = localShortcuts.find(Widget::Leave);
            if(shortcut) (*shortcut)(); //local window shortcut
            if(widget->mouseEvent(int2(e.x,e.y), size, type==EnterNotify?Widget::Enter:Widget::Leave, (e.state&Button1Mask)?LeftButton:None))
                wait();
        } else if(type==Expose) { if(!e.expose.count && e.expose.w>1 && e.expose.h>1) wait();
        } else if(type==UnmapNotify) { mapped=false;
        } else if(type==MapNotify) { mapped=true;
        } else if(type==ReparentNotify) {
        } else if(type==ConfigureNotify) {
            position=int2(e.configure.x,e.configure.y); int2 size=int2(e.configure.w,e.configure.h);
            if(this->size!=size) { this->size=size; if(mapped) wait(); }
        } else if(type==ClientMessage) {
            signal<>* shortcut = localShortcuts.find(Escape);
            if(shortcut) (*shortcut)(); //local window shortcut
            else widget->keyPress(Escape);
        } else if(type==Shm::event+Shm::Completion) { if(state==Wait && mapped) wait(); else state=Idle;
        } else log("Event", type<sizeof(xevent)/sizeof(*xevent)?xevent[type]:str(type));
    }
}

template<class T> T Window::readReply() {
    static bool waiting; if(waiting) error("synchronous request while waiting for a reply requires an unimplemented queue"); waiting=true;
    for(;;) { uint8 type = read<uint8>(x); if(type==1) { waiting=false; return read<T>(x); } else readEvent(type); }
}
uint Window::Atom(const ref<byte>& name) {
    {InternAtom r; r.length=name.size; r.size+=align(4,r.length)/4; write(x,string(raw(r)+name+pad(4,r.length)));}
    {InternAtomReply r=readReply<InternAtomReply>(); return r.atom; }
}
string Window::AtomName(uint atom) {
    {GetAtomName r; r.atom=atom; write(x, raw(r));}
    {GetAtomNameReply r=readReply<GetAtomNameReply>(); string name=read(x,r.size); read(x,align(4,r.size)-r.size); return name;}
}
uint Window::KeySym(uint8 code) {
    {GetKeyboardMapping r; r.keycode=code; write(x, raw(r));}
    {GetKeyboardMappingReply r=readReply<GetKeyboardMappingReply>();
        array<uint> keysyms = read<uint>(x,r.numKeySymsPerKeyCode);
        return keysyms.first();
    }
}
template<class T> array<T> Window::getProperty(uint window, const ref<byte>& name, uint size) {
    {GetProperty r; r.window=window; r.property=Atom(name); r.length=size; write(x, raw(r));}
    {GetPropertyReply r=readReply<GetPropertyReply>(); int size=r.length*r.format/8;
        array<T> a; if(size) a=read<T>(x,size/sizeof(T)); int pad=align(4,size)-size; if(pad) read(x,pad); return a; }
}
template array<uint> Window::getProperty(uint window, const ref<byte>& name, uint size);
template array<byte> Window::getProperty(uint window, const ref<byte>& name, uint size);

void Window::setWidget(Widget* widget) { this->widget=widget; if(mapped) wait(); }

void Window::setPosition(int2 position) {
    if(position.x<0) position.x=display.x+position.x;
    if(position.y<0) position.y=display.y+position.y;
    {ConfigureWindow r; r.id=id+XWindow; r.mask=X|Y; r.x=position.x, r.y=position.y; write(x, raw(r));}
}
void Window::setSize(int2 size) {
    if(size.x<0||size.y<0) {
        int2 hint=widget->sizeHint();
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=display.x;
    if(size.y==0) size.y=display.y-16;
    if(size!=this->size){ConfigureWindow r; r.id=id+XWindow; r.mask=W|H; r.x=size.x, r.y=size.y; write(x, raw(r));}
}
void Window::setTitle(const ref<byte>& title) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_NAME"_); r.type=Atom("UTF8_STRING"_); r.format=8;
    r.length=title.size; r.size+=align(4, r.length)/4; write(x,string(raw(r)+title+pad(4,title.size)));
}
void Window::setIcon(const Image<byte4>& icon) {
    ChangeProperty r; r.window=id+XWindow; r.property=Atom("_NET_WM_ICON"_); r.type=Atom("CARDINAL"_); r.format=32;
    r.length=2+icon.width*icon.height; r.size+=r.length; write(x,string(raw(r)+raw(icon.width)+raw(icon.height)+(ref<byte>)icon));
}

void Window::show() { if(mapped) return; {MapWindow r; r.id=id; write(x, raw(r));}}
void Window::hide() { if(!mapped) return;{UnmapWindow r; r.id=id; write(x, raw(r));}}
void Window::render() { if(mapped) Poll::wait(); }

signal<>& Window::localShortcut(Key key) { return localShortcuts.insert((uint16)key); }
signal<>& Window::globalShortcut(Key key) { return globalShortcuts.insert((uint16)key); }

string Window::getSelection() {
    {GetSelectionOwner r; r.selection=1; write(x, raw(r));} uint owner = readReply<GetSelectionOwnerReply>().owner; if(!owner) return string();
    {ConvertSelection r; r.requestor=id; r.target=Atom("UTF8_STRING"_);}
     for(;;) { uint8 type = read<uint8>(x);
         if((type&0b01111111)==SelectionNotify) { read<Event>(x); return getProperty<byte>(owner,"UTF8_STRING"_); }
         else readEvent(type);
     }
}
void Window::setCursor(Image<byte4> image, uint window) {
    {CreatePixmap r; r.pixmap=id+Pixmap; r.window=window; r.w=image.width, r.h=image.height; write(x, raw(r));}
    {PutImage r; r.drawable=id+Pixmap; r.context=id+GContext; r.w=image.width, r.h=image.height;
        write(x, string(raw(r)+ref<byte>(image)));}
    {CreateCursor r; r.cursor=id+Cursor; r.image=id+Pixmap; write(x, raw(r));}
    {SetWindowCursor r; r.window=root; r.cursor=id+Cursor; write(x, raw(r));}
}
