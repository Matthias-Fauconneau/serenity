#pragma once
#include "process.h"
#include "stream.h"

/// Returns Unix real-time in seconds
long currentTime();
/// Returns Unix real-time in milliseconds
long realTime();
/// Returns CPU timestamp in microseconds
long cpuTime();

struct Date {
    int seconds=-1, minutes=-1, hours=-1, day=-1, month=-1, year=-1, weekDay=-1;
    void invariant();
    Date(){}
    Date(int weekDay, int monthDay, int month):day(monthDay),month(month),weekDay(weekDay){invariant();}
    Date(int seconds, int minutes, int hours, int day, int month, int year, int weekDay) :
        seconds(seconds),minutes(minutes),hours(hours),day(day),month(month),year(year),weekDay(weekDay){ invariant(); }
};
bool operator >(const Date& a, const Date& b);
bool operator ==(const Date& a, const Date& b);

/// Convert unix timestamp to a calendar date
Date date(long time=currentTime());

/// Returns current date formatted using \a format string
string str(Date date, string&& format="dddd, dd MMMM yyyy hh:mm"_);

/// Parses a date from s
/// \note dates are parsed as dddd, dd mmmm yyyy
Date parse(TextStream& s);

struct Timer : Poll {
    int fd;
    Timer();
    void setAbsolute(int date);
    virtual void expired() =0;
    void event(pollfd);
};
