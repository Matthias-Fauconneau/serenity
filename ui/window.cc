#include "window.h"
#include "ui/render.h"
#include "x.h"
#include "time.h"
#include <sys/shm.h>
ICON(text);
Image cursorIcon();
using namespace X11;

static Window* currentWindow = 0; // FIXME
bool hasFocus(Widget* widget) { assert_(currentWindow); return currentWindow->focus==widget; }
void setCursor(Cursor cursor) { assert_(currentWindow); currentWindow->setCursor(cursor); }
String getSelection(bool clipboard) { assert(currentWindow); return currentWindow->getSelection(clipboard); }

void Window::render(shared<Graphics>&& graphics, int2 origin, int2 size) {
	lock.lock();
	updates.append( Update{move(graphics),origin,size} );
	lock.unlock();
	queue();
}
void Window::render() { assert_(size); 	lock.lock(); updates.clear(); lock.unlock(); render(nullptr, int2(0), size); }

Window::Update Window::render(const Image& target) {
	if(!updates) return Update();
	lock.lock();
	Update update = updates.take(0);
	lock.unlock();
	if(!update.graphics) {
		currentWindow = this; // FIXME
		update.graphics = widget->graphics(vec2(size), Rect::fromOriginAndSize(vec2(update.origin), vec2(update.size)));
		currentWindow = 0;
	}
	fill(target, update.origin, update.size, backgroundColor, 1); // Clear framebuffer
	::render(target, update.graphics); 	// Render retained graphics
	return update;
}

XWindow::XWindow(Widget* widget, Thread& thread,  int2 sizeHint) : ::Window(widget, thread, sizeHint), XDisplay(thread) {
	XDisplay::onEvent.connect(this, &XWindow::onEvent);
	assert_(id && root && visual);

	if(sizeHint.x<=0) Window::size.x=XDisplay::size.x;
	if(sizeHint.y<=0) Window::size.y=XDisplay::size.y;
    if((sizeHint.x<0||sizeHint.y<0) && widget) {
		int2 hint (widget->sizeHint(vec2(Window::size)));
		if(sizeHint.x<0) Window::size.x=min(max(abs(hint.x),-sizeHint.x), XDisplay::size.x);
		if(sizeHint.y<0) Window::size.y=min(max(abs(hint.y),-sizeHint.y), XDisplay::size.y-46);
    }
	assert_(Window::size);
	send(CreateColormap{ .colormap=id+Colormap, .window=root, .visual=visual});
	send(CreateWindow{.id=id+Window, .parent=root, .width=uint16(Window::size.x), .height=uint16(Window::size.y), .visual=visual,
					  .colormap=id+Colormap});
	send(Present::SelectInput{.window=id+Window, .eid=id+PresentEvent});
	Window::actions[Escape] = []{requestTermination();};
	send(CreateGC{.context=id+GraphicContext, .window=id+Window});

	{  uint16 sequence = send(XRender::QueryPictFormats());
		buffer<int> fds;
		array<byte> replyData = readReply(sequence, 0, fds);
		auto r = *(XRender::QueryPictFormats::Reply*)replyData.data;
		buffer<XRender::PictFormInfo> formats = read<XRender::PictFormInfo>(r.numFormats);
		for(uint unused i: range(r.numScreens)) {
			auto screen = read<XRender::PictScreen>();
			for(uint unused i: range(screen.numDepths)) {
				auto depth = read<XRender::PictDepth>();
				array<XRender::PictVisual> visuals = read<XRender::PictVisual>(depth.numPictVisuals);
				if(depth.depth==32) for(auto pictVisual: visuals) if(pictVisual.visual==visual) format=pictVisual.format;
			}
		}
		assert(format);
		read<uint>(r.numSubpixels);
	}
}

XWindow::~XWindow() {
	if(id) {
		send(DestroyWindow{.id=id+Window});
		id = 0;
	}
}

// Events
void XWindow::onEvent(const ref<byte> ge) {
	const X11::Event& event = *(X11::Event*)ge.data;
    uint8 type = event.type&0b01111111; //msb set if sent by SendEvent
	/*if(type==MotionNotify) { heldEvent = unique<XEvent>(event); queue(); }
	else*/ {
		/*if(heldEvent) { processEvent(heldEvent); heldEvent=nullptr; }
        // Ignores autorepeat
		if(heldEvent && heldEvent->type==KeyRelease && heldEvent->time==event.time && type==KeyPress) heldEvent=nullptr;
		if(type==KeyRelease) { heldEvent = unique<XEvent>(event); queue(); } // Hold release to detect any repeat
		else*/ if(processEvent(event)) {}
        else if(type==GenericEvent && event.genericEvent.ext == Present::EXT && event.genericEvent.type==Present::CompleteNotify) {
			const auto& completeNotify = *(struct Present::CompleteNotify*)&event;
			assert_(sizeof(X11::Event)+event.genericEvent.size*4 == sizeof(completeNotify),
					sizeof(X11::Event)+event.genericEvent.size*4, sizeof(completeNotify));
			state = Idle;
			currentFrameCounterValue = completeNotify.msc;
			if(!firstFrameCounterValue) firstFrameCounterValue = currentFrameCounterValue;
        }
		else error("Unhandled event", ref<string>(X11::events)[type]);
    }
}

bool XWindow::processEvent(const X11::Event& e) {
    uint8 type = e.type&0b01111111; //msb set if sent by SendEvent
	currentWindow = this; // FIXME
    /**/ if(type==ButtonPress) {
		Widget* previousFocus = focus;
		if(widget->mouseEvent(vec2(e.x,e.y), vec2(Window::size), Press, (Button)e.key, focus) || focus!=previousFocus) render();
		drag = focus;
    }
    else if(type==ButtonRelease) {
        drag=0;
		if(e.key <= RightButton && widget->mouseEvent(vec2(e.x,e.y), vec2(Window::size), Release, (Button)e.key, focus)) render();
    }
    else if(type==KeyPress) {
        Key key = (Key)keySym(e.key, e.state); Modifiers modifiers = (Modifiers)e.state;
        if(focus && focus->keyPress(key, modifiers)) render(); // Normal keyPress event
        else {
			function<void()>* action = Window::actions.find(key);
            if(action) (*action)(); // Local window action
        }
	}
	else if(type==KeyRelease) {
		Key key = (Key)keySym(e.key, e.state); Modifiers modifiers = (Modifiers)e.state;
		if(focus && focus->keyRelease(key, modifiers)) render();
	}
    else if(type==MotionNotify) {
		if(drag && e.state&Button1Mask && drag->mouseEvent(vec2(e.x,e.y), vec2(Window::size), Motion, LeftButton, focus))
            render();
		else if(widget->mouseEvent(vec2(e.x,e.y), vec2(Window::size), Motion, (e.state&Button1Mask)?LeftButton:NoButton, focus))
            render();
    }
    else if(type==EnterNotify || type==LeaveNotify) {
		if(widget->mouseEvent( vec2(e.x,e.y), vec2(Window::size), type==EnterNotify?Enter:Leave,
							   e.state&Button1Mask?LeftButton:NoButton, focus) ) render();
    }
    else if(type==KeymapNotify) {}
    else if(type==Expose) { if(!e.expose.count && !(e.expose.x==0 && e.expose.y==0 && e.expose.w==1 && e.expose.h==1)) render(); }
    else if(type==UnmapNotify) mapped=false;
    else if(type==MapNotify) mapped=true;
    else if(type==ReparentNotify) {}
	else if(type==ConfigureNotify) { int2 size(e.configure.w,e.configure.h); if(size!=Window::size) { Window::size=size; render(); } }
    else if(type==GravityNotify) {}
    else if(type==ClientMessage) {
		function<void()>* action = Window::actions.find(Escape);
        if(action) (*action)(); // Local window action
        else if(focus && focus->keyPress(Escape, NoModifiers)) render(); // Translates to Escape keyPress event
        else requestTermination(0); // Exits application by default
    }
	else if(type==MappingNotify) {}
	else if(type==Shm::event+Shm::Completion) { assert_(state == Copy); state = Present; }
	else { currentWindow = 0; return false; }
	currentWindow = 0;
    return true;
}

void XWindow::show() { send(MapWindow{.id=id}); send(RaiseWindow{.id=id}); }
void XWindow::hide() { send(UnmapWindow{.id=id}); }

void XWindow::setTitle(string title) {
	if(!title || title == this->title) return;
	this->title = copyRef(title);
	send(ChangeProperty{.window=id+Window, .property=Atom("_NET_WM_NAME"), .type=Atom("UTF8_STRING"), .format=8,
						.length=uint(title.size), .size=uint16(6+align(4, title.size)/4)}, title);
}
void XWindow::setIcon(const Image& icon) {
	if(icon) send(ChangeProperty{.window=id+Window, .property=Atom("_NET_WM_ICON"), .type=Atom("CARDINAL"), .format=32,
                        .length=2+icon.width*icon.height, .size=uint16(6+2+icon.width*icon.height)},
                        raw(icon.width)+raw(icon.height)+cast<byte>(icon));
}
void XWindow::setSize(int2 size) { send(SetSize{.id=id+Window, .w=uint(size.x), .h=uint(size.y)}); }

void XWindow::event() {
	XDisplay::event();
	//if(heldEvent) { processEvent(heldEvent); heldEvent = nullptr; }
    setTitle(getTitle ? getTitle() : widget->title());

	if(state!=Idle) return;

	if(target.size != Window::size) {
		if(target) {
			send(FreePixmap{.pixmap=id+Pixmap}); target=Image();
			assert_(shm);
			send(Shm::Detach{.seg=id+Segment});
			shmdt(target.data);
			shmctl(shm, IPC_RMID, 0);
			shm = 0;
		} else assert_(!shm);

		uint stride = align(16, Window::size.x);
		shm = check( shmget(0, Window::size.y*stride*sizeof(byte4) , IPC_CREAT | 0777) );
		target = Image(buffer<byte4>((byte4*)check(shmat(shm, 0, 0)), Window::size.y*stride, 0), Window::size, stride);
		target.clear(byte4(0xFF));
		send(Shm::Attach{.seg=id+Segment, .shm=shm});
		send(CreatePixmap{.pixmap=id+Pixmap, .window=id+Window, .w=uint16(Window::size.x), .h=uint16(Window::size.y)});
	}

	Update update = render(target);

	send(Shm::PutImage{.window=id+Pixmap, .context=id+GraphicContext, .seg=id+Segment,
					   .totalW=uint16(target.stride), .totalH=uint16(target.height), .srcX=uint16(update.origin.x), .srcY=uint16(update.origin.y),
					   .srcW=uint16(update.size.x), .srcH=uint16(update.size.y), .dstX=uint16(update.origin.x), .dstY=uint16(update.origin.y),});
	state=Copy;
	send(Present::Pixmap{.window=id+Window, .pixmap=id+Pixmap}); //FIXME: update region
}

void XWindow::setCursor(::Cursor cursor) {
	if(this->cursor != cursor) {
		static Image (*icons[])() = { cursorIcon, textIcon };
		static constexpr int2 hotspots[] = { int2(5,0), int2(4,9) }; // FIXME
		const Image& icon = icons[uint(cursor)]();
		Image premultiplied(icon.size);
		for(uint y: range(icon.size.y)) for(uint x: range(icon.size.x)) {
			byte4 p=icon(x,y); premultiplied(x,y)=byte4(p.b*p.a/255,p.g*p.a/255,p.r*p.a/255,p.a);
		}
		send(CreatePixmap{.pixmap=id+CursorPixmap, .window=id, .w=uint16(icon.size.x), .h=uint16(icon.size.y)});
		send(PutImage{.drawable=id+CursorPixmap, .context=id+GraphicContext, .w=uint16(icon.size.x), .h=uint16(icon.size.y),
					  .size=uint16(6+premultiplied.Ref::size)}, cast<byte>(premultiplied));
		send(XRender::CreatePicture{.picture=id+Picture, .drawable=id+CursorPixmap, .format=format});
		int2 hotspot = hotspots[uint(cursor)];
		send(XRender::CreateCursor{.cursor=id+Cursor, .picture=id+Picture, .x=uint16(hotspot.x), .y=uint16(hotspot.y)});
		send(SetWindowCursor{.window=id, .cursor=id+Cursor});
		send(FreeCursor{.cursor=id+Cursor});
		send(XRender::FreePicture{.picture=id+Picture});
		send(FreePixmap{.pixmap=id+CursorPixmap});
		this->cursor = cursor;
	}
}

String XWindow::getSelection(bool clipboard) {
	uint owner = request(GetSelectionOwner{.selection=clipboard?Atom("CLIPBOARD"_):1}).owner;
	if(!owner) return String();
	send(ConvertSelection{.requestor=id, .selection=clipboard?Atom("CLIPBOARD"_):1, .target=Atom("UTF8_STRING"_)});
	waitEvent(SelectionNotify);
	return getProperty<byte>(id, "UTF8_STRING"_);
}

// --- DRM Window

DRMWindow::DRMWindow(Widget* widget, Thread& thread) : Window(widget, thread) {
	if(!display) display = unique<Display>();  // Created by the first instantiation of a window, deleted at exit
	display->windows.append(this);
	size = display->buffers[0].target.size;
	actions[Escape] = []{requestTermination();};
	render();
}
DRMWindow::~DRMWindow() {
	display->windows.remove(this);
	if(!display->windows) display=null;
}

// FIXME: Schedules window rendering after all events have been processed
void DRMWindow::event() {
	if(!updates) return;
	render(display->buffers[1].target);
	display->swapBuffers();
}

void DRMWindow::mouseEvent(int2 cursor, ::Event event, Button button) {
	if(event==Press) {
		Widget* previousFocus = focus;
		if(widget->mouseEvent(vec2(cursor), vec2(size), Press, button, focus) || focus!=previousFocus) render();
		drag = focus;
	}
	if(event==Release) {
		drag=0;
		if(button <= RightButton && widget->mouseEvent(vec2(cursor), vec2(size), Release, button, focus)) render();
	}
	if(event==Motion) {
		if(drag && button==LeftButton && drag->mouseEvent(vec2(cursor), vec2(size), Motion, button, focus)) render();
		else if(widget->mouseEvent(vec2(cursor), vec2(size), Motion, button, focus)) render();
	}
}

void DRMWindow::keyPress(Key key) {
	if(focus && focus->keyPress(key, NoModifiers)) render();
	else {
		function<void()>* action = actions.find(key);
		if(action) (*action)(); // Local window action
	}
}

void DRMWindow::setCursor(::Cursor) {}
String DRMWindow::getSelection(bool) { return {}; }

unique<Display> DRMWindow::display = null;

unique<Window> window(Widget* widget, int2 size, Thread& thread) {
	if(getenv("DISPLAY")) {
		auto window = unique<XWindow>(widget, thread, size);
		window->show();
		return move(window);
	}
	return unique<DRMWindow>(widget, thread);
}
