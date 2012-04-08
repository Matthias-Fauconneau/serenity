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

struct Calendar {
    HList<Text> date = { "<"_, ""_, ">"_ };
    Month month;
    Text events;
    Menu menu { &date, &month, &space, &events, &space };
    Window window{&menu,""_,Image(),int2(300,300)};
    Calendar();
    void previousMonth();
    void nextMonth();
    void activeChanged(int index);
    void show();
    void checkAlarm();
};
