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
    while(s) {
        /**/ if(s.match("ss"_))  r << str((int64)date.tm_sec,10,2);
        else if(s.match("mm"_))  r << str((int64)date.tm_min,10,2);
        else if(s.match("hh"_))  r << str((int64)date.tm_hour,10,2);
        else if(s.match("d"_))   r << str((int64)date.tm_mday);
        else if(s.match("MM"_))  r << str((int64)date.tm_mon,10,2);
        else if(s.match("yyyy"_))r << str((int64)date.tm_year);
        else r << s.read<byte>();
    }
    return r;
}

Timer::Timer(){ fd=timerfd_create(CLOCK_REALTIME,0); registerPoll({fd, POLLIN}); }
void Timer::setAbsolute(int date) { itimerspec timer{timespec{0,0},timespec{date,0}}; timerfd_settime(fd,TFD_TIMER_ABSTIME,&timer,0); }
void Timer::event(pollfd) { expired(); }
