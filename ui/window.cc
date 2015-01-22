#include "window.h"
#include "render.h"
#include "x.h"
#include "png.h"
#include "time.h"
#include "gl.h"
#include <sys/shm.h>
extern "C" bool glXMakeCurrent(_XDisplay *dpy, uint drawable, __GLXcontextRec* ctx);

Window::Window(Widget* widget, int2 sizeHint, function<String()> title, bool show, const Image& icon, bool GL, Thread& thread)
	: Display(GL, thread), widget(widget), size(sizeHint), getTitle(title) {
    Display::onEvent.connect(this, &Window::onEvent);
	assert_(id && root && visual);
    send(CreateColormap{ .colormap=id+Colormap, .window=root, .visual=visual});

    if(sizeHint.x<=0) size.x=Display::size.x;
    if(sizeHint.y<=0) size.y=Display::size.y;
    if((sizeHint.x<0||sizeHint.y<0) && widget) {
        int2 hint (widget->sizeHint(vec2(size)));
		if(sizeHint.x<0) size.x=min(max(abs(hint.x),-sizeHint.x), Display::size.x);
		if(sizeHint.y<0) size.y=min(max(abs(hint.y),-sizeHint.y), Display::size.y-46);
    }
    assert_(size);
    send(CreateWindow{.id=id+XWindow, .parent=root, .width=uint16(size.x), .height=uint16(size.y), .visual=visual, .colormap=id+Colormap});
    send(ChangeProperty{.window=id+XWindow, .property=Atom("WM_PROTOCOLS"), .type=Atom("ATOM"),
                        .format=32, .length=1, .size=6+1}, raw(Atom("WM_DELETE_WINDOW")));
    send(ChangeProperty{.window=id+XWindow, .property=Atom("_KDE_OXYGEN_BACKGROUND_GRADIENT"), .type=Atom("CARDINAL"),
                        .format=32, .length=1, .size=6+1}, raw(1));
    setIcon(icon);
    send(CreateGC{.context=id+GraphicContext, .window=id+XWindow});
    send(Present::SelectInput{.window=id+XWindow, .eid=id+PresentEvent});
    actions[Escape] = []{requestTermination();};
    actions[PrintScreen] = [this]{writeFile(str(Date(currentTime())), encodePNG(target), home());};
	if(show) this->show();
	if(glContext) glXMakeCurrent(glDisplay, id+XWindow, glContext); // DRI2CreateDrawable
	//glEnable(GL_FRAMEBUFFER_SRGB);
	//((PFNGLXSWAPINTERVALMESAPROC)glXGetProcAddress((const GLubyte*)"glXSwapIntervalMESA"))(1);
}

Window::~Window() { close(); }

// Events
void Window::onEvent(const ref<byte> ge) {
    const XEvent& event = *(XEvent*)ge.data;
    uint8 type = event.type&0b01111111; //msb set if sent by SendEvent
    if(type==MotionNotify) { heldEvent = unique<XEvent>(event); queue(); }
    else {
        // Ignores autorepeat
		//if(heldEvent && heldEvent->type==KeyRelease && heldEvent->time==event.time && type==KeyPress) heldEvent=nullptr;
        if(heldEvent) { processEvent(heldEvent); heldEvent=nullptr; }
		/*if(type==KeyRelease) { heldEvent = unique<XEvent>(event); queue(); } // Hold release to detect any repeat
		else*/ if(processEvent(event)) {}
        else if(type==GenericEvent && event.genericEvent.ext == Present::EXT && event.genericEvent.type==Present::CompleteNotify) {
			const auto& completeNotify = *(struct Present::CompleteNotify*)&event;
			assert_(sizeof(XEvent)+event.genericEvent.size*4 == sizeof(completeNotify),
					sizeof(XEvent)+event.genericEvent.size*4, sizeof(completeNotify));
            assert_(state == Present);
            state = Idle;
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
	else if(type==KeyRelease) {}
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
    else if(type==Shm::event+Shm::Completion) { assert_(state == Copy); state = Present; }
    else return false;
    return true;
}

void Window::show() { send(MapWindow{.id=id}); send(RaiseWindow{.id=id}); }
void Window::hide() { send(UnmapWindow{.id=id}); }
void Window::close() {
	if(id) {
		send(FreeGC{.context=id+GraphicContext});
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
    send(ChangeProperty{.window=id+XWindow, .property=Atom("_NET_WM_ICON"), .type=Atom("CARDINAL"), .format=32,
                        .length=2+icon.width*icon.height, .size=uint16(6+2+icon.width*icon.height)},
                        raw(icon.width)+raw(icon.height)+cast<byte>(icon));
}
void Window::setSize(int2 size) { send(SetSize{.id=id+XWindow, .w=uint(size.x), .h=uint(size.y)}); }

// Render
void Window::render(shared<Graphics>&& graphics, int2 origin, int2 size) {
    updates.append( Update{move(graphics),origin,size} );
    if(updates && mapped && state == Idle) queue();
}
void Window::render() { assert_(size); updates.clear(); render(nullptr, int2(0), size); }

void Window::event() {
    Display::event();
    if(heldEvent) { processEvent(heldEvent); heldEvent = nullptr; }
	setTitle(getTitle ? getTitle() : widget->title());
    if(updates && state==Idle) {
        assert_(size);
		if(glContext) {
			GLFrameBuffer::bindWindow(0, size, ClearColor|ClearDepth, vec4(black, 1));
			 widget->graphics(vec2(size), Rect(vec2(0), vec2(size)));
			 glFlush();
		} else {
			if(target.size != size) {
				if(target) {
					send(FreePixmap{.pixmap=id+Pixmap}); target=Image();
					assert_(shm);
					send(Shm::Detach{.seg=id+Segment});
					shmdt(target.data);
					shmctl(shm, IPC_RMID, 0);
					shm = 0;
				} else assert_(!shm);

				uint stride = align(16, width);
				shm = check( shmget(0, height*stride*sizeof(byte4) , IPC_CREAT | 0777) );
				target = Image(buffer<byte4>((byte4*)check(shmat(shm, 0, 0)), height*stride, 0), size, stride);
				target.clear(byte4(0xFF));
				send(Shm::Attach{.seg=id+Segment, .shm=shm});
				send(CreatePixmap{.pixmap=id+Pixmap, .window=id+XWindow, .w=uint16(width), .h=uint16(size.y)});
			}

			Update update = updates.take(0);
			if(!update.graphics) update.graphics = widget->graphics(vec2(size), Rect::fromOriginAndSize(vec2(update.origin), vec2(update.size))); // TODO: partial render

			// Render background
			/***/ if(background==NoBackground) {}
			else if(background==White) fill(target, update.origin, update.size, 1, 1);
			else error((int)background);

			// Render graphics
			::render(target, update.graphics);
			send(Shm::PutImage{.window=id+Pixmap, .context=id+GraphicContext, .seg=id+Segment,
							   .totalW=uint16(target.stride), .totalH=uint16(target.height), .srcX=uint16(update.origin.x), .srcY=uint16(update.origin.y),
							   .srcW=uint16(update.size.x), .srcH=uint16(update.size.y), .dstX=uint16(update.origin.x), .dstY=uint16(update.origin.y),});
			state=Copy;
			send(Present::Pixmap{.window=id+XWindow, .pixmap=id+Pixmap}); //FIXME: update region
		}
    }
}
