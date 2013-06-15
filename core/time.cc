#include "time.h"
#include "linux.h"
#include "data.h"
#include "string.h"

#include <time.h> //rt
#include <sys/timerfd.h>

long currentTime() { timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec; }
int64 realTime() { timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000000000ul+ts.tv_nsec; }

static bool leap(int year) { return (year%4==0)&&((year%100!=0)||(year%400==0)); }
int daysInMonth(int month, int year=0) {
    if(month==1 && leap(year)) { assert(year!=0); return 29; }
    static constexpr int daysPerMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return daysPerMonth[month];
}

void Date::invariant() const {
    //Date
    if(year!=-1) { assert(inRange(2012, year, 2099), year); }
    if(month!=-1) { assert(year!=-1); assert(inRange(0, month, 12)); }
    if(day!=-1) { assert(month!=-1); assert(inRange(0, day, daysInMonth(month,year)),day,daysInMonth(month,year));  }
    if(weekDay!=-1) {
        assert(inRange(0, weekDay, 7));
        if(year!=-1 && month!=-1 && day!=-1) {
            assert(weekDay==(Thursday+days())%7,weekDay,(Thursday+days())%7,str(*this));
        }
    }
    //Hour
    if(hours!=-1) { assert(inRange(0, hours, 24)); }
    if(minutes!=-1) { assert(inRange(0, minutes, 60)); assert(hours>=0); }
    if(seconds!=-1) { assert(inRange(0, seconds, 60)); assert(minutes>=0, hours, minutes, seconds); }
}
int Date::days() const {
    assert(year>=0 && month>=0, year, month, day, hours, minutes, seconds);
    int days=0; //days from Thursday, 1st January 1970
    for(int year=1970;year<this->year;year++) days+= leap(year)?366:365;
    for(int month=0;month<this->month;month++) days+=daysInMonth(month,year);
    return days+day;
}
Date::Date(int day, int month, int year, int weekDay) :year(year),month(month),day(day),weekDay(weekDay) {
    if(weekDay<0 && day>=0) this->weekDay=(Thursday+days())%7;
    invariant();
}
bool Date::summerTime() const { //FIXME: always European Summer Time
    assert(year>=0);
    int lastMarchSunday = 31-1-(Date(31-1,March,year).weekDay+1)%7; // after 01:00 UTC on the last Sunday in March
    int lastOctoberSunday = 31-1-(Date(31-1,October,year).weekDay+1)%7; // until 01:00 UTC on the last Sunday in October
    return    (month>March    || (month==March    && (day>lastMarchSunday    || (day==lastMarchSunday    && hours>=1))))
            && (month<October || (month==October && (day<lastOctoberSunday || (day==lastOctoberSunday && hours<  1))));
}
int Date::localTimeOffset() const {
    assert(year>=0);
    int offset = -8; //FIXME: hardcoded Central European Time (UTC+1) or Pacific Standard Time (UTC-8)
    if(summerTime()) offset += 1;
    return offset*60*60;
}
Date::Date(int64 time) {
    int64 utc unused = time;
    for(uint i unused: range(2)) { // First pass computes UTC date to determine DST, second pass computes local date
        seconds = time;
        minutes=seconds/60; seconds %= 60;
        hours=minutes/60; minutes %= 60;
        int days=hours/24; hours %= 24;
        weekDay = (Thursday+days)%7, month=0, year=1970;
        for(;;) { int nofDays = leap(year)?366:365; if(days>=nofDays) days-=nofDays, year++; else break; }
        for(;days>=daysInMonth(month,year);month++) days-=daysInMonth(month,year);
        day=days;
        time += localTimeOffset(); // localTimeOffset is only defined once we computed the UTC date
    }
    invariant();
    assert(long(*this)==utc);
}
Date::operator int64() const {
    invariant();
    return ((days()*24+(hours>=0?hours:0))*60+(minutes>=0?minutes:0))*60+(seconds>=0?seconds:0)-localTimeOffset();
}

bool operator <(const Date& a, const Date& b) {
    if(a.year!=-1 && b.year!=-1) { if(a.year<b.year) return true; else if(a.year>b.year) return false; }
    if(a.month!=-1 && b.month!=-1) { if(a.month<b.month) return true; else if(a.month>b.month) return false; }
    if(a.day!=-1 && b.day!=-1) { if(a.day<b.day) return true; else if(a.day>b.day) return false; }
    if(a.hours!=-1 && b.hours!=-1) { if(a.hours<b.hours) return true; else if(a.hours>b.hours) return false; }
    if(a.minutes!=-1 && b.minutes!=-1) { if(a.minutes<b.minutes) return true; else if(a.minutes>b.minutes) return false; }
    if(a.seconds!=-1 && b.seconds!=-1) { if(a.seconds<b.seconds) return true; else if(a.seconds>b.seconds) return false; }
    return false;
}
bool operator ==(const Date& a, const Date& b) { return a.seconds==b.seconds && a.minutes==b.minutes && a.hours==b.hours
            && a.day==b.day && a.month==b.month && a.year==b.year ; }

String str(Date date, const string& format) {
    String r;
    for(TextData s(format);s;) {
        /**/ if(s.match("ss"_)){ if(date.seconds>=0) r << dec(date.seconds,2); else s.until(' '); }
        else if(s.match("mm"_)){ if(date.minutes>=0) r << dec(date.minutes,2); else s.until(' '); }
        else if(s.match("hh"_)){ if(date.hours>=0) r << dec(date.hours,2); else s.until(' '); }
        else if(s.match("dddd"_)){ if(date.weekDay>=0) r << days[date.weekDay]; else s.until(' '); }
        else if(s.match("ddd"_)){ if(date.weekDay>=0) r << days[date.weekDay].slice(0,3); else s.until(' '); }
        else if(s.match("dd"_)){ if(date.day>=0) r << dec(date.day+1,2); else s.until(' '); }
        else if(s.match("MMMM"_)){ if(date.month>=0) r << months[date.month]; else s.until(' '); }
        else if(s.match("MMM"_)){ if(date.month>=0) r << months[date.month].slice(0,3); else s.until(' '); }
        else if(s.match("MM"_)){ if(date.month>=0) r << dec(date.month+1,2); else s.until(' '); }
        else if(s.match("yyyy"_)){ if(date.year>=0) r << dec(date.year); else s.until(' '); }
        else if(s.match("TZD"_)) r << "GMT"_; //FIXME
        else r << s.next();
    }
    if(endsWith(r,","_) || endsWith(r,":"_)) r.pop(); //prevent dangling separator when last valid part is week day or seconds
    return r;
}

Date parse(TextData& s) {
    Date date;
    {
        if(s.match("Today"_)) date=currentTime(), date.hours=date.minutes=date.seconds=-1;
        else for(int i=0;i<7;i++) if(s.match(days[i])) { date.weekDay=i; goto break_; }
        /*else*/ for(int i=0;i<7;i++) if(s.match(days[i].slice(0,3))) { date.weekDay=i; break; }
        break_:;
    }
    for(;;) {
        s.whileAny(" ,\t"_);
        for(int i=0;i<12;i++) if(s.match(months[i])) { date.month=i; goto continue2_; }
        /*else*/ for(int i=0;i<12;i++) if(s.match(months[i].slice(0,3))) { date.month=i; goto continue2_; }
        /*else */ if(s.available(1) && s.peek()>='0'&&s.peek()<='9') {
            int number = s.integer();
            if(s.match(":"_)) { date.hours=number; date.minutes=s.integer(); if(s.match(":"_)) date.seconds=s.integer(); }
            else if(s.match('h')) { date.hours=number; date.minutes= (s.available(2)>=2 && isInteger(s.peek(2)))? s.integer() : 0; }
            else if(date.day==-1) date.day=number-1;
            else if(date.month==-1) date.year=number-1;
            else if(date.year==-1) date.year=number;
            else error("Invalid date", s.buffer);
        } else break;
        continue2_:;
    }
    if(date.year<0 && (date.month>=0 || date.day>=0)) {
        Date now(currentTime());
        date.year=now.year;
        if(date.month>=0 && now.month-date.month > 6) date.year++; //implicit next year for dates otherwise >6 month in the past
        if(date.month<0) {
            date.month=Date(currentTime()).month;
            if(date.day<0) {
                date.day=Date(currentTime()).day;
            }
        }
    }
    date.invariant();
    return date;
}

Timer::Timer():Poll(timerfd_create(CLOCK_REALTIME,TFD_CLOEXEC)){registerPoll();}
Timer::~Timer(){ close(fd); }
void Timer::setAbsolute(long sec, long nsec) {
    timespec time[2]={{0,0},{sec,nsec}};
    timerfd_settime(fd,1,(const itimerspec*)time,0);
}
void Timer::setRelative(long msec) {
    timespec time[2]={{0,0},{msec/1000,(msec%1000)*1000000}};
    timerfd_settime(fd,0,(const itimerspec*)time,0);
}
