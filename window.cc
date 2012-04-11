#include "process.h"
#include "window.h"
#include "array.cc"
#include "raster.h"

#include <poll.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include <sys/ipc.h>

Display* Window::x=0;
int2 Window::screen;
int Window::depth;
Visual* Window::visual;
map<XID, Window*> Window::windows;
map<KeySym, signal<> > Window::globalShortcuts;
Widget* Window::focus=0;

template<class T> array<T> Window::getProperty(XID window, const char* property) {
    assert(x);
    Atom atom = XInternAtom(x,property,1);
    Atom type; int format; ulong size, bytesAfter; uint8* data =0;
    XGetWindowProperty(x,window,atom,0,~0,0,0,&type,&format,&size,&bytesAfter,&data);
    if(!data || !size) return array<T>();
    array<T> list = copy(array<T>((T*)data,size));
    assert(list.data()!=(T*)data);
    XFree(data);
    return list;
}
template array<Atom> Window::getProperty(XID window, const char* property);

template<class T> void Window::setProperty(const char* type,const char* name, const array<T>& value) {
    assert(id);
    XChangeProperty(x, id, XInternAtom(x,name,1), XInternAtom(x,type,1), sizeof(T)*8, PropModeReplace, (uint8*)value.data(), value.size());
    XFlush(x);
}

static int xErrorHandler(Display* x, XErrorEvent* error) {
    char buffer[64]; XGetErrorText(x,error->error_code,buffer,sizeof(buffer)); log(buffer);
    return 0;
}

Window::Window(Widget* widget, const string& title, const Image& icon, int2 size) : size(size), title(copy(title)), icon(copy(icon)), widget(widget) {
    if(!x) {
        XSetErrorHandler(xErrorHandler);
        x = XOpenDisplay(0);
        if(!x) error("Cannot open X display");
        pollfd p={XConnectionNumber(x), POLLIN, 0}; registerPoll(p);
        XWindowAttributes root; XGetWindowAttributes(x, DefaultRootWindow(x), &root); screen=int2(root.width,root.height);
        XVisualInfo info; XMatchVisualInfo(x, DefaultScreen(x), 32, TrueColor, &info); depth = info.depth; visual=info.visual;
    }
}

string str(const Window& window) { return str(window.id); }

void Window::event(pollfd) { processEvents(); }
void Window::processEvents() {
    array<XID> needRender;
    while(XEventsQueued(x, QueuedAfterFlush)) {
        XEvent e; XNextEvent(x,&e);
        XID id = e.xany.window;
        if(e.type==KeyPress||e.type==KeyRelease) {
            KeySym key = XKeycodeToKeysym(x,e.xkey.keycode,0);
            signal<>* shortcut = globalShortcuts.find(key);
            if(shortcut) {  if(e.type==KeyPress) shortcut->emit(); continue; } //global window shortcut
        }
        Window** window = windows.find(id);
        if(!window) { log("Unknown window",id,"for event",e.type,"(windows = ",windows,")"); continue; }
        if((*window)->event(e) && !contains(needRender,id)) needRender << id;
    }
    for(XID id: needRender) windows[id]->render();
}
bool Window::event(const XEvent& e) {
    assert(id);
    if(e.type==MotionNotify) {
        return widget->mouseEvent(int2(e.xmotion.x,e.xmotion.y), Motion, (e.xmotion.state&Button1Mask)?LeftButton:None);
    } else if(e.type==ButtonPress) {
        return widget->mouseEvent(int2(e.xbutton.x,e.xbutton.y), Press, (Button)e.xbutton.button);
    } else if(e.type==KeyPress) {
        KeySym key = XKeycodeToKeysym(x,e.xkey.keycode,0);
        signal<>* shortcut = localShortcuts.find(key);
        if(shortcut) shortcut->emit(); //local window shortcut
        else if(focus) return focus->keyPress((Key)key); //normal keyPress event
    } else if(e.type==EnterNotify || e.type==LeaveNotify) {
        signal<>* shortcut = localShortcuts.find(Leave);
        if(shortcut) shortcut->emit(); //local window shortcut
        return widget->mouseEvent(int2(e.xcrossing.x,e.xcrossing.y), e.type==EnterNotify?Enter:Leave, (e.xcrossing.state&Button1Mask)?LeftButton:None);
    } else if(e.type==Expose && !e.xexpose.count) {
        return true;
    } else if(e.type==ConfigureNotify || e.type==ReparentNotify) {
        XWindowAttributes window; XGetWindowAttributes(x,id,&window); int2 size(window.width, window.height);
        this->position=int2(window.x,window.y);
        this->size=size;
        if(visible) return true;
    } else if(e.type==MapNotify) {
        visible=true;
        assert(size);
        return true;
    } else if(e.type==UnmapNotify) {
        visible=false;
    } else if(e.type==ClientMessage) {
        signal<>* shortcut = localShortcuts.find(Escape);
        if(shortcut) shortcut->emit(); //local window shortcut
        else widget->keyPress(Escape);
    }
    return false;
}

template<class T> T mix(const T& a,const T& b, float t) { return a*t + b*(1-t); }

void Window::update() {
    widget->size=size;
    widget->update();
    render();
}

void Window::render() {
    assert(id);
    if(!visible || !size) return;
    if(!image || image->width != size.x || image->height != size.y) {
        if(image) {
            XShmDetach(x, &shminfo);
            image->f.destroy_image(image);
            shmdt(shminfo.shmaddr);
        }
        image = XShmCreateImage(x,visual,depth,ZPixmap,0,&shminfo, size.x,size.y);
        shminfo.shmid = shmget(IPC_PRIVATE, image->bytes_per_line*image->height, IPC_CREAT | 0777);
        shminfo.shmaddr = image->data = (char *)shmat(shminfo.shmid, 0, 0);
        shminfo.readOnly = True;
        XShmAttach(x, &shminfo);
    }
    framebuffer = Image((byte4*)image->data, image->width, image->height, false);
    clear(framebuffer.data,framebuffer.width*framebuffer.height,byte4(240,240,240,240));
    if(widget->size!=size) {
        widget->size=size;
        widget->update();
    }
    push(Rect(int2(image->width,image->height)));
    widget->render(int2(0,0));
    finish();
    XShmPutImage(x,id,gc,image,0,0,0,0,image->width,image->height,0);
    XFlush(x);
}

void Window::create() {
    assert(!id);
    setSize(size); //translate special values
    XSetWindowAttributes attributes;
    attributes.colormap = XCreateColormap(x, DefaultRootWindow(x), visual, AllocNone);
    attributes.background_pixel = 0xF0F0F0F0;
    attributes.border_pixel = BlackPixel(x,DefaultScreen(x));
    attributes.event_mask = StructureNotifyMask|KeyPressMask|ButtonPressMask|LeaveWindowMask|PointerMotionMask|ExposureMask;
    attributes.override_redirect = overrideRedirect;
    id = XCreateWindow(x,DefaultRootWindow(x),position.x,position.y,size.x,size.y,0,depth,InputOutput,visual,
                       CWBackPixel|CWColormap|CWBorderPixel|CWEventMask|CWOverrideRedirect, &attributes);
    windows[id] = this;
    gc = XCreateGC(x, id, 0, 0);
    setProperty<uint>("ATOM", "WM_PROTOCOLS", {Atom(WM_DELETE_WINDOW)});
    setType(type?:Atom(_NET_WM_WINDOW_TYPE_NORMAL));
    if(title) setTitle(title);
    if(icon) setIcon(icon);
}

void Window::show() {
    if(!id) create();
    XMapWindow(x, id);
    XRaiseWindow(x, id);
    XSync(x,0);
    processEvents();
}
void Window::hide() { if(id) { XUnmapWindow(x, id); XFlush(x); } }

void Window::setPosition(int2 position) {
    if(position.x<0) position.x=screen.x+position.x;
    if(position.y<0) position.y=screen.y+position.y;
    if(id) XMoveWindow(x, id, position.x, position.y);
    this->position=position;
}

void Window::setSize(int2 size) {
    if(size.x<0||size.y<0) {
        int2 hint=widget->sizeHint(); assert(hint,hint);
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=screen.x;
    if(size.y==0) size.y=screen.y-16;
    assert(size);
    if(id) XResizeWindow(x, id, size.x, size.y);
    else this->size=size;
}

void Window::setFullscreen(bool) {
    XEvent xev; clear(xev);
    xev.type = ClientMessage;
    xev.xclient.window = id;
    xev.xclient.message_type = Atom(_NET_WM_STATE);
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = 1;
    xev.xclient.data.l[1] = Atom(_NET_WM_STATE_FULLSCREEN);
    xev.xclient.data.l[2] = 0;
    XSendEvent(x, DefaultRootWindow(x), 0, SubstructureNotifyMask, &xev);
}

void Window::setTitle(const string& title) {
    this->title=copy(title);
    if(!title) { logTrace(); warn("Empty window title"); }
    if(id) setProperty("UTF8_STRING", "_NET_WM_NAME", title);
}

void Window::setIcon(const Image& icon) {
    this->icon=copy(icon);
    if(!id) return;
    int size = 2+icon.width*icon.height;
    if(sizeof(long)==4) {
        array<int> buffer(size); buffer.setSize(size); //CARDINAL is long
        buffer[0]=icon.width, buffer[1]=icon.height;
        for(uint i=0;i<icon.width*icon.height;i++) buffer[2+i]=*(uint*)&icon.data[i]; //pad to CARDINAL
        setProperty("CARDINAL", "_NET_WM_ICON", buffer);
    } else {
        array<int> buffer(2*size); buffer.setSize(2*size); //CARDINAL is long
        buffer[0]=icon.width, buffer[1]=0; buffer[2]=icon.height, buffer[3]=0;
        for(uint i=0;i<icon.width*icon.height;i++) buffer[4+2*i]=*(uint*)&icon.data[i]; //pad to CARDINAL
        buffer.buffer.size /= 2; //XChangeProperty will read in CARDINAL (long) elements
        setProperty("CARDINAL", "_NET_WM_ICON", buffer);
    }
}

void Window::setType(Atom type) {
    this->type=type;
    if(id) setProperty<uint>("ATOM", "_NET_WM_WINDOW_TYPE", {type});
}

void Window::setOverrideRedirect(bool overrideRedirect) {
    XSetWindowAttributes attributes; attributes.override_redirect=overrideRedirect;
    if(id) XChangeWindowAttributes(x,id,CWOverrideRedirect,&attributes);
    this->overrideRedirect=overrideRedirect;
}

void Window::setFocus(Widget* focus) {
    this->focus=focus;
    XSetInputFocus(x, id, RevertToPointerRoot, CurrentTime);
    XFlush(x);
}

signal<>& Window::localShortcut(const string& key) {
    if(key=="Leave"_) return localShortcuts[Leave];
    KeySym keysym = XStringToKeysym(strz(key).data());
    assert(keysym != NoSymbol);
    assert(!localShortcuts.contains(keysym));
    return localShortcuts[keysym];
}

signal<>& Window::globalShortcut(const string& key) {
    KeySym keysym = XStringToKeysym(strz(key).data());
    assert(keysym != NoSymbol);
    XGrabKey(x, XKeysymToKeycode(x, keysym), AnyModifier, DefaultRootWindow(x), True, GrabModeAsync, GrabModeAsync);
    XFlush(x);
    return globalShortcuts.insert(keysym);
}

string Window::getSelection() {
    XID owner = XGetSelectionOwner(x, XA_PRIMARY);
    if(!owner) return ""_;
    XConvertSelection(x,XA_PRIMARY,Atom(UTF8_STRING), 0, owner, CurrentTime);
    return getProperty<char>(owner,"UTF8_STRING");
}
