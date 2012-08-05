#pragma once
#include "time.h"
#include "interface.h"
//#include "window.h"

/// Returns events occuring on \a query date (-1=unspecified)
array<string> getEvents(Date query);

/// Month shows a week-aligned calendar month
struct Month : Grid<Text> {
    Date active;
    array<Date> dates;
    uint todayIndex;
    Month() : Grid(7,8) {}
    void setActive(Date active);
    void previousMonth();
    void nextMonth();
};

/// Calendar shows current date, month grid and events
struct Calendar : VBox {
    HList<Text> date { array<Text>{string( "<"_), string(""_), string(">"_)}};
    Month month;
    Text events;
    signal<> eventAlarm;
    Calendar();
    /// Pages calendar to previous month
    void previousMonth();
    /// Pages calendar to next month
    void nextMonth();
    /// Shows events matching date from month[\a index]
    void showEvents(uint index);
    /// Reset display to current month
    void update();
    /// Signals eventAlarm if any event will match 5 minutes after current time
    void checkAlarm();
};

struct Clock : Text, Timer {
    signal<> timeout;
    signal<> triggered;
    Clock(int size=16):Text(::str(date(),"hh:mm"_),size){ setAbsolute(currentTime()/60*60+60); }
    void expired() { text=::str(date(),"hh:mm"_); update(); setAbsolute(currentTime()+60); timeout(); }
    bool mouseEvent(int2, Event event, Key) override {
        if(event==Press) { triggered(); return true; }
        return false;
    }
};
