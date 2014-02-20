#include "thread.h"
#include "time.h"
#include "window.h"
#include "text.h"
#include "data.h"
#include "plot.h"
#include "graphics.h"

/// Paramètres
/*const*/ bool graph = true; // Affiche un graphe de la première image (sinon correlogramme) [Entrée pour commuter]
const real vMax = 3e-2; // Valeur typique des maximums du signal (pour normaliser) (si 0: valeurs brutes)
const bool slice = false; // Restreint le traitement de chaque prise à [tMin, tMax]
const real tMin = 21e-6; // Pour couper le début de chaque prise (μs)
const real tMax = 35e-6; // Pour couper la fin de chaque prise (μs)
/*const*/ bool filtre = false; // Active le filtre passe-bas [Espace pour commuter]
const real f = 500e6; // Fréquence d'échantillonage [500MHz]
const real dt = 1/f; // Periode d'échantillonage [2μs]
const real F = 10e6; // Fréquence d'émission des ultrasons [10MHz]
const real lowPassFrequency = F/f; // Frequence (normalisée) de coupure du filtre passe-base

// Biquad filter
struct Biquad {
    real a1,a2,b0,b1,b2;
    real x1=0, x2=0, y1=0, y2=0;
    real operator ()(real x) {
        real y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1, x1=x, y2=y1, y1=y;
        return y;
    }
    void reset() { x1=0, x2=0, y1=0, y2=0; }
};

// Low pass filter
// H(s) = 1 / (s^2 + s/Q + 1)
struct LowPass : Biquad {
    LowPass(real f, real Q=1./sqrt(2.)) {
        real w0 = 2*PI*f;
        real alpha = sin(w0)/(2*Q);
        real a0 = 1+alpha;
        a1 = -2*cos(w0)/a0, a2 = (1-alpha)/a0;
        b0 = ((1-cos(w0))/2)/a0, b1 = (1-cos(w0))/a0, b2 = ((1-cos(w0))/2)/a0;
    }
};

struct Correlogram : Widget {
    array<array<real>> dataSets;
    void render(const Image &target) {
        int2 size = target.size();
        uint N = dataSets.size;
        for(uint i: range(N)) {
            const array<real>& data = dataSets[i];
            const uint n = data.size;
            int x0 = size.x * i / N;
            int x1 = size.x * (i+1) / N;
            for(uint j: range(n)) {
                int y0 = size.y - size.y * (j+1) / n;
                int y1 = size.y - size.y * (j+0) / n;
                real v = data[j];
                real c = (1+v)/2; // [-1,1] -> [0,1]
                fill(target, Rect(int2(x0, y0), int2(x1, y1)), c);
            }
        }
    }
};

struct Test {
    Folder folder = replace(arguments()[0],"~"_,homePath());
    Plot plot;
    Correlogram correlogram;
    Window window {graph ? (Widget*)&plot : &correlogram, int2(768), arguments()[0]};
    Test() {
        process();
        plot.plotPoints = false, plot.plotLines = true;
        window.actions[Escape] = []{ exit(); };
        window.actions[Return] = [this]{ graph=!graph; window.widget = graph ? (Widget*)&plot : &correlogram; process(); window.render(); };
        window.actions[Space] = [this]{ filtre=!filtre; process(); window.render(); };
        window.background = Window::White;
        window.show();
    }
    void process() {
        correlogram.dataSets.clear(); plot.dataSets.clear();
        array<String> samples = folder.list(Files|Sorted);
        real T0 = 0, pT = 0, dT=0; // Previous timestamp, time step
        for(string name: samples.slice(0, graph ? 1 : samples.size)) {
            real T = ({TextData s (name); s.skip("data_"_); s.integer(); s.skip("_"_); s.decimal();}); // Parses frame timestamp
            if(!T0) T0=T; // Records first timestamp
            if(pT) { // After first frame
                if(!dT) dT = T-pT; // Time step from first two frames
                else {
                    uint N = round((T-pT)/dT); // Expected frames since last frame
                    if(N>1) { // Dropped frames
                        log(str(pT-T0)+"s -> "_+str(T-T0)+"s:"_, N-1, "frame(s) dropped");
                        for(uint unused i: range(N-1)) correlogram.dataSets << array<real>();
                    }
                }
            }
            pT = T;
            array<real> data (10000);
            LowPass filter ( lowPassFrequency );
            Map file(name, folder);
            real pt = 0;
            for(TextData s (file);s;) {
                real t = s.decimal(); s.whileAny(" \t"_);
                if(pt) assert_(abs((t-pt)/dt-1)<exp2(-32), 1/(t-pt)*1e-9, log2(abs((t-pt)/dt-1)));
                pt=t;
                real v = s.decimal(); s.skip("\r\n"_);
                if(vMax) v /= vMax;
                if(filtre) v = filter(v);
                if(!slice || (t>tMin && t<tMax)) {
                    if(graph) plot.dataSets[name][t] = v;
                    else data << v;
                }
            }
            correlogram.dataSets << move(data);
        }
    }
} test;
