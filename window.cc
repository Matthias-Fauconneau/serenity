#include "window.h"
#include "display.h"
#include "widget.h"
#include "linux.h"
struct input_event { long sec,usec; uint16 type,code; int32 value; };
enum { EV_SYN, EV_KEY, EV_REL, EV_ABS };

Widget* Window::focus=0;

Window::Window(Widget* widget, const string& name, const Image<byte4>& icon, int2 size) : widget(widget) {
    keyboard = open("/dev/input/event5", O_RDONLY|O_NONBLOCK, 0);
    registerPoll(i({keyboard, POLLIN}));
    setName(name); setIcon(icon); setSize(size);
}
Window::~Window() { close(keyboard); }
void Window::event(pollfd) {
    input_event e;
    /*while(read(touch, &e, sizeof(e)) > 0) {
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
            //float f = (2*float(v-min[i])/float(max[i]-min[i]))-1; //automatic calibration to [-1,1] //TODO -> [0,screen.size]
            //if(i==0) x=f; else if(i==1) y=f; else z=f; //TODO: -> touchEvent
        }*/
    while(read(keyboard, &e, sizeof(e)) > 0) {
        if(e.type == EV_KEY) {
            signal<>* shortcut = shortcuts.find(e.code);
            if(shortcut) shortcut->emit();
        }
    }
}

void Window::render() {
    if(visible) { widget->render(int2(0,0)); }
}

void Window::show() { visible=true; }
void Window::hide() { visible=false; }
void Window::setPosition(int2 position) {
    if(position.x<0) position.x=screen.x+position.x;
    if(position.y<0) position.y=screen.y+position.y;
    widget->position=position;
}
void Window::setSize(int2 size) {
    if(size.x<0||size.y<0) {
        int2 hint=widget->sizeHint(); assert(hint,hint);
        if(size.x<0) size.x=max(abs(hint.x),-size.x);
        if(size.y<0) size.y=max(abs(hint.y),-size.y);
    }
    if(size.x==0) size.x=screen.x;
    if(size.y==0) size.y=screen.y-16;
    assert(size);
    widget->size=size;
    widget->update();
}
void Window::setName(const string& /*name*/) { }
void Window::setIcon(const Image<byte4>& /*icon*/) {}
void Window::setType(int /*type*/) {}

void Window::setFocus(Widget* focus) {
    this->focus=focus;
}
bool Window::hasFocus() { return false; }

signal<>& Window::localShortcut(Key key) {
    return shortcuts.insert((uint16)key);
}
signal<>& Window::globalShortcut(Key key) {
    return shortcuts.insert((uint16)key);
}

string Window::getSelection() { return ""_; }
