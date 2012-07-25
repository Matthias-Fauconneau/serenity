#include "window.h"
#include "display.h"
#include "widget.h"
#include "file.h"
#include "stream.h"
#include "linux.h"
#include "array.cc"

struct input_event { long sec,usec; uint16 type,code; int32 value; };
enum { EV_SYN, EV_KEY, EV_REL, EV_ABS };
enum { VT_ACTIVATE = 0x5606 };
ICON(cursor);

Window::Window(Widget* widget, int2 size, string&& title, Image<byte4>&& icon) : widget(widget), title(move(title)), icon(move(icon)) {
    if(!display) openDisplay();
#if __arm__
    touch = open("/dev/input/event0", O_RDONLY|O_NONBLOCK, 0); registerPoll(i({touch, POLLIN}));
    buttons = open("/dev/input/event4", O_RDONLY|O_NONBLOCK, 0); registerPoll(i({buttons, POLLIN}));
    keyboard = open("/dev/input/event5", O_RDONLY|O_NONBLOCK, 0);
    mouse = open("/dev/input/event6", O_RDONLY|O_NONBLOCK, 0);
#else
    keyboard = open("/dev/input/event0", O_RDONLY|O_NONBLOCK, 0);
    mouse = open("/dev/input/event1", O_RDONLY|O_NONBLOCK, 0);
#endif
    setSize(size);
    vt = open("/dev/console", O_RDWR, 0);
    wm = socket(PF_INET,SOCK_DGRAM,0);
    //int one=1; setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (void *)&one, sizeof(one));
    sockaddr addr = {PF_INET,'W'|('M'<<8),127u|(255<<8)|(255<<16)|(255<<24)};
    int unused e = check( connect(wm,&addr,sizeof(addr)) );
}
Window::~Window() { hide(); close(wm); close(touch); close(buttons); close(keyboard); close(mouse); close(vt); }
void Window::event(pollfd poll) {
    if(poll.fd==wm) for(window w;read(wm, &w, sizeof(w)) > 0;) {
        if(w.length>sizeof(window)) read(wm, w.length-sizeof(window));
        log(w.id);
        //if(w.id==uint(-1)) { w.id=windows.size(); windows<<w; } //FIXME: broadcast loop?
        //if(w.id>=windows.size()) resize(windows,w.id+1);
        if(w.id==id) { // A window took this id (FIXME: broadcast loop?)
            id++; windows<<windows.last(); // Take next one (will cascade until last id (so that a new window receives all others))
            emit(true,true); //Remap title and icon to new id
        }
        if(!w.size) { // A window is hiding
            if(w.id==id-1) { // Fill hole which would break cascade

            }
        }
        if(w.id>=windows.size()) resize(windows,w.id);
        update();
    }
    if(poll.fd==touch) for(input_event e;read(touch, &e, sizeof(e)) > 0;) {
         if (e.type == EV_ABS) {
            int i = e.code;
            int v = e.value;
#if CALIBRATE
            const int M=1<<10; static int min[3]={M,M,M}, max[3]={-M,-M,-M}; // calibrate without a priori
#else
            static int min[3]={337,-80,352}, max[3]={506,63,486}; // touchbook calibration
#endif
            if(v<=min[i]) min[i]=v-1;
            if(v>=max[i]) max[i]=v+1;
            cursor[i] = widget->size[i]*(v-min[i])/uint(max[i]-min[i]);
         }
         if(e.type==EV_SYN) {
             update();
             render(); //TODO: HW cursor | SW repair
         }
    }
    if(poll.fd==mouse) for(input_event e;read(mouse, &e, sizeof(e)) > 0;) {
        if(e.type==EV_REL) { int i = e.code; assert(i<2); cursor[i]+=e.value; cursor[i]=clip(0,cursor[i],display[i]); } //TODO: acceleration
        if(e.type==EV_SYN) {
            update();
            render(); //TODO: HW cursor | SW repair
        }
        if(e.type == EV_KEY) {
            assert(widget);
            widget->mouseEvent(cursor,Press,(Button)e.code);
        }
    }
    if(poll.fd==keyboard) for(input_event e;read(keyboard, &e, sizeof(e)) > 0;) {
        if(e.type == EV_KEY) {
            signal<>* shortcut = shortcuts.find(e.code);
            if(shortcut) (*shortcut)();
        }
    }
}

void Window::update() {
    uint active=-1;
    if(!windows) { assert(id==0); resize(windows,1); }
    windows[id] = window i({sizeof(window),id,cursor,widget->position,widget->size});
    //for(int i=windows.size()-1;i>=0;i--) { const window& w=windows[i];
    for(const window& w: windows) {
        if((w.position+Rect(w.size)).contains(w.cursor)) { active=w.id; break; }
    }
    if(this->active!=id && active==id) {
        registerPoll(i({keyboard, POLLIN}));
        registerPoll(i({mouse, POLLIN}));
        if(this->active<windows.size()) cursor=windows[this->active].cursor; //use cursor from last active
        else cursor=widget->position+widget->size/2; //root window startup with cursor in middle
    }
    if(this->active==id && active!=id) {
            if(!globalShortcuts) {
                if(buttons) unregisterPoll(buttons);
                unregisterPoll(keyboard);
            }
            if(touch) unregisterPoll(touch);
            unregisterPoll(mouse);
            if(hideOnLeave) hide();
            else emit(); //update cursor for new active before leaving
    }
    this->active=active;
    //TODO: compute visibility
}
void Window::emit(bool emitTitle, bool emitIcon) {
    if(!shown) return;
    window w i({sizeof(window),id,widget->position,widget->size,cursor});
    if(w.id==uint(-1)) { w.id=windows.size(); windows<<w; }
    if(w.id==uint(-2)) w.id=-1; // take top level id
    if(emitTitle) {
        array<byte> state;
        state << (uint8)title.size() << title;
        if(emitIcon) state << (uint8)icon.width << (uint8)icon.height << cast<byte>(ref<byte4>(icon));
        w.length += state.size();
        write(wm,array<byte>(raw(w)+state));
    } else write(wm,raw(w));
}

void Window::render() {
    assert(shown);
    widget->render(int2(0,0));
    if(active==id) blit(cursor,cursorIcon());
}

void Window::show() {
    if(shown) return; shown=true;
    registerPoll(i({wm, POLLIN}));
    widget->size = display;
    widget->update();
    emit();
    //ioctl(vt, VT_ACTIVATE, (void*)6); //switch from X
    update();
    render();
}
void Window::hide() {
    if(!shown) return; shown=false;
    unregisterPoll(wm);
    id=-2;
    widget->size=int2(0,0);
    emit();
    //ioctl(vt, VT_ACTIVATE, (void*)7); //switch to X (TODO: only if all hidden)
}

void Window::setPosition(int2 position) {
    if(position.x<0) position.x=display.x+position.x;
    if(position.y<0) position.y=display.y+position.y;
    widget->position=position;
    emit();
}
void Window::setSize(int2 size) {
    if(!widget) return;
    if(size.x<0||size.y<0) {
        int2 hint=widget->sizeHint(); assert(hint,hint);
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=display.x;
    if(size.y==0) size.y=display.y-16;
    assert(size);
    widget->size=size;
    widget->update();
    if(int(id)>=0) emit();
}
void Window::setTitle(string&& title) { this->title=move(title); emit(true); }
void Window::setIcon(Image<byte4>&& icon) { this->icon=move(icon); emit(true,true); }

signal<>& Window::localShortcut(Key key) { return shortcuts.insert((uint16)key); }
signal<>& Window::globalShortcut(Key key) { globalShortcuts=true; return shortcuts.insert((uint16)key); }

string Window::getSelection() { return string(); }
