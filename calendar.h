#pragma once
#include "time.h"
#include "interface.h"

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
struct Calendar {
    HList<Text> date __(array<Text>{string( "<"_), string(""_), string(">"_)});
    Month month;
    Text events;
    signal<> eventAlarm;
    VBox layout __(&date, &month, &events);
    Calendar();
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
    signal<> triggered;
    Clock(int size):Text(::str(date(),"hh:mm"_),size){ setAbsolute(currentTime()/60*60+60); }
    void expired() { text=::str(date(),"hh:mm"_); setAbsolute(currentTime()+60); timeout(); }
    bool mouseEvent(int2, int2, Event event, Button) override {
        if(event==Press) { triggered(); return true; }
        return false;
    }
};
