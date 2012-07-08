#include "time.h"
#include "linux.h"
#include "stream.h"

enum { CLOCK_REALTIME=0, CLOCK_THREAD_CPUTIME_ID=3 };
//long realTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }
long currentTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.sec; }
//long cpuTime() { struct timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec*1000000+ts.tv_nsec/1000; }

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
bool operator ==(const Date& a, const Date& b) { return a.seconds==b.seconds && a.minutes==b.minutes && a.hours==b.hours
            && a.day==b.day && a.month==b.month && a.year==b.year ; }

static const char* days[7] = {"Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"};
static const char* months[12] = {"January","February","March","April","May","June","July","August","September","October","November","December"};

Date date(long time) {
    int seconds = time, minutes=seconds/60, hours=minutes/60+2, days=hours/24, weekDay = (days+3)%7, month=0, year=1970;
    bool leap;
    for(;;) { leap=((year%400)||(!(year%100)&&year%4)); int nofDays = leap?366:365; if(days>nofDays) days-=nofDays, year++; else break; }
    const int daysPerMonth[12] = {31, leap?29:28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for(;days>daysPerMonth[month];month++) days-=daysPerMonth[month]; month++;
    return Date{ seconds%60, minutes%60, hours%24, days, month, year, weekDay }; //GMT+1 and DST
}

string str(Date date, string&& format) {
    TextStream s(move(format));
    string r;
    while(s) {
        /**/ if(s.match("ss"_)){ if(date.seconds>=0)  r << dec(date.seconds,2); else s.until(" "_); }
        else if(s.match("mm"_)){ if(date.minutes>=0)  r << dec(date.minutes,2); else s.until(" "_); }
        else if(s.match("hh"_)){ if(date.hours>=0)  r << dec(date.hours,2); else s.until(" "_); }
        else if(s.match("dddd"_)){ if(date.weekDay>=0) r << str(days[date.weekDay]); else s.until(" "_); }
        else if(s.match("ddd"_)){ if(date.weekDay>=0) r << slice(str(days[date.weekDay]),0,3); else s.until(" "_); }
        else if(s.match("dd"_)){ if(date.day>=0) r << dec(date.day,2); else s.until(" "_); }
        else if(s.match("MMMM"_)){ if(date.month>=0)  r << str(months[date.month]); else s.until(" "_); }
        else if(s.match("MMM"_)){ if(date.month>=0)  r << slice(str(months[date.month]),0,3); else s.until(" "_); }
        else if(s.match("MM"_)){ if(date.month>=0)  r << dec(date.month+1,2); else s.until(" "_); }
        else if(s.match("yyyy"_)){ if(date.year>=0) r << dec(date.year); else s.until(" "_); }
        else if(s.match("TZD"_)) r << "GMT"_; //FIXME
        else r << s.read(1);
    }
    r = simplify(trim(r));
    if(endsWith(r,","_)) r.removeLast();
    return simplify(trim(r));
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
    date.invariant();
    return date;
}

Timer::Timer(){ fd=timerfd_create(CLOCK_REALTIME,0); registerPoll({fd, POLLIN}); }
void Timer::setAbsolute(int date) { timespec time[2]={{0,0},{date,0}}; timerfd_settime(fd,1,time,0); }
void Timer::event(pollfd) { expired(); }
