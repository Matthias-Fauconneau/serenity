#pragma once
#include "process.h"
#include "stream.h"

/// Returns Unix real-time in milliseconds
uint64 getRealTime();

/// Returns Unix real-time in seconds
long currentTime();

template<class T> bool inRange(T min, T x, T max) { return x>=min && x<=max; }
struct Date {
    int seconds=-1, minutes=-1, hours=-1, day=-1, month=-1, year=-1, weekDay=-1;
    void invariant() {
        //Hour
        if(seconds>=0) { assert(inRange(0, seconds, 59)); assert(minutes>=0); }
        if(minutes>=0) { assert(inRange(0, minutes, 59)); assert(hours>=0); }
        if(hours>=0) { assert(inRange(0, hours, 23)); }
        //Date
        if(day>=0) { assert(inRange(1, day, 31)); assert(month>=0); }
        if(month>=0) { assert(inRange(0, month, 11)); }
        if(weekDay>=0) {
            assert(inRange(0, weekDay, 6));
            //if(day>=0) TODO: check if valid
        }
    }
    Date(){}
    Date(int weekDay, int monthDay, int month):day(monthDay),month(month),weekDay(weekDay){invariant();}
    Date(int seconds, int minutes, int hours, int day, int month, int year, int weekDay) :
        seconds(seconds),minutes(minutes),hours(hours),day(day),month(month),year(year),weekDay(weekDay){ invariant(); }
};
inline bool operator ==(const Date& a, const Date& b) { return compare(a,b); }
inline bool operator !=(const Date& a, const Date& b) { return !compare(a,b); }
inline bool operator >(const Date& a, const Date& b) {
    if(a.year!=-1 && b.year!=-1) { if(a.year>b.year) return true; else if(a.year<b.year) return false; }
    if(a.month!=-1 && b.month!=-1) { if(a.month>b.month) return true; else if(a.month<b.month) return false; }
    if(a.day!=-1 && b.day!=-1) { if(a.day>b.day) return true; else if(a.day<b.day) return false; }
    if(a.hours!=-1 && b.hours!=-1) { if(a.hours>b.hours) return true; else if(a.hours<b.hours) return false; }
    if(a.minutes!=-1 && b.minutes!=-1) { if(a.minutes>b.minutes) return true; else if(a.minutes<b.minutes) return false; }
    if(a.seconds!=-1 && b.seconds!=-1) { if(a.seconds>b.seconds) return true; else if(a.seconds<b.seconds) return false; }
    return false;
}

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
