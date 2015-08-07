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
    struct Room {
        String href;
        String postDate;
        String startDate;
        String untilDate;
        uint price;
        String address;

        void load(const URL&, Map&& data) {
            const Element root = parseHTML(data);
            address = copyRef( root("html")("body")("#main")("#container")("#content")(".text result")
                                     .children[1]->children[3]->children[1]->children[2]->content ); // FIXME
            assert_(!address.contains('\n'));
            log(address);
            //log(apply(content.children, [](const Element& e) { return str(e.name, e.attributes); }));
        }
    };

    array<Room> rooms;
    WG() {
        URL url ("http://www.wgzimmer.ch/wgzimmer/search/mate.html?");
        url.post = "query=&priceMin=50&priceMax=1500&state=zurich-stadt&permanent=all&student=none&country=ch&orderBy=MetaData%2F%40mgnl%3Alastmodified&orderDir=descending&startSearchMate=true&wgStartSearch=true"__;
        getURL(move(url), {this, &WG::loadIndex});
    }
    void loadIndex(const URL& url, Map&& data) {
        const Element root = parseHTML(data);
        const auto& list = root("html")("body")("#main")("#container")("#content")("ul");
        for(const Element& li: list.children) {
            const Element& a = li.children[1];
            Room& room = rooms.append();
            room.href = copyRef(a.attribute("href"));
            room.postDate = copyRef(a.children[0]->children[0]->content);
            {TextData s (a.children[2]->content);
                s.skip("Bis:");
                s.whileAny(" \t\n");
                if(!s.match("No time restrictions")) room.untilDate = copyRef(s.untilEnd());
            }
            room.startDate = copyRef(a.children[2]->children[0]->content);
            {TextData s (a.children[3]->children[0]->content);
                s.skip("SFr. ");
                room.price = s.integer();
                s.skip(".00"_);
            }
            log(room.postDate, room.startDate, room.untilDate, room.price, room.href);
            getURL(url.relative(room.href), {&room, &Room::load});
            break;
        }
        log(rooms.size, "rooms");
    }
} app;
#endif
