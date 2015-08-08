#include "http.h"
#include "xml.h"
#include "location.h"
#include "time.h"

float parseAngle(TextData& s) {
    float angle = s.integer();
    s.match(":");
    float minutes = s.integer();
    s.match(":");
    float seconds = s.integer();
    if(seconds >= 60) return nanf;
    //assert_(seconds < 60, seconds, s.lineIndex);
    minutes += seconds / 60;
    assert_(minutes < 60);
    angle += minutes / 60;
    return angle; // in degrees
}

struct Peak {
    vec3 location;
    float prominence;
    vec2 col;
    String name;
    float distance;
    unique<LocationRequest> locationRequest = nullptr;
    void loadAddress(string address, vec3) {
        assert_(address);
        this->name = address+"*"; //copyRef(address);
    }
};
inline bool operator <(const Peak& a, const Peak& b) { return a.distance < b.distance; }
String str(const Peak& p) { return str(p.distance, p.location.z, p.prominence, p.name); }

struct Hike {
    unique<LocationRequest> request {nullptr};
    vec3 location;
    void loadLocation(string, vec3 location) { this->location=location; }

    array<Peak> peaks;

    Hike() {
        request.pointer = new LocationRequest(arguments()[1], {this, &Hike::loadLocation}); // FIXME: does not forward properly through unique<T>(Args...)

        TextData s (readFile("eu150"));
        s.skip("    LONG      LAT ELEV  DROP         SADDLE\n\n");
        while(s && !s.match("<<<<")) {
            Peak p;
            s.whileAny(" \n");
            s.match("[");
            p.location.y = parseAngle(s);
            if(isNaN(p.location.y)) { s.line(); continue; }
            s.whileAny(" ");
            p.location.x = parseAngle(s);
            s.whileAny(" ");
            p.location.z = s.integer();
            s.whileAny(" ");
            p.prominence = s.integer();
            if(p.prominence < 425/*232*/) { s.line(); continue; }
            s.whileAny(" ");
            if(s.match("c.")) {
                p.col.y = parseAngle(s);
                s.whileAny(" ");
                p.col.x = parseAngle(s);
                s.whileAny(" ");
            }
            string name = s.line();
            if(name) p.name = upperCase(name[0])+toLower(name.slice(1));
            p.name = replace(p.name, "oe", "ö");
            p.distance = distance(p.location, this->location).y/1000;
            peaks.insertSorted(move(p));
        }
        peakIndex = peaks.size-1;
        nextPeak();
    }

    int peakIndex;
    Timer timer {{this, &Hike::nextPeak}};
    size_t requestCount = 0;

    void nextPeak() {
        struct HTTP;
        extern array<unique<HTTP>> requests;
        while(peakIndex >= 0) {
            Peak& p = peaks[peakIndex--];
            if(p.distance > 85) continue;
            if(!p.name) {
                p.locationRequest.pointer = new LocationRequest(p.location, {&p, &Peak::loadAddress}); // FIXME: does not forward properly through unique<T>(Args...)
            }
            if(requests) {
                log(peakIndex+1, "/", peaks.size);
                requestCount++;
                if(requestCount > 512) { timer.setRelative(30*1000); requestCount=0; }
                else timer.setRelative(400);
                return;
            }
        }
        for(size_t i: reverse_range(peaks.size)) {
            Peak& p = peaks[i];
            if(p.distance > 85) continue;
            log(p);
        }
    }
} app;

