#include "thread.h"

struct Application {
    Application() {
        Folder results = "Results"_;
        for(string name: results.list()) {
            log(name);
        }
    }
} app;
