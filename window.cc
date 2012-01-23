#include "process.h"
#include "interface.h"
#include "gl.h"

#define Font XWindow
#define Window XWindow
#include <X11/Xlib.h>
#define GLX_GLXEXT_PROTOTYPES
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

Window::Window(Widget& widget, int2 size, const string& name) : widget(widget) {
    x = XOpenDisplay(0);
    XSetErrorHandler(xErrorHandler);
    registerPoll();

    if(!size.x||!size.y) {
        XWindowAttributes root; XGetWindowAttributes(x, DefaultRootWindow(x), &root);
        if(!size.x) size.x=root.width; if(!size.y) size.y=root.height;
    }
    id = XCreateSimpleWindow(x,DefaultRootWindow(x),0,0,size.x,size.y,0,0,0);
    setProperty<char>("STRING", "WM_CLASS", name+"\0"_+name);
    XSelectInput(x, id, KeyPressMask|ButtonPressMask|PointerMotionMask|ExposureMask|StructureNotifyMask);
    setProperty<uint>("ATOM", "WM_PROTOCOLS", {(uint)Atom(WM_DELETE_WINDOW),0});

    XVisualInfo* vis = glXChooseVisual(x,DefaultScreen(x),(int[]){GLX_RGBA,GLX_DOUBLEBUFFER,0});
    ctx = glXCreateContext(x,vis,0,1);
    glXMakeCurrent(x, id, ctx);
    glViewport(widget.size=size);
    glClearColor(7./8,7./8,7./8,0);
    glBlendFunc(GL_DST_COLOR, GL_ZERO); //multiply (i.e darken) blend (allow blending with subpixel fonts)
    glEnable(GL_BLEND); //glEnable(GL_DEPTH_TEST);
    //glHint(GL_LINE_SMOOTH_HINT,GL_NICEST); glHint(GL_POLYGON_SMOOTH_HINT,GL_NICEST);
    //glEnable(GL_LINE_SMOOTH); glEnable(GL_POLYGON_SMOOTH);
    //glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE); glClearColor(0,0,0,0); //correct front-to-back coverage blend
}

void Window::render() {
    assert(visible);
    glClear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT);
    widget.render(int2(0,0));
    //flat.bind(); flat["scale"]=vec2(1,1); flat["offset"]=vec2(0,0); flat["color"] = vec4(/*7.0/8,7.0/8,7.0/8*/1,1,1,1);
    //glQuad(flat,vec2(-1,-1),vec2(1,1));
    glXSwapBuffers(x, id);
}

void Window::setVisible(bool visible) {
    this->visible=visible; if(visible) XMapWindow(x, id); else XUnmapWindow(x, id);
    XSync(x,0); event(pollfd());
}

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

uint Window::addHotKey(const string& key) {
    KeySym keysym = XStringToKeysym(&strz(key));
    assert(keysym != NoSymbol);
    XGrabKey(x, XKeysymToKeycode(x, keysym), AnyModifier, DefaultRootWindow(x), True, GrabModeAsync, GrabModeAsync);
    XFlush(x);
    return keysym;
}

pollfd Window::poll() { return {XConnectionNumber(x), POLLIN}; }
void Window::event(pollfd) {
    bool needRender=false;
    while(XEventsQueued(x, QueuedAfterFlush)) { XEvent e; XNextEvent(x,&e);
        if(e.type==MotionNotify) {
            needRender |= widget.event(int2(e.xmotion.x,e.xmotion.y), Motion,
                                       e.xmotion.state&Button1Mask ? Pressed : Released);
        } else if(e.type==ButtonPress) {
            needRender |= widget.event(int2(e.xbutton.x,e.xbutton.y), (Event)e.xbutton.button, Pressed);
        } else if(e.type==KeyPress) {
            auto key = XKeycodeToKeysym(x,e.xkey.keycode,0);
            if(key==XK_Escape) key=Quit; //DEBUG
            keyPress.emit((Event)key);
            widget.event(int2(),key==XK_Escape?Quit:(Event)key,Pressed);
        } else if(e.type==Expose && !e.xexpose.count) {
            needRender = true;
        } else if(e.type==ConfigureNotify) {
            XConfigureEvent ev = e.xconfigure;
            if(widget.size != int2(ev.width,ev.height)) {
                glViewport(widget.size = int2(ev.width,ev.height));
                widget.update();
                needRender = true;
            }
        } else if(e.type==MapNotify) {
            visible=true;
        } else if(e.type==UnmapNotify) {
            visible=false;
        } else if(e.type==ClientMessage) {
            keyPress.emit(Quit);
            widget.event(int2(),Quit,Pressed);
        }
    }
    if(needRender && visible) render();
}
