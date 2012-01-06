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

Window::Window(int2 size, Widget& widget) : widget(widget) {
	x = XOpenDisplay(0);
#if 1
	int attrib[] = {GLX_RGBA, GLX_RED_SIZE,8, GLX_GREEN_SIZE,8, GLX_BLUE_SIZE,8, GLX_ALPHA_SIZE,8, GLX_DEPTH_SIZE,24, GLX_STENCIL_SIZE,8, GLX_DOUBLEBUFFER, GLX_SAMPLE_BUFFERS,1, GLX_SAMPLES,8, 0};
	XVisualInfo* vis = glXChooseVisual(x,DefaultScreen(x),attrib);
	window = XCreateSimpleWindow(x,DefaultRootWindow(x),0,0,size.x,size.y,0,0,0);
	ctx = glXCreateContext(x,vis,0,1);
#else
	int attrib[] = { GLX_X_RENDERABLE,True, GLX_DRAWABLE_TYPE,GLX_WINDOW_BIT, GLX_RENDER_TYPE,GLX_RGBA_BIT, GLX_X_VISUAL_TYPE,GLX_TRUE_COLOR,
					 GLX_RED_SIZE,8, GLX_GREEN_SIZE,8, GLX_BLUE_SIZE,8, GLX_ALPHA_SIZE,8, GLX_DEPTH_SIZE,24, GLX_STENCIL_SIZE,8, GLX_DOUBLEBUFFER,True,
					 GLX_SAMPLE_BUFFERS,1, GLX_SAMPLES,8, 0 };
	int fbCount;
	GLXFBConfig* fbConfigs = glXChooseFBConfig(x,DefaultScreen(x),attrib,&fbCount);
	assert(fbCount==1);
	GLXFBConfig fbc = fbConfigs[0];
	XFree(fbConfigs);
	XVisualInfo* vis = glXGetVisualFromFBConfig(x, fbc);
	XSetWindowAttributes swa;
	swa.colormap = XCreateColormap(x,DefaultRootWindow(x),vis->visual,AllocNone);
	swa.background_pixmap=None; swa.border_pixel=0; swa.event_mask=StructureNotifyMask;
	window = XCreateWindow(x,DefaultRootWindow(x),0,0,size.x,size.y,0,vis->depth,InputOutput, vis->visual,CWBorderPixel|CWColormap|CWEventMask,&swa);

	int context_attribs[] = { GLX_CONTEXT_MAJOR_VERSION_ARB,3, GLX_CONTEXT_MINOR_VERSION_ARB,1, 0 };
	ctx = glXCreateContextAttribsARB(x,fbc,0,True,context_attribs);
	XSync(x,0);
	assert(glXIsDirect(x,ctx));
#endif

	widget.size=size;
	XSelectInput(x,window,KeyPressMask|ButtonPressMask|PointerMotionMask|ExposureMask|StructureNotifyMask);
	auto atom = XInternAtom(x,"WM_DELETE_WINDOW",True); XSetWMProtocols(x,window,&atom,1);
	registerPoll();
    //XMapWindow(x, window);
	//glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);
	//glHint(GL_POLYGON_SMOOTH_HINT,GL_NICEST);
	//glEnable(GL_LINE_SMOOTH); glEnable(GL_POLYGON_SMOOTH);
	//glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE); glClearColor(0,0,0,0); //correct front-to-back coverage blend
	//glBlendFunc(GL_DST_COLOR, GL_ZERO); glClearColor(1,1,1,0); //multiply (i.e darken) blend (allow component-wise black font blending)
	//glEnable(GL_BLEND);
    //glEnable(GL_DEPTH_TEST);
}
//Window::~Window() { if(x) { XCloseDisplay(x); x=0; } }


void Window::render() {
    assert(visible);
    glClear(GL_COLOR_BUFFER_BIT |GL_DEPTH_BUFFER_BIT);
    widget.render(vec2(2,-2)/vec2(widget.size),vec2(-1,1));
    //flat.bind(); flat["scale"]=vec2(1,1); flat["offset"]=vec2(0,0); flat["color"] = vec4(/*7.0/8,7.0/8,7.0/8*/1,1,1,1);
    //glQuad(flat,vec2(-1,-1),vec2(1,1));
    glXSwapBuffers(x, window);
}

void Window::setVisible(bool visible) {
    this->visible=visible;
    if(visible) {
        XMapWindow(x, window);
        glXMakeCurrent(x,window,ctx);
        glViewport(0,0,widget.size.x,widget.size.y);
    } else {
        XUnmapWindow(x,window);
    }
}

void Window::resize(int2 size) {
	if(!size.x||!size.y) {
		XWindowAttributes root; XGetWindowAttributes(x, DefaultRootWindow(x), &root);
		if(!size.x) size.x=root.width; if(!size.y) size.y=root.height;
	}
	XResizeWindow(x,window,size.x,size.y);
	widget.size=size;
}

void Window::setFullscreen(bool) {
	XEvent xev; clear(xev);
	xev.type = ClientMessage;
	xev.xclient.window = window;
	xev.xclient.message_type = XInternAtom(x, "_NET_WM_STATE", False);
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = 1;
	xev.xclient.data.l[1] = XInternAtom(x, "_NET_WM_STATE_FULLSCREEN", False);
	xev.xclient.data.l[2] = 0;
	XSendEvent(x, DefaultRootWindow(x), False, SubstructureNotifyMask, &xev);
}

void Window::rename(const string& name) {
    XStoreName(x,window,strz(name).data);
    XFlush(x);
}

uint Window::addHotKey(const string& key) {
    KeySym keysym = XStringToKeysym(strz(key).data);
    assert(keysym != NoSymbol);
    XGrabKey(x, XKeysymToKeycode(x, keysym), AnyModifier, DefaultRootWindow(x), True, GrabModeAsync, GrabModeAsync);
    XFlush(x);
    return keysym;
}

pollfd Window::poll() { pollfd p; p.fd=XConnectionNumber(x); p.events=POLLIN; return p; }
bool Window::event(pollfd) {
    bool needRender=false;
    while(XEventsQueued(x, QueuedAfterFlush)) { XEvent ev; XNextEvent(x,&ev);
        if(ev.type==MotionNotify) {
            needRender |= widget.event(int2(ev.xmotion.x,ev.xmotion.y), Widget::Motion,
                                       ev.xmotion.state&Button1Mask ? Widget::Pressed : Widget::Released);
        } else if(ev.type==ButtonPress) {
            needRender |= widget.event(int2(ev.xbutton.x,ev.xbutton.y), (Widget::Event)ev.xbutton.button, Widget::Pressed);
        } else if(ev.type==KeyPress) {
            auto key = XKeycodeToKeysym(x,ev.xkey.keycode,0);
            hotKeyTriggered.emit(key);
            if(key == XK_Escape) return false; //DEBUG
        } else if(ev.type==Expose && !ev.xexpose.count) {
            needRender=true;
        } else if(ev.type==MapNotify) {
            visible=true;
        } else if(ev.type==UnmapNotify) {
            visible=false;
        } else if(ev.type==ClientMessage) {
            x=0; return false;
        }
    }
    if(needRender) render();
    return true;
}
