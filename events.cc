#include "process.h"
#include "file.h"
#include "time.h"

struct Events : Application {
    void start(array<string>&&) {
        events(date());
    }
    /// Returns events occuring on date (-1=unspecified)
    array<string> events(Date date) {
        array<string> events;
        TextBuffer s(readFile(".config/events"_,home()));
        Date until; //End date for recurring events
        while(s) {
            if(s.match("until "_)) { until=parse(s); log("until"_,until); }
            else {
                Date date = parse(s);
                string title = s.until("\n"_);
                log(date,"'"_+title+"'"_);
            }
        }
        return events;
    }
} events;
