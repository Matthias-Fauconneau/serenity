#pragma once
/// \file time.h Time and date operations
#include "data.h"
#include "process.h"

/// Returns Unix real-time in seconds
long currentTime();
/// Returns Unix real-time in milliseconds
long realTime();
/// Returns current thread CPU time in microseconds
uint64 cpuTime();

#if __x86_64__
inline uint64 rdtsc() { uint32 lo, hi; asm volatile("rdtsc":"=a" (lo), "=d" (hi)::"memory"); return (((uint64)hi)<<32)|lo; }
/// Returns the number of cycles used to execute \a statements (low overhead)
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc { uint64 start=rdtsc(); operator uint64(){ return rdtsc()-start; } };
#endif

struct Date {
    int year=-1, month=-1, day=-1, hours=-1, minutes=-1, seconds=-1;
    int weekDay=-1;
    debug(void invariant() const;)
    /// Default constructs an undetermined date
    Date(){}
    /// Constructs a calendar date (unspecified hour)
    Date(int monthDay, int month, int year, int weekDay=-1);
    /// Converts UNIX \a timestamp (in secondes) to a local time calendar date
    Date(long timestamp);
    /// Returns days from Thursday, 1st January 1970
    int days() const;
    /// Returns whether this date is in daylight saving time
    bool summerTime() const;
    /// Returns the local time offset from UTC in minutes (time zone + DST)
    int localTimeOffset() const;
    /// Converts the date to Unix time (in seconds)
    operator long() const;
};
/// Orders two dates
bool operator <(const Date& a, const Date& b);
/// Tests two dates
bool operator ==(const Date& a, const Date& b);

enum WeekDay { Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday };
constexpr ref<byte> days[7] = {"Monday"_,"Tuesday"_,"Wednesday"_,"Thursday"_,"Friday"_,"Saturday"_,"Sunday"_};
enum Month { January, February, March, April, May, June, July, August, September, October, November, December };
constexpr ref<byte> months[12] = {"January"_,"February"_,"March"_,"April"_,"May"_,"June"_,"July"_,"August"_,"September"_,"October"_,"November"_,"December"_};
constexpr bool leap(int year) { return (year%4==0)&&((year%100!=0)||(year%400==0)); }
int daysInMonth(int month, int year);

/// Returns current date formatted using \a format string
string str(Date date, const ref<byte>& format="dddd, dd MMMM yyyy hh:mm"_);

/// Parses a date from s
/// \note dates are parsed as dddd, dd mmmm yyyy
Date parse(TextData& s);
inline Date parse(const ref<byte>& s) { TextData t(s); return parse(t); }

struct Timer : Poll {
    Timer();
    ~Timer();
    void setAbsolute(uint date);
    virtual void event() =0;
};

/// Generates a sequence of uniformly distributed pseudo-random 64bit integers
struct Random {
    uint64 sz,sw;
    uint64 z,w;
    Random() { seed(); reset(); }
    void seed() { sz=rdtsc(); sw=rdtsc(); }
    void reset() { z=sz; w=sw; }
    uint64 next() {
        z = 36969 * (z & 0xFFFF) + (z >> 16);
        w = 18000 * (w & 0xFFFF) + (w >> 16);
        return (z << 16) + w;
    }
    uint64 operator()() { return next(); }
};
