#include "thread.h"
#include "algebra.h"
#include "window.h"
#include "interface.h"
#include "plot.h"
#include "png.h"

/// Solves {-εu'' + u' |[0,1] = 1, u|0=0, u|1=0} on the given nodes
map<real, real> solve(const real e, const ref<real>& x) {
    const uint N = x.size;
    Matrix A (N); Vector b(N);
    for(uint i: range(1,N-1)) {
        real h[] = { x[i]-x[i-1], x[i+1]-x[i] };
        real a[] = { e / h[0] , e / h[1] };
        A(i, i-1)  = - a[0] - 1./2;
        A(i,i) = a[0] + a[1];
        A(i, i+1) = - a[1] + 1./2;
        b[i] = (h[0] + h[1]) / 2;
    }
    A(0,0) = 1; b[0] = 0; // u|0 = 0
    A(N-1,N-1) = 1; b[N-1] = 0; // u|1 = 0
    return {x, UMFPACK(A).solve(b) };
}

struct Application {
    Plot plot {"-εu'' + u' = 1"_, {"Analytic"_, "Regular"_, "Refined"_}, true, Plot::TopLeft};
    Window window {&plot, int2(-1), plot.title};

    Application() {
        const real e = 0.01;
        map<real,real>& analytic = plot.dataSets.first();
        for(uint i: range(window.size.x)) {
            real x = (real) i / (window.size.x-1);
            real y = x - (exp(x/e) - 1) / (exp(1/e) - 1);
            analytic.insert(x,y);
        }
        const uint N = 10;
        plot.dataSets[1] = solve(e, apply(N,[](const int i){ return (real) i / (N-1); }));
        plot.dataSets[2] = solve(e, apply(N,[](const int i){ return pow((real) i / (N-1) , 0.1); }) );
        window.backgroundColor = window.backgroundCenter = 1;
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
        writeFile("N="_+str(N)+".png"_, encodePNG(renderToImage(plot,int2(512))), home());
    }
} app;
