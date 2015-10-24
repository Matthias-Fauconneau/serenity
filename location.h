#pragma once
#include "http.h"
#include "xml.h"
#include "time.h"
#include <unistd.h>

float distance(vec2 A, vec2 B) {
    float R = 6378137;
    float φ1 = PI/180* A.x;
    float φ2 = PI/180* B.x;
    float Δφ = φ1-φ2;
    float Δλ = PI/180* (B.y-A.y);
    float a = sin(Δφ/2) * sin(Δφ/2) + cos(φ1) * cos(φ2) * sin(Δλ/2) * sin(Δλ/2);
    float c = 2 * atan(sqrt(a), sqrt(1-a));
    return R * c;
}

static string key = arguments()[0];

vec2 location(string address) {
    Map data = getURL(URL("https://maps.googleapis.com/maps/api/geocode/xml?key="_+key+"&address="+replace(address," ","+")));
    Element root = parseXML(data);
    string status = root("GeocodeResponse")("status").content;
    if(status == "ZERO_RESULTS"_) return 0;
    assert_(status=="OK", status);
    const Element& location = root("GeocodeResponse")("result")("geometry")("location");
    return vec2(parseDecimal(location("lat").content), parseDecimal(location("lng").content));
}

String nearby(vec2 location, string types) {
    auto data = getURL(URL("https://maps.googleapis.com/maps/api/place/nearbysearch/xml?key="_+key+
                           "&location="+str(location.x)+","+str(location.y)+"&types="+types+
                           "&rankby=distance"));
    Element root = parseXML(data);
    for(const Element& result : root("PlaceSearchResponse").children) {
        if(result.name!="result") continue;
        String destination = result("name").content+", "+result("vicinity").content;
        vec2 dstLocation = ::location(destination);
        if(distance(location, dstLocation) < 22838) return destination;
        // else wrong result in Nearby Search
    }
    error(location, types);
}

static int queryLimit = 1;

uint duration(string origin, string destination, int64 unused time=0) {
    if(origin == destination) return 0;
    String url = "https://maps.googleapis.com/maps/api/directions/xml?key="_+key+"&origin="+replace(origin," ","+")
               + "&destination="+replace(destination," ","+")+"&mode=";

    auto getDuration = [](const URL& url) {
        for(;;) {
            usleep( queryLimit*1000 );
            if(queryLimit>500) queryLimit--;
            extern int queryCount;
            assert_(queryCount < 2500/4);
            Map data = getURL(copy(url));
            Element root = parseXML(data);
            string status = root("DirectionsResponse")("status").content;
            if(status=="OK") {
                return (uint)parseInteger(root("DirectionsResponse")("route")("leg")("duration")("value").content);
            }
            if(status=="NOT_FOUND"_ || status=="ZERO_RESULTS") return 0u;
            if(status == "OVER_QUERY_LIMIT"_) {
                log(status, queryLimit);
                if(!queryLimit) queryLimit = 500;
                else queryLimit *= 2;
                extern const Folder& cache();
                remove(cacheFile(url), cache());
                continue;
            }
        }
    };

    uint bicycling = getDuration(URL(url+"bicycling"));
    assert(bicycling, origin, destination);
#if 1
    url = url+"transit";
    if(time>0) url= url+"&departure_time="+str(time);
    if(time<0) url= url+"&arrival_time="+str(-time);
    uint transit = getDuration(url);
    if(!transit) return bicycling;
    return ::min(bicycling, transit);
#else
    return bicycling;
#endif
}
