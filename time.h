#pragma once
#include "process.h"
#include "stream.h"

/// Returns Unix real-time in milliseconds
uint64 getRealTime();

/// Returns Unix real-time in seconds
long currentTime();

struct Date { int seconds, minutes, hours, day, month, year, weekDay; };
/// Convert unix timestamp to a calendar date
Date date(long time=currentTime());

/// Returns current date formatted using \a format string
string str(Date date, string&& format="dddd, dd MMMM yyyy hh:mm"_);

/// Parses a date from s
/// \note dates are parsed as dddd, dd mmmm yyyy
Date parse(TextBuffer& s);

struct Timer : Poll {
    int fd;
    Timer();
    void setAbsolute(int date);
    virtual void expired() =0;
    void event(pollfd);
};
