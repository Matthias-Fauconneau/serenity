#include "thread.h"
#include "file.h"
#include "http.h"
#include "xml.h"

static string key = arguments()[0];

struct LocationRequest {
    String address;
    function<void(string, vec2)> handler;
    LocationRequest(string address, function<void(string, vec2)> handler) : address(copyRef(address)), handler(handler) {
        getURL(URL("https://maps.googleapis.com/maps/api/geocode/xml?key="_+key+"&address="+replace(address," ","+")+",+Zürich"),
               {this, &LocationRequest::load});
    }
    void load(const URL&, Map&& data) {
        Element root = parseXML(data);
        const auto& location = root("GeocodeResponse")("result")("geometry")("location");
        handler(address, vec2(parseDecimal(location("lat").content), parseDecimal(location("lng").content)));
    }
};

struct WG {
    array<unique<LocationRequest>> requests;
    WG() {
        unique<LocationRequest> request (nullptr);
        request.pointer = new LocationRequest(arguments()[1], {this, &WG::load}); // FIXME: does not forward properly through unique<T>(Args...)
        requests.append(move(request));
    }
    void load(string address, vec2 location) {
        log(address, location);
    }
} app;
