#include "process.h"
#include "window.h"
#include "gl.h"

#define None None
#define Font XWindow
#define Window XWindow
#include <X11/Xlib.h>
//#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>
#undef Window
#undef Font

int xErrorHandler(Display*, XErrorEvent*) { return 0; }
//int xErrorHandler(Display* x, XErrorEvent* e) { char buffer[64]; XGetErrorText(x,e->error_code,buffer,64); log(strz(buffer)); return 0; }

#define Atom(name) XInternAtom(x, #name, 1)

template<class T> void Window::setProperty(const char* type,const char* name, const array<T>& value) {
    XChangeProperty(x, id, XInternAtom(x,name,1), XInternAtom(x,type,1), sizeof(T)*8, PropModeReplace, (uint8*)&value, value.size);
    XFlush(x);
}

GLXContext Window::ctx;

Window::Window(Widget& widget, int2 size, const string& name) : widget(widget) {
    x = XOpenDisplay(0);
    XSetErrorHandler(xErrorHandler);
    registerPoll();

    if(!size.x||!size.y) {
        XWindowAttributes root; XGetWindowAttributes(x, DefaultRootWindow(x), &root);
        if(!size.x) size.x=root.width; if(!size.y) size.y=root.height;
    }
    id = XCreateSimpleWindow(x,DefaultRootWindow(x),0,0,size.x,size.y,0,0,0xFFE0E0E0);
    XSelectInput(x, id, StructureNotifyMask|KeyPressMask|ButtonPressMask|LeaveWindowMask|PointerMotionMask|ExposureMask);
    setProperty<char>("STRING", "WM_CLASS", name+"\0"_+name);
    setProperty<char>("UTF8_STRING", "_NET_WM_NAME", name);
    setProperty<uint>("ATOM", "WM_PROTOCOLS", {Atom(WM_DELETE_WINDOW)});
    setProperty<uint>("ATOM", "_NET_WM_WINDOW_TYPE", {Atom(_NET_WM_WINDOW_TYPE_NORMAL)});

    if(!ctx) {
        XVisualInfo* vis = glXChooseVisual(x,DefaultScreen(x),(int[]){GLX_RGBA,GLX_DOUBLEBUFFER,1,0});
        ctx = glXCreateContext(x,vis,0,1);
        glXMakeCurrent(x, id, ctx);
        glClearColor(7./8,7./8,7./8,0);
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
        glEnable(GL_BLEND); //multiply (i.e darken) blend (allow blending with subpixel fonts)
    }
}

void Window::sync() { XSync(x,0); update(); }

void Window::update() {
    bool needRender=false;
    while(XEventsQueued(x, QueuedAfterFlush)) { XEvent e; XNextEvent(x,&e);
        if(e.type==MotionNotify) {
            needRender |= widget.mouseEvent(int2(e.xmotion.x,e.xmotion.y), Motion, e.xmotion.state&Button1Mask ? LeftButton : None);
        } else if(e.type==ButtonPress) {
            //tXSetInputFocus(x, id, RevertToNone, CurrentTime);
            needRender |= widget.mouseEvent(int2(e.xbutton.x,e.xbutton.y), Press, (Button)e.xbutton.button);
        } else if(e.type==KeyPress) {
            auto key = XKeycodeToKeysym(x,e.xkey.keycode,0);
            keyPress.emit((Key)key);
            if(focus) needRender |= focus->keyPress((Key)key);
        } else if(e.type==EnterNotify || e.type==LeaveNotify) {
            needRender |= widget.mouseEvent(int2(e.xcrossing.x,e.xcrossing.y), e.type==EnterNotify?Enter:Leave, None);
        } else if(e.type==Expose && !e.xexpose.count) {
            needRender = true;
        } else if(e.type == MapNotify || e.type==ConfigureNotify || e.type==ReparentNotify) {
            XWindowAttributes window; XGetWindowAttributes(x,id,&window); int2 size{window.width, window.height};
            if(widget.size != size) {
                widget.size=size;
                widget.update();
                render();
            }
        } else if(e.type==MapNotify) {
            visible=true;
        } else if(e.type==UnmapNotify) {
            visible=false;
        } else if(e.type==ClientMessage) {
            keyPress.emit(Escape);
            return;
        }
    }
    if(needRender) render();
}

void Window::render() {
    glXMakeCurrent(x, id, ctx);
    glViewport(widget.size);
    glClear(GL_COLOR_BUFFER_BIT);
    widget.render(int2(0,0));
    glXSwapBuffers(x,id);
}

void Window::show() { visible=true; XMapWindow(x, id); }
void Window::hide() { visible=false; XUnmapWindow(x, id); sync(); }

void Window::move(int2 position) { XMoveWindow(x, id, position.x, position.y); }

void Window::resize(int2 size) {
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

void Window::rename(const string& name) { setProperty("UTF8_STRING", "_NET_WM_NAME", name); }

void Window::setIcon(const Image& icon) {
    int size = 2+icon.width*icon.height;
    array<int> buffer(2*size); //CARDINAL is long
    buffer.size=2*size; buffer[0]=icon.width, buffer[1]=icon.height;
    copy((byte4*)(&buffer+2),icon.data,icon.width*icon.height);
    if(sizeof(long)==8) for(int i=size-1;i>=0;i--) { buffer[2*i]=buffer[i]; buffer[2*i+1]=0; } //0-extend int to long CARDINAL
    buffer.size /= 2; //XChangeProperty will read in CARDINAL (long) elements
    setProperty("CARDINAL", "_NET_WM_ICON", buffer);
    XFlush(x);
}

void Window::setType(const string& type) {
    setProperty<uint>("ATOM", "_NET_WM_WINDOW_TYPE", {XInternAtom(x,&strz(type),1)});
}

void Window::setOverrideRedirect(bool override_redirect) {
    XSetWindowAttributes attributes; attributes.override_redirect=override_redirect;
    XChangeWindowAttributes(x,id,CWOverrideRedirect,&attributes);
}

uint Window::addHotKey(const string& key) {
    KeySym keysym = XStringToKeysym(&strz(key));
    assert(keysym != NoSymbol);
    XGrabKey(x, XKeysymToKeycode(x, keysym), AnyModifier, DefaultRootWindow(x), True, GrabModeAsync, GrabModeAsync);
    XFlush(x);
    return keysym;
}

pollfd Window::poll() { return {XConnectionNumber(x), POLLIN}; }
void Window::event(pollfd) { update(); }

void Window::setFocus(Widget* focus) {
    this->focus=focus;
    XSetInputFocus(x, id, RevertToNone, CurrentTime);
}
Widget* Window::focus=0;
