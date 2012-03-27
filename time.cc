#include "time.h"
#include "stream.h"
#include <poll.h>
#include <sys/timerfd.h>

uint64 getRealTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }

long currentTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec; }

Date date(long time) {
    tm t; localtime_r(&time,&t);
    return { t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, 1900+t.tm_year, (t.tm_wday+6)%7/*0=Monday*/ };
}

static const string days[7] = {"Monday"_,"Tuesday"_,"Wednesday"_,"Thursday"_,"Friday"_,"Saturday"_,"Sunday"_};
static const string months[12] = {"January"_,"February"_,"March"_,"April"_,"May"_,"June"_,"July"_,"August"_,"September"_,"October"_,"November"_,"December"_};

string str(Date date, string&& format) {
    TextBuffer s(move(format));
    string r;
    while(s) {
        /**/ if(s.match("ss"_)){ if(date.seconds>=0)  r << dec(date.seconds,2); }
        else if(s.match("mm"_)){ if(date.minutes>=0)  r << dec(date.minutes,2); }
        else if(s.match("hh"_)){ if(date.hours>=0)  r << dec(date.hours,2); }
        else if(s.match("dddd"_)){ if(date.weekDay>=0) r << days[date.weekDay]; }
        else if(s.match("ddd"_)){ if(date.weekDay>=0) r << slice(days[date.weekDay],0,3); }
        else if(s.match("dd"_)){ if(date.day>=0) r << dec(date.day,2); }
        else if(s.match("MMMM"_)){ if(date.month>=0)  r << months[date.month]; }
        else if(s.match("MMM"_)){ if(date.month>=0)  r << slice(months[date.month],0,3); }
        else if(s.match("MM"_)){ if(date.month>=0)  r << dec(date.month+1,2); }
        else if(s.match("yyyy"_)){ if(date.year>=0) r << dec(date.year); }
        else if(s.match("TZD"_)) r << "GMT"_; //FIXME
        else r << s.read<byte>();
    }
    return simplify(trim(r));
}

Date parse(TextBuffer& s) {
    Date date = {-1,-1,-1,-1,-1,-1,-1};
    for(int i=0;i<7;i++) if(s.match(days[i])) { date.weekDay=i; break; }
    s.whileAny(" ,\t"_);
    int number = s.number();
    if(number<0) return date;
    if(s.match(":"_)) date.hours=number, date.minutes=s.number();
    else date.day = number;
    s.whileAny(" ,\t"_);
    for(int i=0;i<12;i++) if(s.match(months[i])) date.month=i;
    return date;
}

Timer::Timer(){ fd=timerfd_create(CLOCK_REALTIME,0); registerPoll({fd, POLLIN}); }
void Timer::setAbsolute(int date) { itimerspec timer{timespec{0,0},timespec{date,0}}; timerfd_settime(fd,TFD_TIMER_ABSTIME,&timer,0); }
void Timer::event(pollfd) { expired(); }
