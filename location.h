#pragma once

static string key = arguments()[0];
constexpr int maximumAge = 14*24;

struct LocationRequest {
    String address;
    vec3 location;
    function<void(string, vec3)> handler;
    bool wait = false;
    LocationRequest(string address, function<void(string, vec3)> handler={}, bool wait=true) : address(copyRef(address)), handler(handler), wait(wait) {
        getURL(URL("https://maps.googleapis.com/maps/api/geocode/xml?key="_+key+"&address="+replace(address," ","+")),
               {this, &LocationRequest::parseLocation}, maximumAge, wait);
    }
    LocationRequest(vec3 location, function<void(string, vec3)> handler) : location(location), handler(handler) {
        getURL(URL("https://maps.googleapis.com/maps/api/geocode/xml?key="_+key+"&latlng="+str(location.x)+","+str(location.y)),
               {this, &LocationRequest::parseAddress}, maximumAge, wait);
    }
    void parseLocation(const URL& url, Map&& data) {
        Element root = parseXML(data);
        string status = root("GeocodeResponse")("status").content;
        if(status == "ZERO_RESULTS"_) return;
        assert_(status=="OK", status, root, cacheFile(url));
        const auto& xmlLocation = root("GeocodeResponse")("result")("geometry")("location");
        vec2 location(parseDecimal(xmlLocation("lat").content), parseDecimal(xmlLocation("lng").content));
        assert_(location);
        this->location = vec3(location, 0);
        if(handler) handler(address, vec3(location, 0));
        /*getURL(URL("https://maps.googleapis.com/maps/api/elevation/xml?key="_+key+"&locations="+str(location.x)+","+str(location.y)),
                {this, &LocationRequest::parseElevation}, maximumAge, wait);*/
    }
    void parseAddress(const URL&, Map&& data) {
        this->address = copyRef( parseXML(data)("GeocodeResponse")("result")("address_component")("short_name").content );
        handler(address, location);
    }
    /*void parseElevation(const URL&, Map&& data) {
        location.z = parseDecimal(parseXML(data)("ElevationResponse")("result")("elevation").content);
        assert_(location);
        if(handler) handler(address, location);
    }*/
};

struct DirectionsRequest {
    String origin, destination;
    uint transit = 0, bicycling = 0, duration = 0;
    function<void(string, string, uint)> handler;
    DirectionsRequest(string origin, string destination, function<void(string, string, uint)> handler={}, bool wait=true)
        : origin(copyRef(origin)), destination(copyRef(destination)), handler(handler) {
        assert_(origin != destination, origin, destination);
        Date arrival = parseDate("13/08 19:00");
        getURL(URL("https://maps.googleapis.com/maps/api/directions/xml?key="_+key+
                   "&origin="+replace(destination," ","+")+"&destination="+replace(origin," ","+")+"&mode=transit&arrival_time="+str((int64)arrival)),
               [this](const URL& url, Map&& data) { parse(url, data, transit); }, maximumAge, wait);
        getURL(URL("https://maps.googleapis.com/maps/api/directions/xml?key="_+key+
                   "&origin="+replace(origin," ","+")+"&destination="+replace(destination," ","+")+"&mode=bicycling"),
               [this](const URL& url, Map&& data) { parse(url, data, bicycling); }, maximumAge, wait);
        //if(wait) assert_(duration, transit, bicycling);
    }
    void parse(const URL& url, string data, uint& duration) {
        Element root = parseXML(data);
        string status = root("DirectionsResponse")("status").content;
        if(status!="NOT_FOUND"_ && status!="ZERO_RESULTS") {
            assert_(status=="OK", root, cacheFile(url));
            duration = parseInteger(root("DirectionsResponse")("route")("leg")("duration")("value").content);
            //assert_(duration || &duration==&transit /*FIXME: check whether address location are close*/, parseXML(data), origin, destination, url);
        }
        if(transit && bicycling) { this->duration=min(transit, bicycling); if(handler) handler(origin, destination, duration); }
    }
};

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
float distance(vec3 A, vec3 B) { return sqrt(sq(distance(A.xy(), B.xy())) + sq(B.z - A.z)); }
