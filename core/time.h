#pragma once
/// \file time.h Time and date operations
#include "data.h"
#include "function.h"
#include "string.h"
#include "math.h"

/// A second in nanoseconds
const int64 second = 1000000000ull;

/// Returns Unix real-time in seconds
long currentTime();
/// Returns Unix real-time in nanoseconds
int64 realTime();
int64 threadCPUTime();

#if 0
// FIXME: thread might switch core between cycle counter reads
#define readCycleCounter realTime
#else
#if __clang__
#define readCycleCounter  __builtin_readcyclecounter
#elif __INTEL_COMPILER
#define readCycleCounter __rdtsc
#else
#define readCycleCounter __builtin_ia32_rdtsc
#endif
#endif
/// Returns the number of cycles used to execute \a statements (low overhead)
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc {
 uint64 total=0, tsc=0;
 void reset() { total=0; tsc=0;}
 void start() { if(!tsc) tsc=readCycleCounter(); }
 void stop() { if(tsc) total+=readCycleCounter()-tsc; tsc=0; }
 uint64 cycleCount() const {return total + (tsc?readCycleCounter()-tsc:0); }
 operator uint64() const { return cycleCount(); }
 void operator =(int unused v) { assert(v == 0); reset(); }
};
inline String strD(const uint64 num, const uint64 div) {
 return div ? str(uint(round(100*float(num)/float(div))))+'%' : String();
}
inline String strD(const tsc& num, const tsc& div) { return strD(num.cycleCount(), div.cycleCount()); }
inline String str(const tsc& t) { return str(t.cycleCount()/1e9f)+"Gc"_; }

struct Time {
    uint64 startTime=realTime(), stopTime=0;
    Time(bool start=false) : stopTime(start?0:startTime) {}
    void start() { if(stopTime) startTime=realTime()-(stopTime-startTime); stopTime=0; }
    void stop() { if(!stopTime) stopTime=realTime(); }
    Time reset() { stop(); Time time=*this; startTime=stopTime; stopTime=0; return time; }
    uint64 nanoseconds() const { return ((stopTime?:realTime()) - startTime); }
    uint64 microseconds() const { return ((stopTime?:realTime()) - startTime)/1000; }
    uint64 milliseconds() const { return ((stopTime?:realTime()) - startTime)/1000000; }
    float seconds() const { return ((stopTime?:realTime()) - startTime)/1000000000.; }
    explicit operator bool() const { return startTime != stopTime; }
};
inline String str(const Time& t) { return str(t.seconds(), 1u)+'s'; }
inline bool operator<(float a, const Time& b) { return a < b.seconds(); }
inline String strD(const Time& num, const Time& div) {
 return strD(num.nanoseconds(), div.nanoseconds());
}

struct Date {
    int year=-1, month=-1, day=-1, hours=-1, minutes=-1, seconds=-1;
    int weekDay=-1;
    void invariant() const;
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
inline Date parseDate(string s) { TextData t(s); Date date = parseDate(t); return t ? Date() : date; }

#if 0
/// Generates a sequence of uniformly distributed pseudo-random 64bit integers
struct Random {
    uint sz,sw;
    uint z,w;
    Random(uint sz=1, uint sw=1) : sz(sz), sw(sw) { reset(); }
    void seed() { z=sz=readCycleCounter(); w=sw=readCycleCounter(); }
    void reset() { z=sz; w=sw; }
    uint64 next() {
     z = 36969 * (z & 0xFFFF) + (z >> 16);
     w = 18000 * (w & 0xFFFF) + (w >> 16);
     return (z << 16) + w;
    }
    operator uint64() { return next(); }
    float operator()() { float f = float(next()&((1<<24)-1))*0x1p-24f; assert(f>=0 && f<1); return f; }
};
#endif

#include "thread.h"
struct Timer : Stream, Poll {
 Timer(const function<void()>& timeout={}, long sec=0, Thread& thread=mainThread);
 virtual ~Timer() {}
 void setAbsolute(uint64 nsec);
 void setRelative(long msec);
 function<void()> timeout;
 virtual void event();
};
