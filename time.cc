#include "time.h"
#include "stream.h"
#include <poll.h>
#include <sys/timerfd.h>

int getRealTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }

int getUnixTime() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec; }

string date(string&& format) {
    Stream s(move(format));
    timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    tm date; localtime_r(&ts.tv_sec,&date);
    string r;
    static const string days[7] = {"Monday"_,"Tuesday"_,"Wednesday"_,"Thursday"_,"Friday"_,"Saturday"_,"Sunday"_};
    static const string months[12] = {"January"_,"February"_,"March"_,"April"_,"May"_,"June"_,"July"_,"August"_,"September"_,"October"_,"November"_,"December"_};
    while(s) {
        /**/ if(s.match("ss"_))  r << dec(date.tm_sec,2);
        else if(s.match("mm"_))  r << dec(date.tm_min,2);
        else if(s.match("hh"_))  r << dec(date.tm_hour,2);
        else if(s.match("dddd"_))   r << days[date.tm_wday-1];
        else if(s.match("dd"_))   r << dec(date.tm_mday,2);
        else if(s.match("MMMM"_))  r << months[date.tm_mon];
        else if(s.match("MM"_))  r << dec(date.tm_mon+1,2);
        else if(s.match("yyyy"_))r << dec(1900+date.tm_year);
        else r << s.read<byte>();
    }
    return r;
}

Timer::Timer(){ fd=timerfd_create(CLOCK_REALTIME,0); registerPoll({fd, POLLIN}); }
void Timer::setAbsolute(int date) { itimerspec timer{timespec{0,0},timespec{date,0}}; timerfd_settime(fd,TFD_TIMER_ABSTIME,&timer,0); }
void Timer::event(pollfd) { expired(); }
