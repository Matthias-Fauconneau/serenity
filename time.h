#pragma once
#include "data.h"
#include "process.h"

#if __x86_64__
inline uint64 rdtsc() { uint32 lo, hi; asm volatile("rdtsc" : "=a" (lo), "=d" (hi)); return (uint64)hi << 32 | lo; }
/// Returns the number of cycles used to execute \a statements
#define cycles( statements ) ({ uint64 start=rdtsc(); statements; rdtsc()-start; })
struct tsc { uint64 start=rdtsc(); operator uint64(){ return rdtsc()-start; } };
#endif

/// Returns Unix real-time in seconds
long currentTime();
/// Returns Unix real-time in milliseconds
long realTime();
/// Returns current thread CPU time in microseconds
long cpuTime();

struct Date {
    int year=-1, month=-1, day=-1, hours=-1, minutes=-1, seconds=-1;
    int weekDay=-1;
    debug(void invariant();)
    Date(){}
    Date(int weekDay, int monthDay, int month, int year):year(year),month(month),day(monthDay),weekDay(weekDay) {debug(invariant();)}
    Date(int seconds, int minutes, int hours, int day, int month, int year, int weekDay) :
        year(year),month(month),day(day),hours(hours),minutes(minutes),seconds(seconds),weekDay(weekDay) {debug(invariant();)}
    /// Sets month day and matching week day
    void setDay(int monthDay);
};
bool operator <(const Date& a, const Date& b);
bool operator ==(const Date& a, const Date& b);

enum WeekDay { Monday, Tuesday, Wednesday, Thursday, Friday, Saturday, Sunday };
constexpr ref<byte> days[7] = {"Monday"_,"Tuesday"_,"Wednesday"_,"Thursday"_,"Friday"_,"Saturday"_,"Sunday"_};
enum Month { January, February, March, April, May, June, July, August, September, October, November, December };
constexpr ref<byte> months[12] = {"January"_,"February"_,"March"_,"April"_,"May"_,"June"_,"July"_,"August"_,"September"_,"October"_,"November"_,"December"_};
constexpr bool leap(int year) { return (year%4==0)&&((year%100!=0)||(year%400==0)); }
int daysInMonth(int month, int year);

/// Convert unix timestamp to a calendar date
Date date(long time=currentTime());

/// Returns current date formatted using \a format string
string str(Date date, const ref<byte>& format="dddd, dd MMMM yyyy hh:mm"_);

/// Parses a date from s
/// \note dates are parsed as dddd, dd mmmm yyyy
Date parse(TextData& s);

struct Timer : Poll {
    Timer();
    ~Timer();
    void setAbsolute(uint date);
    virtual void event() =0;
};
