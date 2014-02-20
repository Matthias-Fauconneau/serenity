#include "thread.h"
#include "time.h"
#include "window.h"
#include "text.h"
#include "data.h"
#include "plot.h"
#include "graphics.h"

/// Paramètres
/*const*/ bool graph = false; // Affiche un graphe de la première image (sinon profil de mouvement) [Entrée pour commuter]
const real uMax = 3e-2; // Ecart type du signal (pour normaliser) (si 0: valeurs brutes)
const bool slice = false; // Restreint le traitement de chaque prise à [tMin, tMax]
const real tMin = 21e-6; // Pour couper le début de chaque prise (μs)
const real tMax = 35e-6; // Pour couper la fin de chaque prise (μs)
/*const*/ bool enableLowPass = true; // Active le filtre passe-bas [Espace pour commuter]
const real f = 500e6; // Fréquence d'échantillonage [500MHz]
const real dt = 1/f; // Periode d'échantillonage [2μs]
const real F = 10e6; // Fréquence d'émission des ultrasons [10MHz]
const real lowPassFrequency = F/f; // Frequence (normalisée) de coupure du filtre passe-base
const uint samplePerCycle = round(f/F); // Nombre d'échantillons par cycle du signal d'émission
const real startThreshold = 0.99; // Seuil de correlation pour déclencher l'analyse du signal (quand la corrélation passe en dessous du seuil)
const real stopThreshold = 0.99; // Seuil de correlation pour stopper l'analyse du signal (quand la corrélation repasse au dessus du seuil)
const bool allowFrameDrops = true; // Accepte une série de données avec des prises manquantes
const bool reportFrameDrops = false; // Rapporte les prises manquantes
const uint frameSize = 10000; // Nombre d'échantillons par prise
/*const*/ bool enableLocalNormalization = true; // Active la normalisation local
const uint localNormalizationHalfWidth =  frameSize/16; // Demi largeur des fenetres de normalization local
const real vMax = 0; // Valeur de contraste pour l'affichage des signaux (0: automatique)

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

struct MotionVector { float t0, t1; };

struct MotionView : Widget {
    array<buffer<float>> frames;
    array<array<MotionVector>> motionVectors;
    void render(const Image &target) {
        int2 size = target.size();
        real vMin=inf, vMax=-inf;
        for(const ref<float>& frame: frames) for(real v: frame) vMin=min(vMin, v), vMax=max(vMax, v);
        if(::vMax) vMax = ::vMax, vMin = -::vMax;
        uint N = frames.size;
        for(uint T: range(N)) {
            const buffer<float>& frame = frames[T];
            const uint n = frame.size;
            int x0 = size.x * T / N;
            int x1 = size.x * (T+1) / N;
            for(uint y: range(size.y)) {
                int i0 = n * (y+0) / size.y;
                int i1 = n * (y+1) / size.y;
                real v = 0;
                for(uint i: range(i0, i1)) v += frame[i];
                v /= (i1-i0);
                vec3 c = (v-vMin)/(vMax-vMin); // [-1,1] -> [0,1]
                fill(target, Rect(int2(x0, size.y-1-y), int2(x1, size.y-y)), c);
            }
            for(MotionVector v: motionVectors[T]) {
                line(target, int2(size.x * (T+1./2) / N, size.y * v.t0 / n), int2(size.x * (T+1+1./2) / N, size.y * v.t1 / n));
            }
        }
    }
};

/// Computes normalized cross correlation
real correlation(const ref<float>& u, const ref<float>& v) {
    assert_(u.size == v.size);
    uint N = u.size;
    float uu = 0, uv = 0, vv = 0;
    for(uint i: range(N)) {
        uu += u[i] * u[i];
        uv += u[i] * v[i];
        vv += v[i] * v[i];
    }
    return real(uv) / sqrt(real(uu)*real(vv));
}

/// Normalizes signal by variance over a sliding window
buffer<float> localNormalization(const ref<float>& u, uint halfWidth) {
    uint N = u.size;
    buffer<float> v (N /*-2*halfWidth*/);
    v.clear(0); // DEBUG
    for(uint i0: range(halfWidth,N-halfWidth)) {
        real uu = 0;
        for(uint i: range(i0-halfWidth, i0+halfWidth)) uu += u[i] * u[i];
        v[i0] = u[i0] / sqrt(uu); // Normalizes by window variance
    }
    return v;
}

struct Test {
    Folder folder = replace(arguments()[0],"~"_,homePath());
    Plot plot;
    MotionView motionView;
    Window window {graph ? (Widget*)&plot : &motionView, int2(768), arguments()[0]};
    Test() {
        process();
        plot.plotPoints = false, plot.plotLines = true;
        window.actions[Escape] = []{ exit(); };
        window.actions[Return] = [this]{ graph=!graph; window.widget = graph ? (Widget*)&plot : &motionView; process(); window.render(); };
        window.actions[Space] = [this]{
            //enableLowPass=!enableLowPass;
            enableLocalNormalization = !enableLocalNormalization;
            window.setTitle(enableLocalNormalization ? "Local Normalization"_ : "Raw"_);
            process();
            window.render();
        };
        window.background = Window::White;
        window.show();
    }
    void process() {
        plot.dataSets.clear();
        array<buffer<float>> frames;
        array<String> samples = folder.list(Files|Sorted);
        real T0 = 0, pT = 0, dT=0; // Previous timestamp, time step
        for(string name: samples.slice(0, graph ? 1 : samples.size)) {
            real T = ({TextData s (name); s.skip("data_"_); s.integer(); s.skip("_"_); s.decimal();}); // Parses frame timestamp
            if(!T0) T0=T; // Records first timestamp
            T -= T0; // Sets all timestamp relative to first frame (T[0]=0)
            if(pT) { // After first frame
                if(!dT) dT = T-pT; // Time step from first two frames
                else {
                    uint N = round((T-pT)/dT); // Expected frames since last frame
                    if(N>1) { // Dropped frames
                        if(!allowFrameDrops) error(str(pT)+"s -> "_+str(T)+"s:"_, N-1, "frame(s) dropped");
                        if(reportFrameDrops) log(str(pT)+"s -> "_+str(T)+"s:"_, N-1, "frame(s) dropped");
                        for(uint unused i: range(N-1)) frames << array<float>();
                    }
                }
            }
            pT = T;
            buffer<float> frame (frameSize); uint i=0;
            LowPass filter ( lowPassFrequency );
            Map file(name, folder);
            real pt = 0;
            for(TextData s (file);s;) {
                real t = s.decimal(); s.whileAny(" \t"_);
                if(pt) assert_(abs((t-pt)/dt-1)<exp2(-32), 1/(t-pt)*1e-9, log2(abs((t-pt)/dt-1)));
                pt=t;
                real u = s.decimal(); s.skip("\r\n"_);
                if(uMax) u /= uMax;
                if(enableLowPass) u = filter(u);
                if(!slice || (t>tMin && t<tMax)) {
                    if(graph) plot.dataSets[name][t] = u;
                    else frame[i++] = float(u);
                }
            }
            frames << move(frame);
        }
        range best = 0; // Longest series of contiguous frames
        int start=-1; // First frame of evolution
        for(int index: range(frames.size-1)) {
            real NCC = frames[index] && frames[index+1] ? correlation(frames[index], frames[index+1]) : 1;
            if(start<0 && NCC < startThreshold) {
                start = index;
                if(index > 0 && frames[index-1]) start = index-1; // Starts one frame early (just to be sure)
            }
            if(start>=0 && NCC > stopThreshold) {
                if(index - start > best.stop - best.start) best = range(start, index); // Sets current best candidate
                start = -1;
            }
        }
        motionView.frames.clear();
        for(uint index: best) {
            buffer<float> frame = move(frames[index]);
            if(enableLocalNormalization) frame = localNormalization(frame, localNormalizationHalfWidth);
            motionView.frames << move(frame);
            array<MotionVector> motionVectors;
            //TODO: compute motion vectors
            motionView.motionVectors << move(motionVectors);
        }
    }
} test;
