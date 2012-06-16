#include "window.h"
#include "raster.h"
#include "widget.h"

#include "array.cc"
Array_Copy_Compare(Window*)

Display* Window::x=0;
int2 Window::screen;
int Window::depth;
Visual* Window::visual;
int2 Window::cursor;
map<uint, Window*> Window::windows;
map<uint, signal<> > Window::globalShortcuts;
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
template array<byte> Window::getProperty(XID window, const char* property);
template array<Atom> Window::getProperty(XID window, const char* property);

template<class T> void Window::setProperty(XID window, const char* type,const char* name, const array<T>& value) {
    XChangeProperty(x, window, XInternAtom(x,name,1), XInternAtom(x,type,1), sizeof(T)*8, 0, (uint8*)value.data(), value.size());
    XFlush(x);
}
template void Window::setProperty(XID window, const char* type,const char* name, const array<byte>& value);
template void Window::setProperty(XID window, const char* type,const char* name, const array<uint>& value);
#if __WORDSIZE == 64
template void Window::setProperty(XID window, const char* type,const char* name, const array<Atom>& value);
#endif

static int xErrorHandler(Display* x, XErrorEvent* error) {
    char buffer[64]; XGetErrorText(x,error->error_code,buffer,sizeof(buffer)); log(buffer);
    return 0;
}

Window::Window(Widget* widget, const string& title, const Image& icon, int2 size)
    : size(size), title(copy(title)), icon(copy(icon)), widget(widget) {
    if(!x) {
        XSetErrorHandler(xErrorHandler);
        x = XOpenDisplay(0);
        if(!x) error("Cannot open X display");
        registerPoll({XConnectionNumber(x), POLLIN});
        XWindowAttributes root; XGetWindowAttributes(x, x->screens[0].root, &root); screen=int2(root.width,root.height);
        XVisualInfo info; XMatchVisualInfo(x, 0, 32, TrueColor, &info); depth = info.depth; visual=info.visual;
    }
}
Window::~Window() { windows.remove(id); }

void Window::event(pollfd) { processEvents(); }
void Window::processEvents() {
    array<Window*> needRender;
    while(XPending(x)) {
        XEvent e; XNextEvent(x,&e);
        uint id = e.window;
        if(e.type==KeyPress||e.type==KeyRelease) {
            signal<>* shortcut = globalShortcuts.find(e.code);
            if(shortcut) {  if(e.type==KeyPress) shortcut->emit(); continue; } //global window shortcut
        }
        if(!windows.contains(id)) { log("Unknown window for event",e.type); continue; }
        Window* window = windows.at(id);
        if(window->event(e) && !contains(needRender, window)) needRender << window;
    }
    for(Window* window: needRender) window->render();
}

bool Window::event(const XEvent& e) {
    assert(id);
    if(e.type==MotionNotify) {
        return widget->mouseEvent(int2(e.x,e.y), Motion, (e.state&Button1Mask)?LeftButton:None);
    } else if(e.type==ButtonPress) {
        cursor=int2(e.x_root,e.y_root);
        return widget->mouseEvent(int2(e.x,e.y), Press, (Button)e.code);
    } else if(e.type==KeyPress) {
        signal<>* shortcut = localShortcuts.find(e.code);
        if(shortcut) shortcut->emit(); //local window shortcut
        else if(focus) return focus->keyPress((Key)XkbKeycodeToKeysym(x,e.code,0,0)); //normal keyPress event
    } else if(e.type==EnterNotify || e.type==LeaveNotify) {
        signal<>* shortcut = localShortcuts.find(Leave);
        if(shortcut) shortcut->emit(); //local window shortcut
        return widget->mouseEvent(int2(e.x,e.y), e.type==EnterNotify?Enter:Leave,
                                  (e.state&Button1Mask)?LeftButton:None);
    } else if(e.type==Expose && !e.state) {
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
            image->destroy_image(image);
            shmdt(shminfo.shmaddr);
        }
        image = XShmCreateImage(x,visual,depth,2,0,&shminfo, size.x,size.y);
        shminfo.shmid = shmget(0, image->stride*image->height, 01777);
        shminfo.shmaddr = image->data = (char *)shmat(shminfo.shmid, 0, 0);
        shminfo.readOnly = true;
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
    attributes.colormap = XCreateColormap(x, x->screens[0].root, visual, 0);
    attributes.bg_pixel = 0xF0F0F0F0;
    attributes.border_pixel = 0;
    attributes.event_mask = StructureNotifyMask|KeyPressMask|ButtonPressMask|LeaveWindowMask|PointerMotionMask|ExposureMask;
    attributes.override_redirect = overrideRedirect;
    id = XCreateWindow(x,x->screens[0].root,position.x,position.y,size.x,size.y,0,depth,1,visual,
                       CWBackPixel|CWColormap|CWBorderPixel|CWEventMask|CWOverrideRedirect, &attributes);
    windows.insert(id, this);
    gc = XCreateGC(x, id, 0, 0);
    setProperty<uint>(id, "ATOM", "WM_PROTOCOLS", {Atom(WM_DELETE_WINDOW)});
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
    XClientMessageEvent xev = {ClientMessage,0,0,0,id,Atom(_NET_WM_STATE),32,{1,Atom(_NET_WM_STATE_FULLSCREEN),0,0,0}};
    XSendEvent(x, x->screens[0].root, 0, SubstructureNotifyMask, (XEvent*)&xev);
}

void Window::setTitle(const string& title) {
    this->title=copy(title);
    if(!title) { logTrace(); warn("Empty window title"); }
    if(id) setProperty(id, "UTF8_STRING", "_NET_WM_NAME", title);
}

void Window::setIcon(const Image& icon) {
    this->icon=copy(icon);
    if(!id) return;
    int size = 2+icon.width*icon.height;
    if(sizeof(long)==4) {
        array<uint> buffer(size); buffer.setSize(size); //CARDINAL is long
        buffer[0]=icon.width, buffer[1]=icon.height;
        for(uint i=0;i<icon.width*icon.height;i++) buffer[2+i]=*(uint*)&icon.data[i]; //pad to CARDINAL
        setProperty(id, "CARDINAL", "_NET_WM_ICON", buffer);
    } else {
        array<uint> buffer(2*size); buffer.setSize(2*size); //CARDINAL is long
        buffer[0]=icon.width, buffer[1]=0; buffer[2]=icon.height, buffer[3]=0;
        for(uint i=0;i<icon.width*icon.height;i++) buffer[4+2*i]=*(uint*)&icon.data[i]; //pad to CARDINAL
        buffer.buffer.size /= 2; //XChangeProperty will read in CARDINAL (long) elements
        setProperty(id, "CARDINAL", "_NET_WM_ICON", buffer);
    }
}

void Window::setType(Atom type) {
    this->type=type;
    if(id) setProperty<uint>(id, "ATOM", "_NET_WM_WINDOW_TYPE", {type});
}

void Window::setOverrideRedirect(bool overrideRedirect) {
    XSetWindowAttributes attributes; attributes.override_redirect=overrideRedirect;
    if(id) XChangeWindowAttributes(x,id,CWOverrideRedirect,&attributes);
    this->overrideRedirect=overrideRedirect;
}

void Window::setFocus(Widget* focus) {
    this->focus=focus;
    XSetInputFocus(x, id, 1, 0);
    XFlush(x);
}

bool Window::hasFocus() {
    XID window; int revert;
    XGetInputFocus(x, &window, &revert);
    return window==id;
}

signal<>& Window::localShortcut(const string& key) {
    if(key=="Leave"_) return localShortcuts[Leave];
    uint code = XKeysymToKeycode(x, XStringToKeysym(strz(key)));
    assert(code);
    return localShortcuts.insert(code);
}

signal<>& Window::globalShortcut(const string& key) {
    uint code = XKeysymToKeycode(x, XStringToKeysym(strz(key)));
    assert(code);
    XGrabKey(x, code, AnyModifier, x->screens[0].root, 1, 1, 1);
    XFlush(x);
    return globalShortcuts.insert(code);
}

string Window::getSelection() {
    XID owner = XGetSelectionOwner(x, 1);
    if(!owner) return ""_;
    XConvertSelection(x,1,Atom(UTF8_STRING), 0, owner, 0);
    return getProperty<byte>(owner,"UTF8_STRING");
}
