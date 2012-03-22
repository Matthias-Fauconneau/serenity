#pragma once
#include "process.h"

/// Returns Unix real-time in milliseconds
uint64 getRealTime();

/// Returns Unix real-time in seconds
long currentTime();

struct Date { int seconds, minutes, hours, day, month, year, weekDay; };
/// Convert unix timestamp to a calendar date
Date date(long time=currentTime());

/// Returns current date formatted using \a format string
string date(string&& format, Date date=::date());

struct Timer : Poll {
    int fd;
    Timer();
    void setAbsolute(int date);
    virtual void expired() =0;
    void event(pollfd);
};
