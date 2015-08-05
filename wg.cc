#include "thread.h"
#include "file.h"
#include "http.h"
#include "xml.h"

struct WG {
    WG() {
        getURL("google.com"_, {this, &WG::load});
    }
    void load(const URL& url, Map&& data) {
        log(url, (string)data);
    }
} app;
