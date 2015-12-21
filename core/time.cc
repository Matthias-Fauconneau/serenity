#include "time.h"
#include "data.h"
#include "string.h"
#include <unistd.h>
#include <sys/timerfd.h>
#include <time.h> //rt

// \file core.cc
generic bool inRange(T min, T x, T max) { return !(x<min) && x<max; }

long currentTime() { timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec; }
int64 realTime() { timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000000000ull+ts.tv_nsec; }
int64 threadCPUTime() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec*1000000000ull+ts.tv_nsec; }

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
 /*if(weekDay!=-1) {
        assert(inRange(0, weekDay, 7));
        if(year!=-1 && month!=-1 && day!=-1) {
            assert(weekDay==(Thursday+days())%7,weekDay,(Thursday+days())%7,str(*this));
        }
    }*/
 //Hour
 if(hours!=-1) { assert(inRange(0, hours, 24)); }
 if(minutes!=-1) { assert(inRange(0, minutes, 60)); assert(hours>=0); }
 if(seconds!=-1) { assert(inRange(0, seconds, 60)); assert(minutes>=0, hours, minutes, seconds); }
}
int Date::days() const {
 assert(year>=0 && month>=0, year, month, day, hours, minutes, seconds);
 int days=0; //days from Thursday, 1st January 1970
 for(int year: range(1970, this->year)) days+= leap(year)?366:365;
 for(int month: range(this->month)) days+=daysInMonth(month, year);
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
/*int Date::localTimeOffset(int64 unused utc) const {
 assert(year>=0);
 // Parses /etc/localtime to get local time zone offset (without DST)
 static buffer<byte> localtime = readFile("/etc/localtime");
 BinaryData s (localtime, true);
 s.advance(20);
 uint unused gmtCount = s.read(), unused stdCount = s.read(), unused leapCount = s.read(), transitionCount = s.read(), infoCount = s.read(), nameCount = s.read();
 ref<int> transitionTimes = s.read<int>(transitionCount);
 uint i = 0; for(; i < transitionCount; i++) if(utc < (int)big32(transitionTimes[i])) break; i--;
 ref<uint8> transitionIndices = s.read<uint8>(transitionCount);
 uint index = transitionIndices[i];
 struct ttinfo { int32 gmtOffset; uint8 isDST, nameIndex; } packed;
 ref<ttinfo> infos = s.read<ttinfo>(infoCount);
 return big32(infos[index].gmtOffset);
}*/
Date::Date(int64 time) {
 //int64 utc = time;
 for(uint i unused: range(2)) { // First pass computes UTC date to determine DST, second pass computes local date
  seconds = time;
  minutes=seconds/60; seconds %= 60;
  hours=minutes/60; minutes %= 60;
  int days=hours/24; hours %= 24;
  weekDay = (Thursday+days)%7, month=0, year=1970;
  for(;;) { int nofDays = leap(year)?366:365; if(days>=nofDays) days-=nofDays, year++; else break; }
  for(;days>=daysInMonth(month,year);month++) days-=daysInMonth(month,year);
  day=days;
  //time += localTimeOffset(utc); // localTimeOffset is only defined once we computed the UTC date
 }
 invariant();
 //assert(long(*this)==utc);
}
Date::operator int64() const {
 invariant();
 int64 local = ((days()*24+(hours>=0?hours:0))*60+(minutes>=0?minutes:0))*60+(seconds>=0?seconds:0);
 return local; //-localTimeOffset(local);
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

String str(Date date, const string format) {
 array<char> r;
 for(TextData s(format);s;) {
  /**/ if(s.match("ss")){ if(date.seconds>=0) r.append( str(date.seconds,2u,'0') ); else s.until(' '); }
  else if(s.match("mm")){ if(date.minutes>=0) r.append( str(date.minutes,2u,'0') ); else s.until(' '); }
  else if(s.match("hh")){ if(date.hours>=0) r.append( str(date.hours,2u,'0') ); else s.until(' '); }
  else if(s.match("dddd")){ if(date.weekDay>=0) r.append( days[date.weekDay] ); else s.until(' '); }
  else if(s.match("ddd")){ if(date.weekDay>=0) r.append( days[date.weekDay].slice(0,3) ); else s.until(' '); }
  else if(s.match("dd")){ if(date.day>=0) r.append( str(date.day+1,2u,'0') ); else s.until(' '); }
  else if(s.match("MMMM")){ if(date.month>=0) r.append( months[date.month] ); else s.until(' '); }
  else if(s.match("MMM")){ if(date.month>=0) r.append( months[date.month].slice(0,3) ); else s.until(' '); }
  else if(s.match("MM")){ if(date.month>=0) r.append( str(date.month+1,2u,'0') ); else s.until(' '); }
  else if(s.match("yyyy")){ if(date.year>=0) r.append( str(date.year) ); else s.until(' '); }
  else if(s.match("TZD")) r.append("GMT"); //FIXME
  else r.append( s.next() );
 }
 if(endsWith(r,",") || endsWith(r,":")) r.pop(); //prevent dangling separator when last valid part is week day or seconds
 return move(r);
}

Date parseDate(TextData& s) {
 Date date;
 if(s.match("Today")) date=currentTime(), date.hours=date.minutes=date.seconds=-1;
 else for(int i=0;i<7;i++) if(s.match(days[i]) || s.match(days[i].slice(0,3))) { date.weekDay=i; break; }
 for(;;) {
  s.whileAny(" ,\t");
  for(int i=0;i<12;i++) {
   if(s.match(months[i]) || s.match(months[i].slice(0,3))) { date.month=i; goto continue2_; }
  } /*else */ if(s.available(1) && s.peek()>='0'&&s.peek()<='9') {
   int number = s.integer();
   if(s.match(":")) { date.hours=number; date.minutes=s.integer(); if(s.match(":")) date.seconds=s.integer(); }
   else if(s.match('h')) { date.hours=number; date.minutes= (s.available(2)>=2 && isInteger(s.peek(2)))? s.integer() : 0; }
   else if(s.match("/")) {
    date.month=number; date.day=s.integer();
    if(s.match("/")) date.year=s.integer();
   }
   else if(s.match("-")) {
    date.year=number; date.month=s.integer()-1;
    if(s.match("-")) date.day=s.integer()-1;
    s.match('T');
   }
   //else if(date.day==-1) date.day=number-1;
   //else if(date.month==-1) date.month=number-1;
   //else if(date.year==-1) date.year=number;
   else error("Invalid date", s);
  } else break;
  /**/continue2_:;
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

Timer::Timer(const function<void()>& timeout, long sec, Thread& thread)
 : Stream(timerfd_create(CLOCK_REALTIME,TFD_CLOEXEC)), Poll(Stream::fd, POLLIN, thread), timeout(timeout) {
 if(sec) setAbsolute(realTime()+sec*1000000000ull);
}
void Timer::event() { read<uint64>(); timeout(); }
void Timer::setAbsolute(uint64 nsec) {
 timespec time[2]={{0,0},{long(nsec/1000000000ull),long(nsec%1000000000ull)}};
 timerfd_settime(Stream::fd, 1, (const itimerspec*)time,0);
}
void Timer::setRelative(long msec) {
 timespec time[2]={{0,0},{msec/1000,(msec%1000)*1000000}};
 timerfd_settime(Stream::fd, 0, (const itimerspec*)time,0);
}
