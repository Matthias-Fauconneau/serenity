#pragma once
#include "time.h"
#include "interface.h"
#include "window.h"

/// Returns events occuring on \a query date (-1=unspecified)
array<string> getEvents(Date query);

struct Month : Grid<Text> {
    Date active;
    array<Date> dates;
    int todayIndex;
    Month() : Grid(7,8) {}
    void setActive(Date active);
    void previousMonth();
    void nextMonth();
};

struct Calendar : VBox {
    HList<Text> date = { "<"_, ""_, ">"_ };
    Month month;
    Text events;
    signal<> eventAlarm;
    Calendar();
    void previousMonth();
    void nextMonth();
    void activeChanged(int index);
    void update();
    void checkAlarm();
};

template<class T> struct Popup : T {
    Window window{this,""_,Image(),int2(300,300)};
    Popup() {
        window.setType(Atom("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"));
        window.localShortcut("Leave"_).connect(&window,&Window::hide);
    }
    Popup(T&& t) : T(move(t)) {
        window.setType(Atom("_NET_WM_WINDOW_TYPE_DROPDOWN_MENU"));
        window.localShortcut("Leave"_).connect(&window,&Window::hide);
    }
    void toggle() { if(window.visible) window.hide(); else { T::update(); window.show(); } }
};

struct Clock : Text, Timer {
    signal<> timeout;
    signal<> triggered;
    Clock(int size=16):Text(str(date(),"hh:mm"_),size){ setAbsolute(currentTime()/60*60+60); }
    void expired() { text=str(date(),"hh:mm"_); update(); setAbsolute(currentTime()+60); timeout.emit(); }
    bool mouseEvent(int2, Event event, Button button) override {
        if(event==Press && button==LeftButton) { triggered.emit(); return true; }
        return false;
    }
};
