#include "time.h"
#include "stream.h"
#include <poll.h>
#include <sys/timerfd.h>

int getRealTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }

int getUnixTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec; }

Date currentDate() { tm t; time_t ts=getUnixTime(); localtime_r(&ts,&t); return { t.tm_sec, t.tm_min, t.tm_hour, t.tm_mday, t.tm_mon, 1900+t.tm_year, (t.tm_wday+6)%7/*0=Monday*/ }; }

string date(string&& format) {
    TextBuffer s(move(format));
    Date date = currentDate();
    string r;
    static const string days[7] = {"Monday"_,"Tuesday"_,"Wednesday"_,"Thursday"_,"Friday"_,"Saturday"_,"Sunday"_};
    static const string months[12] = {"January"_,"February"_,"March"_,"April"_,"May"_,"June"_,"July"_,"August"_,"September"_,"October"_,"November"_,"December"_};
    while(s) {
        /**/ if(s.match("ss"_))  r << dec(date.seconds,2);
        else if(s.match("mm"_))  r << dec(date.minutes,2);
        else if(s.match("hh"_))  r << dec(date.hours,2);
        else if(s.match("dddd"_))   r << days[date.weekDay];
        else if(s.match("dd"_))   r << dec(date.day,2);
        else if(s.match("MMMM"_))  r << months[date.month];
        else if(s.match("MM"_))  r << dec(date.month+1,2);
        else if(s.match("yyyy"_))r << dec(date.year);
        else r << s.read<byte>();
    }
    return r;
}

Timer::Timer(){ fd=timerfd_create(CLOCK_REALTIME,0); registerPoll({fd, POLLIN}); }
void Timer::setAbsolute(int date) { itimerspec timer{timespec{0,0},timespec{date,0}}; timerfd_settime(fd,TFD_TIMER_ABSTIME,&timer,0); }
void Timer::event(pollfd) { expired(); }
