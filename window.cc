#include "process.h"
#include "interface.h"
#include "gl.h"

#define Font XWindow
#define Window XWindow
#include <X11/Xlib.h>
#include <GL/glx.h>
#undef Window
#undef Font

Window::Window(int2 size, Widget& widget) : widget(widget) {
	x = XOpenDisplay(0);
	int attrib[] = {GLX_RGBA, GLX_DOUBLEBUFFER, 0};
	XVisualInfo* vis = glXChooseVisual(x,0,attrib);
	ctx = glXCreateContext(x,vis,0,1);
	window = XCreateSimpleWindow(x,DefaultRootWindow(x),0,0,size.x,size.y,0,0,0);
	widget.size=size;
	XSelectInput(x,window,KeyPressMask|ButtonPressMask|PointerMotionMask|ExposureMask|StructureNotifyMask);
	auto atom = XInternAtom(x,"WM_DELETE_WINDOW",True); XSetWMProtocols(x,window,&atom,1);
	registerPoll();
	XMapWindow(x, window);
	glXMakeCurrent(x,window,ctx);
	glViewport(0,0,size.x,size.y);
	glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);
	glHint(GL_POLYGON_SMOOTH_HINT,GL_NICEST);
	glEnable(GL_LINE_SMOOTH); glEnable(GL_POLYGON_SMOOTH);
	//glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE); glClearColor(0,0,0,0); //correct front-to-back coverage blend
	glBlendFunc(GL_DST_COLOR, GL_ZERO); glClearColor(1,1,1,0); //multiply (i.e darken) blend (allow component-wise black font blending)
	glEnable(GL_BLEND);
}
//Window::~Window() { if(x) { XCloseDisplay(x); x=0; } }
pollfd Window::poll() { pollfd p; p.fd=XConnectionNumber(x); p.events=POLLIN; return p; }
bool Window::event(pollfd) {
	while(XEventsQueued(x, QueuedAfterFlush)) { XEvent ev; XNextEvent(x,&ev);
		if(ev.type==MotionNotify) {
			widget.event(int2(ev.xmotion.x,ev.xmotion.y),0,ev.xmotion.state&Button1Mask?1:0);
		} else if(ev.type==ButtonPress) {
			widget.event(int2(ev.xbutton.x,ev.xbutton.y),ev.xbutton.button,Widget::Pressed);
		} else if(ev.type==KeyPress) {
			auto key = XKeycodeToKeysym(x,ev.xkey.keycode,0);
			keyPress.emit(key);
			if(key == XK_Escape) return false;
		} else if(ev.type==Expose && !ev.xexpose.count) {
			render();
		} else if(ev.type==MapNotify) {
			visible=true;
		} else if(ev.type==UnmapNotify) {
			visible=false;
		} else if(ev.type==ClientMessage) {
			x=0; return false;
		}
	}
	return true;
}
uint Window::addHotKey(const string& key) {
	KeySym keysym = XStringToKeysym(strz(key).data);
	XGrabKey(x, XKeysymToKeycode(x, keysym), AnyModifier, DefaultRootWindow(x), True, GrabModeAsync, GrabModeAsync);
	return keysym;
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
void Window::rename(const string& name) { XStoreName(x,window,strz(name).data); XFlush(x); }
void Window::render() {
	glClear(GL_COLOR_BUFFER_BIT);
	widget.render(vec2(2,-2)/vec2(widget.size),vec2(-1,1));
	//flat.bind(); flat["scale"]=vec2(1,1); flat["offset"]=vec2(0,0); flat["color"] = vec4(/*7.0/8,7.0/8,7.0/8*/1,1,1,1);
	//glQuad(flat,vec2(-1,-1),vec2(1,1));
	glXSwapBuffers(x, window);
}
