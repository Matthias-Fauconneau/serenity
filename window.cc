#include "process.h"
#include "window.h"

#include <poll.h>

#include <X11/extensions/XShm.h>
#include <sys/shm.h>
#include <sys/ipc.h>
Image framebuffer;

#define Atom(name) XInternAtom(x, #name, 1)
template<class T> void Window::setProperty(const char* type,const char* name, const array<T>& value) {
    XChangeProperty(x, id, XInternAtom(x,name,1), XInternAtom(x,type,1), sizeof(T)*8, PropModeReplace, (uint8*)value.data(), value.size());
    XFlush(x);
}

int xErrorHandler(Display*,XErrorEvent*) { return 0; }
Window::Window(Widget* widget, int2 size, const string& name, const Image& icon) : widget(*widget) {
    XSetErrorHandler(&xErrorHandler);
    x = XOpenDisplay(0);
    registerPoll({XConnectionNumber(x), POLLIN, 0});

    if(size.x==0||size.y==0) {
        int2 hint=widget->sizeHint();
        if(size.x==0) size.x=abs(hint.x); if(size.y==0) size.y=abs(hint.y);
    }
    if(size.x==-1||size.y==-1) {
        XWindowAttributes root; XGetWindowAttributes(x, DefaultRootWindow(x), &root);
        if(size.x==-1) size.x=root.width; if(size.y==-1) size.y=root.height;
    }
    id = XCreateSimpleWindow(x,DefaultRootWindow(x),0,0,size.x,size.y,0,0,0xFFE0E0E0);
    XSelectInput(x, id, StructureNotifyMask|KeyPressMask|ButtonPressMask|LeaveWindowMask|PointerMotionMask|ExposureMask);
    setProperty<uint>("ATOM", "WM_PROTOCOLS", {Atom(WM_DELETE_WINDOW)});
    setProperty<uint>("ATOM", "_NET_WM_WINDOW_TYPE", {Atom(_NET_WM_WINDOW_TYPE_NORMAL)});
    if(name) setName(name);
    if(icon) setIcon(icon);
    if(!focus) this->focus=widget;
    widget->size=size;
    widget->update();
}

void Window::event(pollfd) { update(); }
void Window::update() {
    bool needRender=false;
    while(XEventsQueued(x, QueuedAfterFlush)) { XEvent e; XNextEvent(x,&e);
        if(e.type==MotionNotify) {
            needRender |= widget.mouseEvent(int2(e.xmotion.x,e.xmotion.y), Motion, e.xmotion.state&Button1Mask ? LeftButton : None);
        } else if(e.type==ButtonPress) {
            //XSetInputFocus(x, id, RevertToNone, CurrentTime);
            needRender |= widget.mouseEvent(int2(e.xbutton.x,e.xbutton.y), Press, (Button)e.xbutton.button);
        } else if(e.type==KeyPress) {
            auto key = XKeycodeToKeysym(x,e.xkey.keycode,0);
            keyPress.emit((Key)key);
            if(focus) needRender |= focus->keyPress((Key)key);
        } else if(e.type==EnterNotify || e.type==LeaveNotify) {
            needRender |= widget.mouseEvent(int2(e.xcrossing.x,e.xcrossing.y), e.type==EnterNotify?Enter:Leave, None);
        } else if(e.type==Expose && !e.xexpose.count) {
            needRender = true;
        } else if(e.type==ConfigureNotify || e.type==ReparentNotify) {
            XWindowAttributes window; XGetWindowAttributes(x,id,&window); int2 size{window.width, window.height};
            if(widget.size != size) {
                widget.size=size;
                widget.update();
                needRender=true;
            }
        } else if(e.type==MapNotify) {
            visible=true;
            widget.update();
            render();
        } else if(e.type==UnmapNotify) {
            visible=false;
        } else if(e.type==ClientMessage) {
            keyPress.emit(Escape);
            widget.keyPress(Escape);
            return;
        }
    }
    if(needRender) render();
}

template<class T> T mix(const T& a,const T& b, float t) { return a*t + b*(1-t); }

void Window::render() {
    if(!visible || !widget.size) return;
    if(!image || image->width != widget.size.x || image->height != widget.size.y) {
        if(image) {
            XShmDetach(x, &shminfo);
            image->f.destroy_image(image);
            shmdt(shminfo.shmaddr);
        }
        image = XShmCreateImage(x,DefaultVisual(x,0),DefaultDepth(x,0),ZPixmap,0,&shminfo,widget.size.x, widget.size.y);
        shminfo.shmid = shmget(IPC_PRIVATE, image->bytes_per_line*image->height, IPC_CREAT | 0777);
        shminfo.shmaddr = image->data = (char *)shmat(shminfo.shmid, 0, 0);
        shminfo.readOnly = True;
        XShmAttach(x, &shminfo);
    }
    framebuffer = Image((byte4*)image->data, image->width, image->height);
    {
         int2 center = int2(widget.size.x/2,0); int radius=256;
         for_Image(framebuffer) {
            int2 pos = int2(x,y);
            int g = mix(224,240,min(1.f,length(pos-center)/radius));
            framebuffer(x,y) = byte4(g,g,g,255);
         }
    }
    widget.render(int2(0,0));
    XShmPutImage(x,id,DefaultGC(x,0),image,0,0,0,0,image->width,image->height,0);
    XFlush(x);
}

void Window::show() { setVisible(true); }
void Window::hide() { setVisible(false); }
void Window::setVisible(bool visible) { if(visible) XMapWindow(x, id);  else XUnmapWindow(x, id); XFlush(x); }
void Window::setPosition(int2 position) { XMoveWindow(x, id, position.x, position.y); XFlush(x); }
void Window::setSize(int2 size) {
    if(!size.x||!size.y) {
        XWindowAttributes root; XGetWindowAttributes(x, DefaultRootWindow(x), &root);
        if(!size.x) size.x=root.width; if(!size.y) size.y=root.height;
    }
    XResizeWindow(x, id, size.x, size.y);
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
void Window::setName(const string& name) { setProperty("UTF8_STRING", "_NET_WM_NAME", name); }
void Window::setIcon(const Image& icon) {
    int size = 2+icon.width*icon.height;
    array<int> buffer(2*size); //CARDINAL is long
    buffer.buffer.size=2*size; buffer[0]=icon.width, buffer[1]=icon.height;
    for(uint i=0;i<icon.width*icon.height;i++) buffer[i]=*(uint*)icon.data; //pad to CARDINAL
    buffer.buffer.size /= 2; //XChangeProperty will read in CARDINAL (long) elements
    setProperty("CARDINAL", "_NET_WM_ICON", buffer);
    XFlush(x);
}
void Window::setType(const string& type) {
    setProperty<uint>("ATOM", "_NET_WM_WINDOW_TYPE", {XInternAtom(x,strz(type).data(),1)});
}
void Window::setOverrideRedirect(bool override_redirect) {
    XSetWindowAttributes attributes; attributes.override_redirect=override_redirect;
    XChangeWindowAttributes(x,id,CWOverrideRedirect,&attributes);
}
Widget* Window::focus=0;
void Window::setFocus(Widget* focus) {
    this->focus=focus;
    XSetInputFocus(x, id, RevertToNone, CurrentTime);
    XFlush(x);
}

uint Window::addHotKey(const string& key) {
    KeySym keysym = XStringToKeysym(strz(key).data());
    assert(keysym != NoSymbol);
    XGrabKey(x, XKeysymToKeycode(x, keysym), AnyModifier, DefaultRootWindow(x), True, GrabModeAsync, GrabModeAsync);
    XFlush(x);
    return keysym;
}
