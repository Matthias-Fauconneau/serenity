#include "window.h"
#include "render.h"
#include "x.h"
#include "png.h"
#include "time.h"
#include "gl.h"

#if 0 // DRI3
#include <sys/shm.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>
#include <gbm.h> // gbm
#include <EGL/egl.h> // EGL
#include <EGL/eglext.h>
#include <GL/gl.h> // drm
#include <sys/mman.h>
extern "C" int drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd);
#else // GLX/Xlib/DRI2
#undef packed
#define Time XTime
#define Cursor XCursor
#define Depth XXDepth
#define Window XWindow
#define Screen XScreen
#define XEvent XXEvent
#define Display XDisplay
#define Font XFont
#include <GL/glx.h> //X11
#undef Time
#undef Cursor
#undef Depth
#undef Window
#undef Screen
#undef XEvent
#undef Display
#undef Font
#undef None
#endif

Window::Window(Widget* widget, int2 sizeHint, function<String()> title, bool show, const Image& icon, Thread& thread)
	: Display(thread), widget(widget), size(sizeHint), getTitle(title) {
    Display::onEvent.connect(this, &Window::onEvent);
	assert_(id && root && visual);

    if(sizeHint.x<=0) size.x=Display::size.x;
    if(sizeHint.y<=0) size.y=Display::size.y;
    if((sizeHint.x<0||sizeHint.y<0) && widget) {
        int2 hint (widget->sizeHint(vec2(size)));
		if(sizeHint.x<0) size.x=min(max(abs(hint.x),-sizeHint.x), Display::size.x);
		if(sizeHint.y<0) size.y=min(max(abs(hint.y),-sizeHint.y), Display::size.y-46);
    }
    assert_(size);
	send(CreateColormap{ .colormap=id+Colormap, .window=root, .visual=visual});
	send(CreateWindow{.id=id+XWindow, .parent=root, .width=uint16(width), .height=uint16(height), .visual=visual, .colormap=id+Colormap});
	setIcon(icon);
    send(Present::SelectInput{.window=id+XWindow, .eid=id+PresentEvent});
    actions[Escape] = []{requestTermination();};
	//actions[PrintScreen] = [this]{writeFile(str(Date(currentTime())), encodePNG(dmaBuffer), home());};
	if(show) this->show();
#if 0 // DRI3
		// -- EGL Render node initialization
		request(DRI3::QueryVersion());
		drmDevice = ({buffer<int> fds; requestFD(DRI3::Open{.drawable=id+XWindow}, fds); fds[0]; });
		gbmDevice = gbm_create_device(drmDevice);
		eglDevice = eglGetDisplay((EGLNativeDisplayType)gbmDevice);
		assert_(eglDevice);
		EGLint major, minor; eglInitialize(eglDevice, &major, &minor);
		eglBindAPI(EGL_OPENGL_API);
		EGLint n;
		eglChooseConfig(eglDevice, (EGLint[]){EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
											  EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE}, &eglConfig, 1, &n);
		assert_(eglConfig);
		eglContext = eglCreateContext(eglDevice, eglConfig, 0, (EGLint[]) {
										  EGL_CONTEXT_MAJOR_VERSION_KHR,  3,  EGL_CONTEXT_MINOR_VERSION_KHR, 3,
										  EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR, EGL_NONE});
		assert_(eglContext);
		gbmSurface = gbm_surface_create(gbmDevice, width, height, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
		eglSurface = eglCreateWindowSurface(eglDevice, eglConfig, (EGLNativeWindowType)gbmSurface, 0);
		assert_(eglSurface);
		eglMakeCurrent(eglDevice, eglSurface, eglSurface, eglContext);
		surfaceSize = size;
#else // GLX/Xlib/DRI2
	glDisplay = XOpenDisplay(strz(getenv("DISPLAY"_,":0"_))); assert_(glDisplay);
	const int fbAttribs[] = {GLX_DOUBLEBUFFER, 1, GLX_RED_SIZE, 8, GLX_GREEN_SIZE, 8, GLX_BLUE_SIZE, 8, GLX_DEPTH_SIZE, 24,
							 GLX_SAMPLE_BUFFERS, 1, GLX_SAMPLES, 8, 0};
	int fbCount=0; fbConfig = glXChooseFBConfig(glDisplay, 0, fbAttribs, &fbCount)[0]; assert(fbConfig && fbCount);
	initializeThreadGLContext();
#endif
}

Window::~Window() { close(); }

// Events
void Window::onEvent(const ref<byte> ge) {
    const XEvent& event = *(XEvent*)ge.data;
    uint8 type = event.type&0b01111111; //msb set if sent by SendEvent
	/*if(type==MotionNotify) { heldEvent = unique<XEvent>(event); queue(); }
	else*/ {
		//if(heldEvent) { processEvent(heldEvent); heldEvent=nullptr; }
        // Ignores autorepeat
		//if(heldEvent && heldEvent->type==KeyRelease && heldEvent->time==event.time && type==KeyPress) heldEvent=nullptr;
		/*if(type==KeyRelease) { heldEvent = unique<XEvent>(event); queue(); } // Hold release to detect any repeat
		else*/ if(processEvent(event)) {}
        else if(type==GenericEvent && event.genericEvent.ext == Present::EXT && event.genericEvent.type==Present::CompleteNotify) {
			const auto& completeNotify = *(struct Present::CompleteNotify*)&event;
			assert_(sizeof(XEvent)+event.genericEvent.size*4 == sizeof(completeNotify),
					sizeof(XEvent)+event.genericEvent.size*4, sizeof(completeNotify));
#if DRI3
			assert_(bo);
			gbm_surface_release_buffer(gbmSurface, bo);
			bo = 0;
#endif
			currentFrameCounterValue = completeNotify.msc;
			if(!firstFrameCounterValue) firstFrameCounterValue = currentFrameCounterValue;
        }
		else error("Unhandled event", ref<string>(X11::events)[type]);
    }
}

bool Window::processEvent(const XEvent& e) {
    uint8 type = e.type&0b01111111; //msb set if sent by SendEvent
    /**/ if(type==ButtonPress) {
		Widget* previousFocus = focus;
        if(widget->mouseEvent(vec2(e.x,e.y), vec2(size), Widget::Press, (Widget::Button)e.key, focus) || focus!=previousFocus) render();
		drag = focus;
    }
    else if(type==ButtonRelease) {
        drag=0;
        if(e.key <= Widget::RightButton && widget->mouseEvent(vec2(e.x,e.y), vec2(size), Widget::Release, (Widget::Button)e.key, focus)) render();
    }
    else if(type==KeyPress) {
        Key key = (Key)keySym(e.key, e.state); Modifiers modifiers = (Modifiers)e.state;
        if(focus && focus->keyPress(key, modifiers)) render(); // Normal keyPress event
        else {
            function<void()>* action = actions.find(key);
            if(action) (*action)(); // Local window action
        }
	}
	else if(type==KeyRelease) {
		Key key = (Key)keySym(e.key, e.state); Modifiers modifiers = (Modifiers)e.state;
		if(focus && focus->keyRelease(key, modifiers)) render();
	}
    else if(type==MotionNotify) {
        if(drag && e.state&Button1Mask && drag->mouseEvent(vec2(e.x,e.y), vec2(size), Widget::Motion, Widget::LeftButton, focus))
            render();
        else if(widget->mouseEvent(vec2(e.x,e.y), vec2(size), Widget::Motion, (e.state&Button1Mask)?Widget::LeftButton:Widget::NoButton, focus))
            render();
    }
    else if(type==EnterNotify || type==LeaveNotify) {
        if(widget->mouseEvent( vec2(e.x,e.y), vec2(size), type==EnterNotify?Widget::Enter:Widget::Leave,
                               e.state&Button1Mask?Widget::LeftButton:Widget::NoButton, focus) ) render();
    }
    else if(type==KeymapNotify) {}
    else if(type==Expose) { if(!e.expose.count && !(e.expose.x==0 && e.expose.y==0 && e.expose.w==1 && e.expose.h==1)) render(); }
    //else if(type==DestroyNotify) {}
    else if(type==UnmapNotify) mapped=false;
    else if(type==MapNotify) mapped=true;
    else if(type==ReparentNotify) {}
    else if(type==ConfigureNotify) { int2 size(e.configure.w,e.configure.h); if(size!=this->size) { this->size=size; render(); } }
    else if(type==GravityNotify) {}
    else if(type==ClientMessage) {
        function<void()>* action = actions.find(Escape);
        if(action) (*action)(); // Local window action
        else if(focus && focus->keyPress(Escape, NoModifiers)) render(); // Translates to Escape keyPress event
        else requestTermination(0); // Exits application by default
    }
	else if(type==MappingNotify) {}
    else return false;
    return true;
}

void Window::show() { send(MapWindow{.id=id}); send(RaiseWindow{.id=id}); }
void Window::hide() { send(UnmapWindow{.id=id}); }
void Window::close() {
	if(id) {
		send(DestroyWindow{.id=id+XWindow});
		id = 0;
	}
	unregisterPoll();
}

void Window::setTitle(string title) {
	if(!title || title == this->title) return;
	this->title = copyRef(title);
	send(ChangeProperty{.window=id+XWindow, .property=Atom("_NET_WM_NAME"), .type=Atom("UTF8_STRING"), .format=8,
						.length=uint(title.size), .size=uint16(6+align(4, title.size)/4)}, title);
}
void Window::setIcon(const Image& icon) {
	if(icon) send(ChangeProperty{.window=id+XWindow, .property=Atom("_NET_WM_ICON"), .type=Atom("CARDINAL"), .format=32,
                        .length=2+icon.width*icon.height, .size=uint16(6+2+icon.width*icon.height)},
                        raw(icon.width)+raw(icon.height)+cast<byte>(icon));
}
void Window::setSize(int2 size) { send(SetSize{.id=id+XWindow, .w=uint(size.x), .h=uint(size.y)}); }

// Render
void Window::render(shared<Graphics>&& graphics, int2 origin, int2 size) {
    updates.append( Update{move(graphics),origin,size} );
	if(updates /*&& mapped && state == Idle*/) queue();
}
void Window::render() { Locker lock(this->lock); assert_(size); updates.clear(); render(nullptr, int2(0), size); }

#if DRI3
struct DMABuf {
	int fd = 0;
	DMABuf() {}
	DMABuf(const DMABuf&)=delete; DMABuf& operator=(const DMABuf&)=delete;
	~DMABuf() { log("~DMA", fd); close(fd); }
};
static void destroy_user_data(gbm_bo*, void* dmabuf) { delete (DMABuf*)dmabuf; }
#endif

void Window::event() {
	Display::event();
	//if(heldEvent) { processEvent(heldEvent); heldEvent = nullptr; }
	//setTitle(getTitle ? getTitle() : widget->title());
	if(!updates) return;
	assert_(size);
#if DRI3
	if(bo) return; // Wait for Present
	assert_(!bo);
	if(surfaceSize != size) {
		log("Size changed", surfaceSize, size);
		eglDestroySurface(eglDevice, eglSurface);
		gbm_surface_destroy(gbmSurface);
		gbmSurface = gbm_surface_create(gbmDevice, width, height, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
		eglSurface = eglCreateWindowSurface(eglDevice, eglConfig, (EGLNativeWindowType)gbmSurface, 0);
		eglMakeCurrent(eglDevice, eglSurface, eglSurface, eglContext);
		surfaceSize = size;
	}
#endif

	lock.lock();
	Update update = updates.take(0);
	lock.unlock();
	{Locker lock(glLock); // FIXME
		// Widget::graphics may renders using GL immediately and/or return primitives
		GLFrameBuffer::bindWindow(0, size, ClearColor|ClearDepth, vec4(backgroundColor,1));
		if(!update.graphics) update.graphics = widget->graphics(vec2(size), Rect::fromOriginAndSize(vec2(update.origin), vec2(update.size)));
	}
#if DRI3
	assert_(gbm_surface_has_free_buffers(gbmSurface));
	assert_( eglSwapBuffers(eglDevice, eglSurface) );
	bo = gbm_surface_lock_front_buffer(gbmSurface);
	DMABuf* dmabuf = (DMABuf*)gbm_bo_get_user_data(bo);
	if(!dmabuf) {
		dmabuf = new DMABuf();
		drmPrimeHandleToFD(drmDevice, gbm_bo_get_handle(bo).u32, 0, &dmabuf->fd);
		//byte4* pixels = (byte4*)mmap(0, height*width*4, PROT_READ|PROT_WRITE, MAP_SHARED, dmabuf->fd, 0);
		//mref<byte4>(pixels, height*width).clear(0);
		send(DRI3::PixmapFromBuffer{.pixmap=id+Pixmap,.drawable=id+XWindow,.bufferSize=height*width*4,
									.width=uint16(width),.height=uint16(height),.stride=uint16(width*4)}, dmabuf->fd);
		gbm_bo_set_user_data(bo, dmabuf, &destroy_user_data);
	}
	//::render(Image(dmabuf->pointer, size), update.graphics); // FIXME: Render retained graphics
	send(Present::Pixmap{.window=id+XWindow, .pixmap=id+Pixmap}); //FIXME: update region
#else
	if(update.graphics) {
		Image target(size); target.clear(0xFF);
		::render(target, update.graphics); // FIXME: Render retained graphics
		for(int y: range(target.size.y/2)) for(int x: range(target.size.x)) swap(target(x, y), target(x, target.size.y-1-y));
		GLFrameBuffer::blitWindow(target);
	}
	{Locker lock(glLock);
		glXSwapBuffers(glDisplay, id);
	}
#endif
	//assert_(updates.size<=1);
}

void Window::initializeThreadGLContext() {
	const int contextAttribs[] = { GLX_CONTEXT_MAJOR_VERSION_ARB, 3, GLX_CONTEXT_MINOR_VERSION_ARB, 3, 0};
	glContext = ((PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddress((const GLubyte*)"glXCreateContextAttribsARB"))
			(glDisplay, fbConfig, glContext, 1, contextAttribs);
	glXMakeCurrent(glDisplay, id+XWindow, glContext);
}
