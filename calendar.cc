#include "calendar.h"
#include "file.h"
#include "map.h"

array<Event> getEvents(Date query) {
    static Folder config = openFolder(string(getenv("HOME"_)+"/.config"_),root(),true);
    array<Event> events;
    if(!existsFile("events"_,config)) {warn("No events settings [$HOME/.config/events]"); return events; }
    string file = readFile("events"_,config);

    map<string, array<Date>> exceptions; //Exceptions for recurring events
    for(TextStream s(file);s;) { //first parse all exceptions (may occur after recurrence definitions)
        if(s.match("except "_)) {
            Date except=parse(s); s.skip(); string title=string(s.until('\n'));
            exceptions[move(title)] << except;
        } else s.until('\n');
    }

    Date until; //End date for recurring events
    for(TextStream s(file);s.skip(), s;) {
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
            if(query.weekDay>=0 && date.weekDay>=0 && date.weekDay!=query.weekDay) continue;
            if(exceptions.contains(title)) for(Date date: exceptions.at(title)) if(date.day==query.day && date.month==query.month) goto skip;
            events.insertSorted(Event __(date,end,move(title)));
            skip:;
        }
    }
    return events;
}

void Calendar::setActive(Date active) {
    clear(); dates.clear();
    todayIndex=-1; this->active=active;
    static const ref<byte> days[7] = {"Mon"_,"Tue"_,"Wed"_,"Thu"_,"Fri"_,"Sat"_,"Sun"_};
    for(int i=0;i<7;i++) {
        *this<< string(days[i]);
        dates << Date(i,-1,active.month,active.year);
    }
    Date date=active; date.setDay(1); int first=date.weekDay;
    for(int i=0;i<first-1;i++) { //previous month
        int previousMonth = (active.month+11)%12;
        int day = daysInMonth(previousMonth,active.year-(active.month==0))-first+i+1;
        dates << Date(count()%7, day, previousMonth, active.year-(active.month==0));
        *this<< Text(format(Italic)+dec(day+1,2),16,128);
    }
    Date today=::date();
    for(int i=0;i<daysInMonth(active.month,active.year);i++) { //current month
        bool isToday = today.month==active.month && i==today.day;
        if(isToday) todayIndex=count();
        dates << Date(count()%7, i, active.month, active.year);
        *this<< string((isToday?format(Bold):string())+dec(i+1,2)); //current day
    }
    for(int i=0;count()<7*8;i++) { //next month
        dates << Date(count()%7, i, (active.month+1)%12, active.year+(active.month==11));
        *this<< Text(format(Italic)+dec(i+1,2),16,128);
    }
    Selection::setActive(todayIndex);
}

void Calendar::previousMonth() { active.month--; if(active.month<0) active.year--, active.month=11; setActive(active); }
void Calendar::nextMonth() { active.month++; if(active.month>11) active.year++, active.month=0; setActive(active); }

Events::Events()/*:VBox(__(&date, &month, &events))*/ {
    *this<<&date<<&month<<&events; date<<string( "<"_)<<string(""_)<<string(">"_);
    date.main=Linear::Spread;
    date[0].textClicked.connect(this, &Events::previousMonth);
    date[2].textClicked.connect(this, &Events::nextMonth);
    month.activeChanged.connect(this,&Events::showEvents);
    events.minSize=int2(256,256);
    reset();
}
void Events::reset() { month.setActive(::date());  date[1].setText(::str(month.active,"MMMM yyyy"_) ); }
void Events::previousMonth() { month.previousMonth(); date[1].setText(::str(month.active,"MMMM yyyy"_) ); events.setText(string()); }
void Events::nextMonth() { month.nextMonth(); date[1].setText(::str(month.active,"MMMM yyyy"_) ); events.setText(string()); }

void Events::showEvents(uint index) {
    string text;
    Date date = month.dates[index];
    array< ::Event> events = getEvents(date);
    if(events) {
        text << string(format(Bold)+(index==month.todayIndex?string("Today"_): ::str(date,"dddd, dd"_))+format(Regular)+"\n"_);
        text << str(events,'\n')+"\n"_;
    }
    if(index==month.todayIndex) {
        Date date = month.dates[index+1];
        array< ::Event> events = getEvents(date);
        if(events) {
            text << format(Bold)+"Tomorrow"_+format(Regular)+"\n"_;
            text << str(::getEvents(date),'\n');
        }
    }
    this->events.setText(move(text));
}

void Events::checkAlarm() { if(getEvents(::date(currentTime()+5*60))) eventAlarm(); }
