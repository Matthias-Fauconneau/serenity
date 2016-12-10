#pragma once
/// \file time.h Time and date operations
#include "data.h"
#include "thread.h"

/// A second in nanoseconds
const int64 second = 1000000000ull;

/// Returns Unix real-time in seconds
long currentTime();
/// Returns Unix real-time in nanoseconds
int64 realTime();
int64 threadCPUTime();

inline uint64 rdtsc() { uint32 lo, hi; asm volatile("rdtsc":"=a" (lo), "=d" (hi)::"memory"); return (((uint64)hi)<<32)|lo; }

struct Time {
 uint64 startTime=realTime(), stopTime=0;
 Time(bool start=false) : stopTime(start?0:startTime) {}
 void start() { if(stopTime) startTime=realTime()-(stopTime-startTime); stopTime=0; }
 void stop() { if(!stopTime) stopTime=realTime(); }
 uint64 reset() { stop(); uint64 t = stopTime-startTime; startTime=stopTime; stopTime=0; return t; }
 operator uint64() const { return ((stopTime?:realTime()) - startTime); }
 uint64 nanoseconds() const { return ((stopTime?:realTime()) - startTime); }
 uint64 microseconds() const { return ((stopTime?:realTime()) - startTime)/1000; }
 uint64 milliseconds() const { return ((stopTime?:realTime()) - startTime)/1000000; }
 float seconds() const { return ((stopTime?:realTime()) - startTime)/1000000000.; }
 explicit operator bool() const { return !stopTime; }
};

String strD(uint64 num, uint64 div);
template<> inline String str(const Time& t) { return str(t.seconds(), 1u)+'s'; }

inline bool operator<(float a, const Time& b) { return a < b.seconds(); }
inline bool operator<(double a, const Time& b) { return a < b.seconds(); }

struct Date {
    int year=-1, month=-1, day=-1, hours=-1, minutes=-1, seconds=-1;
    int weekDay=-1;
    void invariant(string s=""_) const;
    /// Default constructs an undetermined date
    Date(){}
    /// Constructs a calendar date (unspecified hour)
    Date(int monthDay, int month, int year, int weekDay=-1);
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
    explicit operator bool() const { return year>=0&&month>=0&&day>=0; }
};
/// Orders two dates
bool operator <(const Date& a, const Date& b);
/// Tests two dates
bool operator ==(const Date& a, const Date& b);

enum WeekDay { Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday };
constexpr string days[7] = {"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};
enum Month { January, February, March, April, May, June, July, August, September, October, November, December };
constexpr string months[12] = {"January","February","March","April","May","June","July","August","September","October","November","December"};
int daysInMonth(int month, int year);

/// Returns current date formatted using \a format String
String str(Date date, const string format="dddd, dd MMMM yyyy hh:mm:ss");

/// Parses a date from s
Date parseDate(TextData& s);
inline Date parseDate(const string s) { TextData t(s); return parseDate(t); }

struct Timer : Stream, Poll {
    Timer(const function<void()>& timeout={}, long sec=0, Thread& thread=mainThread);
    virtual ~Timer() {}
    void setAbsolute(uint64 nsec);
    void setRelative(long msec);
    function<void()> timeout;
    virtual void event();
};

/// Generates a sequence of uniformly distributed pseudo-random 64bit integers
struct Random {
 uint sz=1,sw=1;
 uint z=sz, w=sw;
 void seed() { z=sz=rdtsc(); w=sw=rdtsc(); } // and resets
 void reset() { z=sz; w=sw; }
 uint64 next() {
  z = 36969 * (z & 0xFFFF) + (z >> 16);
  w = 18000 * (w & 0xFFFF) + (w >> 16);
  return (z << 16) + w;
 }
 operator uint64() { return next(); }
 float operator()() { float f = float(next()&((1<<24)-1))*0x1p-24f; assert(f>=0 && f<1); return f; }
};
