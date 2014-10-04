#include "window.h"
#include "render.h"
#include "x.h"
#include <sys/shm.h>

Window::Window(Widget* widget, int2 size, const string& title, const Image& icon) : widget(widget) {
    onEvent.connect(this, &Window::processEvent);
    send(CreateColormap{ .colormap=id+Colormap, .window=root, .visual=visual});

    if((size.x<0||size.y<0) && widget) {
        int2 hint=widget->sizeHint(0);
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=screenX;
    if(size.y==0) size.y=screenY;
    send(CreateWindow{.id=id+XWindow, .parent=root, .width=uint16(size.x), .height=uint16(size.y), .visual=visual, .colormap=id+Colormap});
    send(ChangeProperty{.window=id+XWindow, .property=Atom("WM_PROTOCOLS"_), .type=Atom("ATOM"_), .format=32,
                        .length=1, .size=6+1}, raw(Atom("WM_DELETE_WINDOW"_)));
    send(ChangeProperty{.window=id+XWindow, .property=Atom("_KDE_OXYGEN_BACKGROUND_GRADIENT"_), .type=Atom("CARDINAL"_), .format=32,
                        .length=1, .size=6+1}, raw(1));
    setTitle(title);
    setIcon(icon);
    send(CreateGC{.context=id+GraphicContext, .window=id+XWindow});
    send(Present::SelectInput{.window=id+XWindow, .eid=id+PresentEvent});
    show();

    actions[Escape] = []{exit();};
}

Window::~Window() {
    send(FreeGC{.context=id+GraphicContext});
    send(DestroyWindow{.id=id+XWindow});
}

// Events
void Window::processEvent(const ref<byte>& ge) {
    const XEvent& e = *(XEvent*)ge.data;
    uint8 type = e.type&0b01111111; //msb set if sent by SendEvent
    if(type==MotionNotify) {
        cursorPosition = int2(e.x,e.y);
        cursorState = e.state;
        motionPending = true;
        return;
    }
    if(motionPending) {
        motionPending = false;
        if(drag && cursorState&Button1Mask && drag->mouseEvent(cursorPosition, size, Widget::Motion, Widget::LeftButton, focus)) render();
        else if(widget->mouseEvent(cursorPosition, size, Widget::Motion, (cursorState&Button1Mask)?Widget::LeftButton:Widget::NoButton, focus)) render();
    }
    /**/ if(type==ButtonPress) {
        Widget* focus=this->focus; this->focus=0;
        if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Press, (Widget::Button)e.key, focus) || this->focus!=focus) render();
        drag = focus;
    }
    else if(type==ButtonRelease) {
        drag=0;
        if(e.key <= Widget::RightButton && widget->mouseEvent(int2(e.x,e.y), size, Widget::Release, (Widget::Button)e.key, focus)) render();
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
    else if(type==EnterNotify || type==LeaveNotify) {
        if(widget->mouseEvent( int2(e.x,e.y), size, type==EnterNotify?Widget::Enter:Widget::Leave,
                               e.state&Button1Mask?Widget::LeftButton:Widget::NoButton, focus) ) render();
    }
    else if(type==KeymapNotify) {}
    else if(type==Expose) { if(!e.expose.count && !(e.expose.x==0 && e.expose.y==0 && e.expose.w==1 && e.expose.h==1)) render(); }
    else if(type==DestroyNotify) {}
    else if(type==UnmapNotify) mapped=false;
    else if(type==MapNotify) mapped=true;
    else if(type==ReparentNotify) {}
    else if(type==ConfigureNotify) setSize(int2(e.configure.w,e.configure.h));
    else if(type==GravityNotify) {}
    else if(type==ClientMessage) {
        function<void()>* action = actions.find(Escape);
        if(action) (*action)(); // Local window action
        else if(focus && focus->keyPress(Escape, NoModifiers)) render(); // Translates to Escape keyPress event
        else exit(0); // Exits application by default
    }
    else if(type==MappingNotify) {}
    else if(type==Shm::event+Shm::Completion) { assert_(state == Copy); state = Present; }
    else if(type==GenericEvent && e.genericEvent.ext == Present::EXT && e.genericEvent.type==Present::CompleteNotify) {
        assert_(state == Present);
        state = Idle;
    }
    else log("Unhandled event", ref<string>(X11::events)[type]);
    if(updates && mapped && state == Idle) queue();
}

void Window::show() { send(MapWindow{.id=id}); send(RaiseWindow{.id=id}); }
void Window::hide() { send(UnmapWindow{.id=id}); }

void Window::setTitle(const string& title) {
    send(ChangeProperty{.window=id+XWindow, .property=Atom("_NET_WM_NAME"_), .type=Atom("UTF8_STRING"_), .format=8,
                        .length=uint(title.size), .size=uint16(6+align(4, title.size)/4)}, title);
}
void Window::setIcon(const Image& icon) {
    send(ChangeProperty{.window=id+XWindow, .property=Atom("_NET_WM_ICON"_), .type=Atom("CARDINAL"_), .format=32,
                        .length=2+icon.width*icon.height, .size=uint16(6+2+icon.width*icon.height)}, raw(icon.width)+raw(icon.height)+(ref<byte>)icon);
}

// Render
#include "trace.h"
void Window::setSize(int2 size) {
    assert_(size);
    if(size == this->size) return;
    if(this->size) send(FreePixmap{.pixmap=id+Pixmap});
    this->size = size;
    if(shm) {
        send(Shm::Detach{.seg=id+Segment});
        shmdt(target.pixels);
        shmctl(shm, IPC_RMID, 0);
    }
    uint stride = align(16, width);
    shm = check( shmget(0, height*stride*sizeof(byte4) , IPC_CREAT | 0777) );
    target = Image(buffer<byte4>((byte4*)check(shmat(shm, 0, 0)), height*stride), size, stride);
    target.pixels.clear(0xFF);
    send(Shm::Attach{.seg=id+Segment, .shm=shm});
    send(CreatePixmap{.pixmap=id+Pixmap, .window=id+XWindow, .w=uint16(width), .h=uint16(size.y)});
    render();
}

void Window::render(Graphics&& graphics, int2 origin, int2 size) {
    updates << Update{move(graphics),origin,size};
    if(updates && mapped && state == Idle) queue();
}
void Window::render() { updates.clear(); render({},int2(0),size); }

void Window::event() {
    Display::event();
    if(updates && state==Idle) {
        Update update = updates.take(0);
        if(!update.graphics) update.graphics = widget->graphics(size); // TODO: partial render
        assert_(update.graphics);

        // Render background
        /***/ if(background==NoBackground) {}
        else if(background==White) fill(target, update.origin, update.size, 1, 1);
        else if(background==Black) fill(target, update.origin, update.size, 0, 1);
        else if(background==Oxygen) oxygen(target, update.origin, update.origin+update.size);
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
