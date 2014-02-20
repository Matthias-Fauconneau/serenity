#include "thread.h"
#include "time.h"
#include "window.h"
#include "text.h"
#include "data.h"
#include "plot.h"
#include "graphics.h"

const real vMax = 3e-2; // Valeur maximum du signal (pour saturer)
const bool slice = false; // Restreint le traitement a [tMin, tMax]
const real tMin = 21e-6; // Pour couper le debut (us)
const real tMax = 35e-6; // Pour couper la fin (us)
const bool graph = false; // Affiche un graphe de la première image (sinon correlogramme)

struct Correlogram : Widget {
    array<array<real>> dataSets;
    void render(const Image &target) {
        int2 size = target.size();
        real vMin=inf, vMax=-inf;
        if(0) {
            for(const auto& data: dataSets) vMin=min(vMin, min(data));
            for(const auto& data: dataSets) vMax=max(vMax, max(data));
        } else {
            vMax=::vMax, vMin=-vMax;
        }
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
                real c = (v-vMin) / (vMax-vMin); // Normalized to [0,1]
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
        plot.plotPoints = false, plot.plotLines = true;
        auto samples = folder.list(Files|Sorted);
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
            Map file(name, folder);
            for(TextData s (file);s;) {
                real t = s.decimal(); s.whileAny(" \t"_); real y = s.decimal(); s.skip("\r\n"_);
                if(!slice || (t>tMin && t<tMax)) {
                    if(graph) plot.dataSets[name][t] = y;
                    else data << y;
                }
            }
            correlogram.dataSets << move(data);
        }
        window.actions[Escape] = []{ exit(); };
        window.background = Window::White;
        window.show();
    }
} test;
