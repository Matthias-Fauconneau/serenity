#include "thread.h"
#include "algebra.h"
#include "math.h"
#include "time.h"
#include "window.h"
#include "interface.h"
#include "plot.h"
#include "png.h"

struct Application {
    Plot plot {{"Analytic"_, "Regular"_, "Refined"_}, true, Plot::TopLeft};
    const uint N = 8;
    const real e = exp2(-4);
    Window window {&plot, int2(-1), "-εu'' + u' = 1"_};

    /// Solves {-εu'' + u' |[0,1] = 1, u|0=0, u|1=1} at resolution N
    void solve(uint N) {
    }

    Application() {
        map<float,float>& analytic = plot.dataSets.first();
        for(uint i: range(window.size.x)) {
            real x = (real) i / window.size.x;
            real y = x - (exp(x/e) - 1) / (exp(1/e) - 1);
            analytic.insert(x,y);
        }
        window.backgroundColor = window.backgroundCenter = 1;
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
    }
} app;
