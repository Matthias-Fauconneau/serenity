#include "thread.h"
#include "math.h"
#include "plot.h"
#include "layout.h"
#include "window.h"

// for N=2^(12..14), plot min(log2(1+x/48000),log2(1+48000/(N*x))), x=30..5000
struct Plotter {
    VList<Plot> plots;
    Window window {&plots, int2(1024, 768), "Tuner"};
    Plotter() {
        Plot plot;
        for(int N: ref<int>{4096,8192,16384}) {
            map<float,float> points;
            for(float x: range(30, 5000)) points.insert(x, 100*12*min(log2(1+x/48000),log2(1+48000/(N*x))));
            plot.legends << dec(N);
            plot.dataSets << move(points);
        }
        plots << move(plot);
        if(plots) {
            window.backgroundColor=window.backgroundCenter=1;
            window.show();
            window.localShortcut(Escape).connect([]{exit();});
        }
    }
} app;
