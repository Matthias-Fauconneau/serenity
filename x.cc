#include "interface.h"
#include "gl.h"

#define Font XWindow
#define Window XWindow
#include <X11/Xlib.h>
#include <GL/glx.h>
#undef Window
#undef Font

struct X : Window, Poll {
	Display* x=0;
	GLXContext ctx=0;
	XWindow window=0;

	X() {
		x = XOpenDisplay(0);
		int attrib[] = {GLX_RGBA, GLX_DOUBLEBUFFER, 0};
		XVisualInfo* vis = glXChooseVisual(x,0,attrib);
		ctx = glXCreateContext(x,vis,0,1);
	}
	~X() { if(x) { XCloseDisplay(x); x=0; } }
	pollfd poll() { pollfd p; p.fd=XConnectionNumber(x); p.events=POLLIN; return p; }
	bool event(pollfd) {
		while(XEventsQueued(x, QueuedAfterFlush)) { XEvent ev; XNextEvent(x,&ev);
			if(ev.type==ClientMessage) { x=0; return false; }
			else if(ev.type==KeyPress) {
				auto key = XKeycodeToKeysym(x,ev.xkey.keycode,0);
				if(key == XK_Escape) return false;
			} else if(ev.type==ButtonPress) {
				Widget::event(int2(ev.xbutton.x,ev.xbutton.y),ev.xbutton.button,ev.xbutton.button);
			} else if(ev.type==MotionNotify) {
				Widget::event(int2(ev.xmotion.x,ev.xmotion.y),0,ev.xmotion.state&Button1Mask?1:0);
			} else ::log("XEvent");
		}
		return true;
	}
	void resize(int2 size) { this->size=size;
		if(window) { XResizeWindow(x,window,size.x,size.y); return; }
		window = XCreateSimpleWindow(x,DefaultRootWindow(x),0,0,size.x,size.y,0,0,0);
		XSelectInput(x,window,KeyPressMask|ButtonPressMask|PointerMotionMask);
		auto atom = XInternAtom(x,"WM_DELETE_WINDOW",True); XSetWMProtocols(x,window,&atom,1);
		XMapWindow(x, window);
		XFlush(x);

		glXMakeCurrent(x,window,ctx);
		glViewport(0,0,size.x,size.y);
		glClearColor(0,0,0,0);
		glHint(GL_LINE_SMOOTH_HINT,GL_NICEST);
		glHint(GL_POLYGON_SMOOTH_HINT,GL_NICEST);
		glEnable(GL_LINE_SMOOTH); glEnable(GL_POLYGON_SMOOTH);
		glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE);
		glEnable(GL_BLEND);
	}
	void rename(const string& name) { XStoreName(x,window,strz(name).data); }
	void render() {
		glClear(GL_COLOR_BUFFER_BIT);
		Widget::render(vec2(2,-2)/vec2(size),vec2(-1,1));
		flat.bind(); flat["scale"]=vec2(1,1); flat["offset"]=vec2(0,0); flat["color"] = vec4(7.0/8,7.0/8,7.0/8,1);
		glQuad(flat,vec2(-1,-1),vec2(1,1));
		glXSwapBuffers(x, window);
	}
};
Window* Window::instance() { return new X(); }
