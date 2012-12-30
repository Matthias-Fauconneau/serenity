#include "time.h"
#include "linux.h"
#include "data.h"
#include "string.h"

#if NOLIBC
struct timespec { long tv_sec,tv_nsec; };
enum {CLOCK_REALTIME=0, CLOCK_THREAD_CPUTIME_ID=3};
enum {TFD_CLOEXEC = 02000000};
#else
#include <time.h>
#include <sys/timerfd.h>
#endif

long currentTime() { timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec; }
long realTime() { timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }
uint64 cpuTime() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec*1000000UL+ts.tv_nsec/1000; }

int daysInMonth(int month, int year=0) {
    if(month==1 && leap(year)) { assert(year!=0); return 29; }
    static constexpr int daysPerMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return daysPerMonth[month];
}

template<class T> bool inRange(T min, T x, T max) { return x>=min && x<max; }
debug(void Date::invariant() const {
    //Date
    if(year>=0) { assert(inRange(2012, year, 2099)); }
    if(month>=0) { assert(year>=0); assert(inRange(0, month, 12)); }
    if(day>=0) { assert(month>=0); assert(inRange(0, day, daysInMonth(month,year)),day,daysInMonth(month,year));  }
    if(weekDay>=0) {
        assert(inRange(0, weekDay, 7));
        if(year>=0 && month>=0 && day>=0) {
            assert(weekDay==(Thursday+days())%7,weekDay,(Thursday+days())%7,str(*this));
        }
    }
    //Hour
    if(hours>=0) { assert(inRange(0, hours, 24)); }
    if(minutes>=0) { assert(inRange(0, minutes, 60)); assert(hours>=0); }
    if(seconds>=0) { assert(inRange(0, seconds, 60)); assert(minutes>=0); }
})
int Date::days() const {
    assert(year>=0 && month>=0);
    int days=0; //days from Thursday, 1st January 1970
    for(int year=1970;year<this->year;year++) days+= leap(year)?366:365;
    for(int month=0;month<this->month;month++) days+=daysInMonth(month,year);
    return days+day;
}
Date::Date(int day, int month, int year, int weekDay) :year(year),month(month),day(day),weekDay(weekDay) {
    if(weekDay<0 && day>=0) this->weekDay=(Thursday+days())%7;
    debug(invariant();)
}
bool Date::summerTime() const { //FIXME: always European Summer Time
    int lastMarchSunday = 31-1-(Date(31-1,March,year).weekDay+1)%7; assert(year==2012 && lastMarchSunday==25-1); // after 01:00 UTC on the last Sunday in March
    int lastOctoberSunday = 31-1-(Date(31-1,October,year).weekDay+1)%7; // until 01:00 UTC on the last Sunday in October
    return    (month>March    || (month==March    && (day>lastMarchSunday    || (day==lastMarchSunday    && hours>=1))))
            && (month<October || (month==October && (day<lastOctoberSunday || (day==lastOctoberSunday && hours<  1))));
}
int Date::localTimeOffset() const {
    int offset = 1; //FIXME: always Central European Time (UTC+1)
    if(summerTime()) offset += 1;
    return offset*60;
}
Date::Date(long time) {
    // UTC time and date
    seconds = time;
    minutes=seconds/60; seconds %= 60;
    hours=minutes/60; minutes %= 60;
    int days=hours/24; hours %= 24;
    weekDay = (Thursday+days)%7, month=0, year=1970;
    for(;;) { int nofDays = leap(year)?366:365; if(days>=nofDays) days-=nofDays, year++; else break; }
    for(;days>=daysInMonth(month,year);month++) days-=daysInMonth(month,year);
    day=days;

    // Local time and date
    minutes+=localTimeOffset();
    if(minutes>=60) {
        hours += minutes/60; minutes %= 60;
        if(hours>=24) {
            hours-=24; weekDay=(weekDay+1)%7; day++;
            if(day>=daysInMonth(month,year)) {
                day=0, month++;
                if(month>=12) month=0, year++;
            }
        }
    }

    debug(invariant();)
    assert(long(*this)==time);
}
Date::operator long() const {
    debug(invariant();)
    return ((days()*24+(hours>=0?hours:0))*60+(minutes>=0?minutes:0)-localTimeOffset())*60+(seconds>=0?seconds:0);
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

string str(Date date, const ref<byte>& format) {
    string r;
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
    if(endsWith(r,","_)) r.pop(); //prevent dangling comma when last valid part is week day
    return r;
}

Date parse(TextData& s) {
    Date date;
    if(s.match("Today"_)) date=currentTime(), date.hours=date.minutes=date.seconds=-1;
    else for(int i=0;i<7;i++) if(s.match(str(days[i]))) { date.weekDay=i; break; }
    {
        s.whileAny(" ,\t"_);
        int number = s.integer();
        if(number>=0) {
            if(s.match(":"_)) date.hours=number, date.minutes= (s.available(2)>=2 && isInteger(s.peek(2)))? s.integer() : -1;
            else if(s.match('h')) date.hours=number, date.minutes= (s.available(2)>=2 && isInteger(s.peek(2)))? s.integer() : 0;
            else if(date.day==-1) date.day=number-1;
        }
    }
    {
        s.whileAny(" ,\t"_);
        for(int i=0;i<12;i++) if(s.match(str(months[i]))) date.month=i;
    }
    {
        s.whileAny(" ,\t"_);
        int number = s.integer();
        if(number>=0) {
            if(s.match(":"_)) date.hours=number, date.minutes= (s.available(2)>=2 && isInteger(s.peek(2)))? s.integer() : -1;
            else if(s.match('h')) date.hours=number, date.minutes= (s.available(2)>=2 && isInteger(s.peek(2)))? s.integer() : 0;
            else if(date.day==-1) date.day=number-1;
        }
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
    debug(date.invariant();) //assert(date.hours>=0 || date.year>=0,date,s.buffer);
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
