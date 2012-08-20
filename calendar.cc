#include "calendar.h"
#include "file.h"
#include "map.h"

/// Returns events occuring on \a query date (-1=unspecified)
array<string> getEvents(Date query) {
    static int config = openFolder("config"_);
    array<string> events;
    if(!exists("events"_,config)) { /*warn("No events settings [config/events]");*/ return events; }

    TextStream s = readFile("events"_,config);
    map<string, array<Date>> exceptions; //Exceptions for recurring events
    while(s) { //first parse all exceptions (may occur after recurrence definitions)
        if(s.match("except "_)) {
            Date except=parse(s); s.skip(); string title=string(s.until('\n'));
            exceptions.insert(move(title),array<Date>__(except));
        } else s.until('\n');
    }
    s.index=0;

    Date until; //End date for recurring events
    while(s) {
        s.skip();
        if(s.match("#"_)) s.until('\n'); //comment
        else if(s.match("until "_)) { until=parse(s); } //apply to all following recurrence definitions
        else if(s.match("except "_)) s.until('\n'); //already parsed
        else {
            Date date = parse(s); s.skip();
            Date end=date; if(s.match("-"_)) { end=parse(s); s.skip(); }
            string title = string(s.until('\n'));
            if(query.day>=0) {
                if(date.day>=0) { if(date.day!=query.day) continue; }
                else if(query>until) continue;
            }
            if(query.month>=0) {
                if(date.month>=0) { if(date.month!=query.month) continue; }
                else if(query>until) continue;
            }
            if(query.year>=0) {
                if(date.year>=0) { if(date.year!=query.year) continue; }
                else if(query>until) continue;
            }
            if(query.hours>=0 && date.hours!=query.hours) continue;
            if(query.minutes>=0 && date.minutes!=query.minutes) continue;
            if(date.weekDay>=0 && date.weekDay!=query.weekDay) continue;
            if(exceptions.contains(title)) for(Date date: exceptions.at(title)) if(date.day==query.day && date.month==query.month) goto skip;
            events.insertSorted(string(str(date,"hh:mm"_)+(date!=end?string("-"_+str(end,"hh:mm"_)):string())+": "_+title));
            skip:;
        }
    }
    return events;
}

void Month::setActive(Date active) {
    clear(); dates.clear();
    todayIndex=-1; this->active=active;
    static const ref<byte> days[7] = {"Mo"_,"Tu"_,"We"_,"Th"_,"Fr"_,"Sa"_,"Su"_};
    const int nofDays[12] = { 31, !(active.year%4)&&(active.year%400)?29:28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    for(int i=0;i<7;i++) {
        *this<< string(days[i]);
        dates << Date(-1,-1,-1,-1,-1,-1,i);
    }
    int first = (35+active.weekDay+1-active.day)%7;
    for(int i=0;i<first;i++) { //previous month
        int previousMonth = (active.month+11)%12;
        int day = nofDays[previousMonth]-first+i+1;
        dates << Date(count()%7, day, previousMonth);
        *this<< Text(format(Italic)+dec(day,2),16,128);
    }
    Date today=::date();
    for(int i=1;i<=nofDays[active.month];i++) { //current month
        bool isToday = today.month==active.month && i==today.day;
        if(isToday) todayIndex=count();
        dates << Date(count()%7, i, active.month);
        *this<< string((isToday?format(Bold):string())+dec(i,2)); //current day
    }
    for(int i=1;count()<7*8;i++) { //next month
        dates << Date(count()%7, i, (active.month+1)%12);
        *this<< Text(format(Italic)+dec(i,2),16,128);
    }
    Selection::setActive(todayIndex);
}

void Month::previousMonth() { active.month--; if(active.month<0) active.year--, active.month=11; setActive(active); }
void Month::nextMonth() { active.month++; if(active.month>11) active.year++, active.month=0; setActive(active); }

Calendar::Calendar() {
    date.main=Linear::Spread;
    date[0].textClicked.connect(this, &Calendar::previousMonth);
    date[2].textClicked.connect(this, &Calendar::nextMonth);
    month.activeChanged.connect(this,&Calendar::showEvents);
    reset();
}
void Calendar::reset() {
    date[1].setText(::str(month.active,"MMMM yyyy"_) ); month.setActive(::date());
}
void Calendar::previousMonth() {
    month.previousMonth(); date[1].setText(::str(month.active,"MMMM yyyy"_) );
    events.setText(string());
}
void Calendar::nextMonth() {
    month.nextMonth(); date[1].setText(::str(month.active,"MMMM yyyy"_) );
    events.setText(string());
}

void Calendar::showEvents(uint index) {
    string text;
    Date date = month.dates[index];
    array<string> events = getEvents(date);
    if(events) {
        text << string(format(Bold)+(index==month.todayIndex?string("Today"_): ::str(date,"dddd, dd"))+format(Regular)+"\n"_);
        text << join(events,"\n"_)+"\n"_;
    }
    if(index==month.todayIndex) {
        Date date = month.dates[index+1];
        array<string> events = getEvents(date);
        if(events) {
            text << format(Bold)+"Tomorrow"_+format(Regular)+"\n"_;
            text << join(::getEvents(date),"\n"_);
        }
    }
    this->events.setText(move(text));
}

void Calendar::checkAlarm() { if(getEvents(::date(currentTime()+5*60))) eventAlarm(); }
