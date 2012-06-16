#include "time.h"
#include "stream.h"
#include <sys/timerfd.h>

long realTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }
long currentTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec; }
long cpuTime() { struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec*1000000+ts.tv_nsec/1000; }

template<class T> inline bool inRange(T min, T x, T max) { return x>=min && x<=max; }
void Date::invariant() {
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
bool operator >(const Date& a, const Date& b) {
    if(a.year!=-1 && b.year!=-1) { if(a.year>b.year) return true; else if(a.year<b.year) return false; }
    if(a.month!=-1 && b.month!=-1) { if(a.month>b.month) return true; else if(a.month<b.month) return false; }
    if(a.day!=-1 && b.day!=-1) { if(a.day>b.day) return true; else if(a.day<b.day) return false; }
    if(a.hours!=-1 && b.hours!=-1) { if(a.hours>b.hours) return true; else if(a.hours<b.hours) return false; }
    if(a.minutes!=-1 && b.minutes!=-1) { if(a.minutes>b.minutes) return true; else if(a.minutes<b.minutes) return false; }
    if(a.seconds!=-1 && b.seconds!=-1) { if(a.seconds>b.seconds) return true; else if(a.seconds<b.seconds) return false; }
    return false;
}
bool operator ==(const Date& a, const Date& b) { return compare((const byte*)&a,(const byte*)&b,sizeof(Date)); }


Date date(long time) {
    tm t; localtime_r(&time,&t);
    return Date{ t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, 1900+t.tm_year, (t.tm_wday+6)%7/*0=Monday*/ };
}

static const string days[7] = {"Monday"_,"Tuesday"_,"Wednesday"_,"Thursday"_,"Friday"_,"Saturday"_,"Sunday"_};
static const string months[12] = {"January"_,"February"_,"March"_,"April"_,"May"_,"June"_,"July"_,"August"_,"September"_,"October"_,"November"_,"December"_};

string str(Date date, string&& format) {
    TextBuffer s(move(format));
    string r;
    while(s) {
        /**/ if(s.match("ss"_)){ if(date.seconds>=0)  r << dec(date.seconds,2); else s.until(" "_); }
        else if(s.match("mm"_)){ if(date.minutes>=0)  r << dec(date.minutes,2); else s.until(" "_); }
        else if(s.match("hh"_)){ if(date.hours>=0)  r << dec(date.hours,2); else s.until(" "_); }
        else if(s.match("dddd"_)){ if(date.weekDay>=0) r << days[date.weekDay]; else s.until(" "_); }
        else if(s.match("ddd"_)){ if(date.weekDay>=0) r << slice(days[date.weekDay],0,3); else s.until(" "_); }
        else if(s.match("dd"_)){ if(date.day>=0) r << dec(date.day,2); else s.until(" "_); }
        else if(s.match("MMMM"_)){ if(date.month>=0)  r << months[date.month]; else s.until(" "_); }
        else if(s.match("MMM"_)){ if(date.month>=0)  r << slice(months[date.month],0,3); else s.until(" "_); }
        else if(s.match("MM"_)){ if(date.month>=0)  r << dec(date.month+1,2); else s.until(" "_); }
        else if(s.match("yyyy"_)){ if(date.year>=0) r << dec(date.year); else s.until(" "_); }
        else if(s.match("TZD"_)) r << "GMT"_; //FIXME
        else r << s.read(1);
    }
    r = simplify(trim(r));
    if(endsWith(r,","_)) r.removeLast();
    return simplify(trim(r));
}

Date parse(TextBuffer& s) {
    Date date;
    for(int i=0;i<7;i++) if(s.match(days[i])) { date.weekDay=i; break; }

    s.whileAny(" ,\t"_);
    {
        int number = s.number();
        if(number<0) return date;
        if(s.match(":"_)) date.hours=number, date.minutes=s.number();
        else date.day = number;
    }

    s.whileAny(" ,\t"_);
    for(int i=0;i<12;i++) if(s.match(months[i])) date.month=i;

    s.whileAny(" ,\t"_);
    {
        int number = s.number();
        if(number<0) return date;
        if(s.match(":"_)) date.hours=number, date.minutes=s.number();
    }
    date.invariant();
    return date;
}

Timer::Timer(){ fd=timerfd_create(CLOCK_REALTIME,0); registerPoll({fd, POLLIN}); }
void Timer::setAbsolute(int date) { itimerspec timer{timespec{0,0},timespec{date,0}}; timerfd_settime(fd,TFD_TIMER_ABSTIME,&timer,0); }
void Timer::event(pollfd) { expired(); }
