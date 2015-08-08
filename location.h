#pragma once

static string key = arguments()[0];
constexpr int maximumAge = 7*24;

struct LocationRequest {
    String address;
    vec3 location;
    function<void(string, vec3)> handler;
    LocationRequest(string address, function<void(string, vec3)> handler) : address(copyRef(address)), handler(handler) {
        getURL(URL("https://maps.googleapis.com/maps/api/geocode/xml?key="_+key+"&address="+replace(address," ","+")),
               {this, &LocationRequest::parseLocation}, maximumAge);
    }
    LocationRequest(vec3 location, function<void(string, vec3)> handler) : location(location), handler(handler) {
        getURL(URL("https://maps.googleapis.com/maps/api/geocode/xml?key="_+key+"&latlng="+str(location.x)+","+str(location.y)),
               {this, &LocationRequest::parseAddress}, maximumAge);
    }
    void parseLocation(const URL&, Map&& data) {
        Element root = parseXML(data);
        const auto& xmlLocation = root("GeocodeResponse")("result")("geometry")("location");
        vec2 location(parseDecimal(xmlLocation("lat").content), parseDecimal(xmlLocation("lng").content));
        this->location = vec3(location, 0);
        getURL(URL("https://maps.googleapis.com/maps/api/elevation/xml?key="_+key+"&locations="+str(location.x)+","+str(location.y)),
                {this, &LocationRequest::parseElevation}, maximumAge);
    }
    void parseAddress(const URL&, Map&& data) {
        Element root = parseXML(data);
        this->address = copyRef( root("GeocodeResponse")("result")("address_component")("short_name").content );
        handler(address, location);
    }
    void parseElevation(const URL&, Map&& data) {
        Element root = parseXML(data);
        location.z = parseDecimal(root("ElevationResponse")("result")("elevation").content);
        handler(address, location);
    }
};

vec3 distance(vec3 A, vec3 B) {
    float R = 6378137; // FIXME TODO: proper WGS84
    float φ1 = PI/180* A.x;
    float φ2 = PI/180* B.x;
    float Δφ = φ1-φ2;
    float Δλ = PI/180* (B.y-A.y);
    float a = sin(Δφ/2) * sin(Δφ/2) + cos(φ1) * cos(φ2) * sin(Δλ/2) * sin(Δλ/2);
    float c = 2 * atan(sqrt(a), sqrt(1-a));
    float dz = B.z - A.z;
    float d = R * c + abs(dz);
    return vec3((d/15+abs(dz))*60/1000, d, dz);
}
