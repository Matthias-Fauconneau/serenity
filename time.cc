#include "time.h"
#include "linux.h"
#include "stream.h"
#include "string.h"
#include "debug.h"

long currentTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.sec; }
long cpuTime() { struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.sec*1000000+ts.nsec/1000; }

int daysInMonth(int month, int year) {
    if(month==1 && leap(year)) return 29;
    static constexpr int daysPerMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return daysPerMonth[month];
}

template<class T> bool inRange(T min, T x, T max) { return x>=min && x<=max; }
void Date::invariant() {
    //Date
    if(year>=0) { assert_(inRange(2012, year, 2012)); }
    if(month>=0) { assert_(year>=0); assert_(inRange(0, month, 11)); }
    if(day>=0) { assert_(month>=0); assert_(inRange(1, day, 31));  }
    if(weekDay>=0) {
        assert_(inRange(0, weekDay, 6));
        if(year>=0 && month>=0 && day>=0) {
            int setWeekDay = weekDay;
            setDay(day);
            assert(weekDay==setWeekDay,weekDay,setWeekDay);
        }
    }
    //Hour
    if(hours>=0) { assert_(inRange(0, hours, 23)); }
    if(minutes>=0) { assert_(inRange(0, minutes, 59)); assert_(hours>=0); }
    if(seconds>=0) { assert_(inRange(0, seconds, 59)); assert_(minutes>=0); }
    assert_(year>=0 || hours>=0);
}
void Date::setDay(int monthDay) {
    assert(year>=0 && month>=0);
    int days=3; //days from Thursday, 1st January 1970
    for(int year=1970;year<this->year;year++) days+= leap(year)?366:365;
    for(int month=0;month<this->month;month++) days+=daysInMonth(month,year);
    days+=monthDay-1;
    weekDay = days%7;
}
bool operator >(const Date& a, const Date& b) {
    if(a.year!=-1 && b.year!=-1) { if(a.year>b.year) return true; else if(a.year<b.year) return false; }
    if(a.month!=-1 && b.month!=-1) { if(a.month>b.month) return true; else if(a.month<b.month) return false; }
    if(a.day!=-1 && b.day!=-1) { if(a.day>b.day) return true; else if(a.day<b.day) return false; }
    if(a.hours!=-1 && b.hours!=-1) { if(a.hours>b.hours) return true; else if(a.hours<b.hours) return false; }
    if(a.minutes!=-1 && b.minutes!=-1) { if(a.minutes>b.minutes) return true; else if(a.minutes<b.minutes) return false; }
    if(a.seconds!=-1 && b.seconds!=-1) { if(a.seconds>b.seconds) return true; else if(a.seconds<b.seconds) return false; }
    return false;
}
bool operator ==(const Date& a, const Date& b) { return a.seconds==b.seconds && a.minutes==b.minutes && a.hours==b.hours
            && a.day==b.day && a.month==b.month && a.year==b.year ; }

Date date(long time) {
    int seconds = time, minutes=seconds/60, hours=minutes/60+2, days=hours/24+1, weekDay = (days+2)%7, month=0, year=1970;
    for(;;) { int nofDays = leap(year)?366:365; if(days>=nofDays) days-=nofDays, year++; else break; }
    for(;days>daysInMonth(month,year);month++) days-=daysInMonth(month,year);
    return Date( seconds%60, minutes%60, hours%24, days, month, year, weekDay ); //GMT+1 and DST
}

string str(Date date, const ref<byte>& format) {
    date.invariant();
    string r;
    for(TextStream s(format);s;) {
        /**/ if(s.match("ss"_)){ if(date.seconds>=0)  r << dec(date.seconds,2); else s.until(' '); }
        else if(s.match("mm"_)){ if(date.minutes>=0)  r << dec(date.minutes,2); else s.until(' '); }
        else if(s.match("hh"_)){ if(date.hours>=0)  r << dec(date.hours,2); else s.until(' '); }
        else if(s.match("dddd"_)){ if(date.weekDay>=0) r << days[date.weekDay]; else s.until(' '); }
        else if(s.match("ddd"_)){ if(date.weekDay>=0) r << days[date.weekDay].slice(0,3); else s.until(' '); }
        else if(s.match("dd"_)){ if(date.day>=0) r << dec(date.day+1,2); else s.until(' '); }
        else if(s.match("MMMM"_)){ if(date.month>=0)  r << months[date.month]; else s.until(' '); }
        else if(s.match("MMM"_)){ if(date.month>=0)  r << months[date.month].slice(0,3); else s.until(' '); }
        else if(s.match("MM"_)){ if(date.month>=0)  r << dec(date.month+1,2); else s.until(' '); }
        else if(s.match("yyyy"_)){ if(date.year>=0) r << dec(date.year); else s.until(' '); }
        else if(s.match("TZD"_)) r << "GMT"_; //FIXME
        else r << s.next();
    }
    if(endsWith(r,","_)) r.pop(); //prevent dangling comma when last valid part is week day
    assert(r,date.year,date.month,date.day,date.hours,date.minutes,date.seconds,date.weekDay,format);
    return r;
}

Date parse(TextStream& s) {
    Date date;

    for(int i=0;i<7;i++) if(s.match(str(days[i]))) { date.weekDay=i; break; }

    s.whileAny(" ,\t"_);
    {
        int number = s.number();
        if(number<0) return date;
        if(s.match(":"_)) date.hours=number, date.minutes=s.number();
        else date.day = number;
    }

    s.whileAny(" ,\t"_);
    for(int i=0;i<12;i++) if(s.match(str(months[i]))) date.month=i;

    s.whileAny(" ,\t"_);
    {
        int number = s.number();
        if(number<0) return date;
        if(s.match(":"_)) date.hours=number, date.minutes=s.number();
    }

    if(date.year==-1 && date.month>=0) date.year=::date().year;
    date.invariant();
    return date;
}

Timer::Timer(){ fd=timerfd_create(CLOCK_REALTIME,0); registerPoll(); }
void Timer::setAbsolute(uint date) { timespec time[2]={{0,0},{date,0}}; timerfd_settime(fd,1,time,0); }
void Timer::event() { expired(); }
