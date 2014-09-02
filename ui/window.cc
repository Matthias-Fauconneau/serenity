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
    if(size.y==0) size.y=screenY-16;
    this->size=size;
    send(CreateWindow{.id=id+XWindow, .parent=root, .width=uint16(size.x), .height=uint16(size.y), .visual=visual, .colormap=id+Colormap});
    send(Present::SelectInput{.window=id+XWindow, .eid=id+PresentEvent});
    send(CreateGC{.context=id+GraphicContext, .window=id+XWindow});
    send(ChangeProperty{.window=id+XWindow, .property=Atom("WM_PROTOCOLS"_), .type=Atom("ATOM"_), .format=32,
                        .length=1, .size=6+1}, raw(Atom("WM_DELETE_WINDOW"_)));
    send(ChangeProperty{.window=id+XWindow, .property=Atom("_KDE_OXYGEN_BACKGROUND_GRADIENT"_), .type=Atom("CARDINAL"_), .format=32,
                        .length=1, .size=6+1}, raw(1));
    setTitle(title);
    setIcon(icon);
    actions[Escape] = []{exit();};
    show();
}

Window::~Window() {
    send(FreeGC{.context=id+GraphicContext});
    send(DestroyWindow{.id=id+XWindow});
}

// Events
void Window::processEvent(const ref<byte>& ge) {
    const XEvent& e = *(XEvent*)ge.data;
    uint8 type = e.type&0b01111111; //msb set if sent by SendEvent
    /**/ if(type==MotionNotify) {
        if(drag && e.state&Button1Mask && drag->mouseEvent(int2(e.x,e.y), size, Widget::Motion, Widget::LeftButton, focus)) render();
        else if(widget->mouseEvent(int2(e.x,e.y), size, Widget::Motion, (e.state&Button1Mask)?Widget::LeftButton:Widget::NoButton, focus)) render();
    }
    else if(type==ButtonPress) {
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
    else if(type==MapNotify) { mapped=true; if(needUpdate) queue(); }
    else if(type==ReparentNotify) {}
    else if(type==ConfigureNotify) {}
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
        if(needUpdate && mapped) queue();
    }
    else log("Unhandled event", ref<string>(X11::events)[type]);
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
void Window::render() { needUpdate=true; if(mapped) queue(); }

void Window::event() {
    Display::event();
    if(needUpdate && state==Idle) {
        needUpdate = false;
        assert(size);
        if(target.size != size) {
            if(shm) {
                send(Shm::Detach{.seg=id+Segment});
                shmdt(target.data);
                shmctl(shm, IPC_RMID, 0);
            }
            target.size=size; target.stride=align(16,size.x);
            target.buffer.size = target.height*target.stride;
            shm = check( shmget(0, target.buffer.size*sizeof(byte4) , IPC_CREAT | 0777) );
            target.buffer.data = target.data = (byte4*)check( shmat(shm, 0, 0) ); assert(target.data);
            target.buffer.clear(0xFF);
            send(Shm::Attach{.seg=id+Segment, .shm=shm});
            send(CreatePixmap{.pixmap=id+Pixmap, .window=id, .w=uint16(size.x), .h=uint16(size.y)});
        }

        { // Render background
            int2 size = target.size;
            /***/ if(background==NoBackground) {}
            else if(background==White) {
                for(uint y: range(size.y)) for(uint x: range(size.x)) target.data[y*target.stride+x] = 0xFF;
            }
            else if(background==Black) {
                for(uint y: range(size.y)) for(uint x: range(size.x)) target.data[y*target.stride+x] = byte4(0, 0, 0, 0xFF);
            }
#if OXYGEN
            else if(background==Oxygen) { // Oxygen-like radial gradient background
                const int y0 = -32-8, splitY = min(300, 3*size.y/4);
                const vec3 radial = vec3(246./255); // linear
                const vec3 top = vec3(221, 223, 225); // sRGB
                const vec3 bottom = vec3(184, 187, 194); // sRGB
                const vec3 middle = (bottom+top)/2.f; //FIXME
                // Draws upper linear gradient
                for(int y: range(0, max(0, y0+splitY/2))) {
                    float t = (float) (y-y0) / (splitY/2);
                    for(int x: range(size.x)) target(x,y) = byte4(byte3(round((1-t)*top + t*middle)), 0xFF);
                }
                for(int y: range(max(0, y0+splitY/2), min(size.y, y0+splitY))) {
                    float t = (float) (y- (y0 + splitY/2)) / (splitY/2);
                    byte4 verticalGradient (byte3((1-t)*middle + t*bottom), 0xFF); // mid -> dark
                    for(int x: range(size.x)) target(x,y) = verticalGradient;
                }
                // Draws lower flat part
                for(int y: range(max(0, y0+splitY), size.y)) for(int x: range(size.x)) target(x,y) = byte4(byte3(bottom), 0xFF);
                // Draws upper radial gradient (600x64)
                const int w = min(600, size.x), h = 64;
                for(int y: range(0, min(size.y, y0+h))) for(int x: range((size.x-w)/2, (size.x+w)/2)) {
                    const float cx = size.x/2, cy = y0+h/2;
                    float r = sqrt(sq((x-cx)/(w/2)) + sq((y-cy)/(h/2)));
                    const float r0 = 0./4, r1 = 2./4, r2 = 3./4, r3 = 4./4;
                    const float a0 = 255./255, a1 = 101./255, a2 = 37./255, a3 = 0./255;
                    /***/ if(r < r1) { float t = (r-r0) / (r1-r0); blend(target, x, y, radial, (1-t)*a0 + t*a1); }
                    else if(r < r2) { float t = (r-r1) / (r2-r1); blend(target, x, y, radial, (1-t)*a1 + t*a2); }
                    else if(r < r3) { float t = (r-r2) / (r3-r2); blend(target, x, y, radial, (1-t)*a2 + t*a3); }
                }
            }
#endif
            else error((int)background);
        }

        ::render(target, widget->graphics(target.size));

        send(Shm::PutImage{.window=id+Pixmap, .context=id+GraphicContext, .seg=id+Segment,
                           .totalW=uint16(target.stride), .totalH=uint16(target.height), .srcW=uint16(size.x), .srcH=uint16(size.y)});
        state=Copy;
        send(Present::Pixmap{.window=id+XWindow, .pixmap=id+Pixmap});
    }
}
