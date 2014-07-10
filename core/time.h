#pragma once
/// \file time.h Time and date operations
#include "data.h"
#include "thread.h"
#include "function.h"
#include "string.h"
#include "math.h"

/// Returns Unix real-time in seconds
long currentTime();
/// Returns Unix real-time in nanoseconds
int64 realTime();

#if __x86_64__ || __i386__
#define USE_TSC 1
inline uint64 rdtsc() { uint32 lo, hi; asm volatile("rdtsc":"=a" (lo), "=d" (hi)::"memory"); return (((uint64)hi)<<32)|lo; }
/// Returns the number of cycles used to execute \a statements (low overhead)
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc { uint64 total=0, tsc=0; void reset(){total=0;tsc=0;} void start(){assert(!tsc); tsc=rdtsc();} void stop(){assert(tsc); total+=rdtsc()-tsc; tsc=0;} operator uint64() const {assert_(!tsc); return total + (tsc?rdtsc()-tsc:0);} };
#endif

/// Logs the time spent executing a scope
struct Time {
    uint64 startTime=0, stopTime=0;
    Time() { start(); }
    void start() { startTime = realTime() - (stopTime ? stopTime-startTime : 0); stopTime=0; }
    void stop() { if(!stopTime) stopTime = realTime(); }
    uint64 elapsed() const { return startTime ? ((stopTime?:realTime()) - startTime) : 0; }
    operator uint64() const { return elapsed(); }
    float toFloat() const { return elapsed()/1000000000.f; }
};
inline String str(const Time& t) { return str(t.toFloat())+"s"_; }
inline String operator/(const Time& a, const Time& b) { return b ? dec(round(100*a.toFloat()/b.toFloat()))+"%"_ : String(); }

struct Date {
    int year=-1, month=-1, day=-1, hours=-1, minutes=-1, seconds=-1;
    int weekDay=-1;
    void invariant() const;
    /// Default constructs an undetermined date
    Date(){}
    /// Constructs a calendar date (specified hour)
    Date(int monthDay, int month, int year, int hours, int minutes, int seconds);
    /// Constructs a calendar date (unspecified hour)
    Date(int monthDay, int month, int year/*, int weekDay=-1*/) : Date(monthDay, month, year, -1, -1, -1) {}
    /// Converts UNIX \a timestamp (in seconds) to a local time calendar date
    Date(int64 time);
    /// Returns days from Thursday, 1st January 1970
    int days() const;
    /// Returns whether this date is in daylight saving time
    bool summerTime() const;
    /// Returns the local time offset from UTC in seconds (time zone + DST)
    int localTimeOffset(int64 utc) const;
    /// Converts the date to Unix time (in seconds)
    operator int64() const;
};
/// Orders two dates
bool operator <(const Date& a, const Date& b);
/// Tests two dates
bool operator ==(const Date& a, const Date& b);

enum WeekDay { Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday };
constexpr string days[7] = {"Monday"_,"Tuesday"_,"Wednesday"_,"Thursday"_,"Friday"_,"Saturday"_,"Sunday"_};
enum Month { January, February, March, April, May, June, July, August, September, October, November, December };
constexpr string months[12] = {"January"_,"February"_,"March"_,"April"_,"May"_,"June"_,"July"_,"August"_,"September"_,"October"_,"November"_,"December"_};
int daysInMonth(int month, int year);

/// Returns current date formatted using \a format String
String str(Date date, const string& format="dddd, dd MMMM yyyy hh:mm:ss"_);

/// Parses a date from s
/// \note dates are parsed as dddd, dd mmmm yyyy
Date parseDate(TextData& s);
inline Date parseDate(const string& s) { TextData t(s); return parseDate(t); }

struct Timer : Poll {
    Timer(long msec, function<void()> timeout, Thread& thread=mainThread);
    virtual ~Timer();
    void setAbsolute(long sec, long nsec=0);
    void setRelative(long msec);
    const function<void()> timeout;
    virtual void event() { timeout(); }
};
