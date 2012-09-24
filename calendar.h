#pragma once
/// \file calendar.h Calendar, Events, Clock
#include "time.h"
#include "interface.h"

struct Event { Date date,end; string title; };
inline string str(const Event& e) { return str(e.date,"hh:mm"_)+(e.date!=e.end?string("-"_+str(e.end,"hh:mm"_)):string())+": "_+e.title; }
inline bool operator <(const Event& a, const Event& b) { return a.date<b.date; }

/// Returns events occuring on \a query date
array<Event> getEvents(Date query=Date(-1,-1,-1,-1));

/// Shows a week-aligned calendar month
struct Calendar : GridSelection<Text> {
    Date active;
    array<Date> dates;
    uint todayIndex;
    Calendar() : GridSelection(7,8,16) {}
    void setActive(Date active);
    void previousMonth();
    void nextMonth();
};

/// Shows current date, month grid and events
struct Events : VBox {
    HList<Text> date;// __(array<Text>{string( "<"_), string(""_), string(">"_)});
    Calendar month;
    Text events;
    signal<> eventAlarm;
    Events();
    /// Resets calendar to today
    void reset();
    /// Shows previous month
    void previousMonth();
    /// Shows next month
    void nextMonth();
    /// Shows events matching date from month[\a index]
    void showEvents(uint index);
    /// Signals eventAlarm if any event will match 5 minutes after current time
    void checkAlarm();
};

struct Clock : Text, Timer {
    signal<> timeout;
    signal<> pressed;
    Clock(int size=16):Text(::str(date(),"hh:mm"_),size){ setAbsolute(currentTime()/60*60+60); }
    void event() { setText(::str(date(),"hh:mm"_)); setAbsolute(currentTime()/60*60+60); timeout(); }
    bool mouseEvent(int2, int2, Event event, MouseButton) override {
        if(event==Press) { pressed(); return true; }
        return false;
    }
};
