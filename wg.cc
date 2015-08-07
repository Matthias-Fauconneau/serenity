#include "thread.h"
#include "file.h"
#include "http.h"
#include "xml.h"
#include "math.h"

static string key = arguments()[0];

struct LocationRequest {
    String address;
    vec3 location;
    function<void(string, vec3)> handler;
    LocationRequest(string address, function<void(string, vec3)> handler) : address(copyRef(address)), handler(handler) {
        getURL(URL("https://maps.googleapis.com/maps/api/geocode/xml?key="_+key+"&address="+replace(address," ","+")+",+Zürich"),
               {this, &LocationRequest::parseLocation});
    }
    void parseLocation(const URL&, Map&& data) {
        Element root = parseXML(data);
        const auto& xmlLocation = root("GeocodeResponse")("result")("geometry")("location");
        vec2 location(parseDecimal(xmlLocation("lat").content), parseDecimal(xmlLocation("lng").content));
        this->location = vec3(location, 0);
        getURL(URL("https://maps.googleapis.com/maps/api/elevation/xml?key="_+key+"&locations="+str(location.x)+","+str(location.y)),
                {this, &LocationRequest::parseElevation});
    }
    void parseElevation(const URL&, Map&& data) {
        Element root = parseXML(data);
        location.z = parseDecimal(root("ElevationResponse")("result")("elevation").content);
        handler(address, location);
    }
};

#if 0
struct WG {
    array<unique<LocationRequest>> requests;
    map<string, vec3> locations;
    WG() {
        for(string address: arguments().slice(1)) {
            unique<LocationRequest> request (nullptr);
            request.pointer = new LocationRequest(address, {this, &WG::load}); // FIXME: does not forward properly through unique<T>(Args...)
            requests.append(move(request));
        }
    }
    void load(string address, vec3 location) {
        log(address, location);
        for(auto entry: locations) {
            float R = 6371000; // Earth radius
            float φ1 = PI/180* location.x;
            float φ2 = PI/180* entry.value.x ;
            float Δφ = φ1-φ2;
            float Δλ = PI/180* (entry.value.y-location.y);
            float a = sin(Δφ/2) * sin(Δφ/2) + cos(φ1) * cos(φ2) * sin(Δλ/2) * sin(Δλ/2);
            float c = 2 * atan(sqrt(a), sqrt(1-a));
            float dz = location.z - entry.value.z;
            float d = R * c + abs(dz);
            log(address, entry.key, round(d), abs(round(dz)));
        }
        locations.insert(address, location);
    }
} app;
#elif 1
struct WG {
    WG() {
        URL url ("http://www.wgzimmer.ch/wgzimmer/search/mate.html?");
        url.post = "priceMin=50&priceMax=1500&state=zurich-stadt&permanent=all&studio=false&student=none&country=ch&orderBy=MetaData/@mgnl:lastmodified"
                   "&orderDir=descending&startSearchMate=true&wgStartSearch=true"__;
        getURL(move(url), {this, &WG::load});
    }
    void load(const URL&, Map&& data) {
        log(/*(string)data,*/ data.size);
    }
} app;
#else
struct WG {
    WG() {
        const auto file = readFile("index.html");
        const Element root = parseHTML(file);
        const auto& list = root("html")("body")("#main")("#container")("#content")("ul");
        for(const Element& li: list.children) {
            const Element& a = li.children[1];
            string href = a.attribute("href");
            string postDate = a.children[0]->children[0]->content;
            string untilDate;
            {TextData s (a.children[2]->content);
                s.skip("Until:");
                s.whileAny(" \t\n");
                if(!s.match("No time restrictions")) untilDate = s.untilEnd();
            }
            string startDate = a.children[2]->children[0]->content;
            uint price;
            {TextData s (a.children[3]->children[0]->content);
                s.skip("SFr. ");
                price = s.integer();
                s.skip(".00"_);
            }
            log(postDate, startDate, untilDate, price, href);
            getURL(href, {this, &WG::load});
            break;
        }
        log(list.children.size);
    }
    void load(const URL&, Map&& data) {
        const Element root = parseHTML(data);
        const auto& address = root("html")("body")("#main")("#container")("#content")(".text result")
                               .children[1]->children[3]->children[1]->children[2]->content; // FIXME
        log(address);
        //log(apply(content.children, [](const Element& e) { return str(e.name, e.attributes); }));
    }
} app;
#endif
