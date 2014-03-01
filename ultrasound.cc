#include "thread.h"
#include "time.h"
#include "window.h"
#include "text.h"
#include "data.h"
#include "plot.h"
#include "graphics.h"

/// Paramètres

// Input
const uint frameRate = 11; // Expected frame rate
const bool allowFrameDrops = true; // Accepte une série de données avec des prises manquantes
const bool reportFrameDrops = false; // Rapporte les prises manquantes
const uint frameSize = 10000; // Nombre d'échantillons par prise

// Low pass
bool enableLowPass = true; // Active le filtre passe-bas ['l' pour commuter] (semble utile)
const real f = 500e6; // Fréquence d'échantillonage [500MHz]
const real dt = 1/f; // Periode d'échantillonage [2μs]
const real F = 10e6; // Fréquence d'émission des ultrasons [10MHz]
const real lowPassFrequency = F/f; // Frequence (normalisée) de coupure du filtre passe-base
const uint samplePerCycle = round(f/F); // Nombre d'échantillons par cycle du signal d'émission

// Normalization
bool enableWindowNormalization = true; // Active la normalisation local ['n' pour commuter]
const uint windowNormalizationWindowHalfWidth =  frameSize/16; // Demi largeur des fenetres de normalization local

// Graph view
bool graph = false; // Affiche un graphe de la première image (sinon profil de mouvement) ['g' pour commuter]
const uint graphStart = 0; // Index of the first frame to display as graphs
const uint graphFrameCount = 1; // Number of frames to display as graphs

const bool slice = false; // Restreint le traitement et l'affichage des graphes chaque prise à [tMin, tMax]
const real tMin = 21e-6; // Pour couper le début de chaque prise (s)
const real tMax = 35e-6; // Pour couper la fin de chaque prise (s)

// Analysis range
const uint motionStart = 0; // Index of the first frame to display in motion view
const uint motionCount = 0; // Number of frames to display in motion view (0: all or automatic (if cut is true))

const bool cut = true; // Restreint le traitement et l'affichage de chaque experience à la partie instationnaire
const real startThreshold = 0.99; // Seuil de correlation pour déclencher l'analyse du signal (quand la corrélation passe en dessous du seuil)
const real stopThreshold = 0.99; // Seuil de correlation pour stopper l'analyse du signal (quand la corrélation repasse au dessus du seuil)

// Motion view
const real vMax = 0; // Valeur de contraste pour l'affichage des signaux (0: automatique)

bool showMotionVectors = true; // Affiche les vecteurs de déplacement ('m' pour commuter)
const uint motionVectorSampleCount = 32; // Nombre de vecteurs de déplacement à estimer (régulièrement espacés)
const uint motionVectorWindowHalfWidth = (frameSize/motionVectorSampleCount)/2/2; // Demi largeur des fenetres de correlation
const uint motionVectorMaximumMotion = (frameSize/motionVectorSampleCount)/2/2; // Déplacement maximum (limite l'évaluation exhaustive)
const real motionVectorCorrelationThreshold = 0; // Correlation minimum pour ajouter un vecteur de déplacement
bool showVelocityProfile = true; // Affiche le profil de vitesse ('v' pour commuter)

// Biquad filter
struct Biquad {
    float a1,a2,b0,b1,b2;
    float x1=0, x2=0, y1=0, y2=0;
    float operator ()(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
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

/// Normalizes signal by mean and deviation over a sliding window
buffer<float> windowNormalization(buffer<float>&& u, uint halfWidth) {
    const uint r = halfWidth;
    uint N = u.size;
    assert_(N>r);
    buffer<float> t (N);
    {
        float U = 0; uint n=r;
        for(uint i: range(r)) U += u[i];
        for(uint i: range(N)) {
            if(i+r<N) { U += u[i+r]; n++; } // Slides value in
            float mean = U / n;
            t[i] = u[i] - mean; // Substracs by window mean
            if(i>=r) { U -= u[i-r]; n--; } // Slides value out
        }
    }
    {
        float UU = 0; uint n=0;
        for(uint i: range(r)) UU += sq(t[i]);
        for(uint i: range(N)) {
            if(i<N-r) { UU += sq(t[i+r]); n++; } // Slides value in
            u[i] = t[i] / sqrt(UU/n); // Divides by window deviation
            if(i>=r) { UU -= sq(t[i-r]); n--; } // Slides value out
        }
    }
    return move(u);
}

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
    return uu>0 && uv>0 ? real(uv) / sqrt(real(uu)*real(vv)) : 0;
}

struct MotionVector { uint t0, t1; };

struct Experiment {
    Folder folder;
    map<int64,String> files;
    array<buffer<float>> frames;
    range analyzedRange = 0;
    array<array<MotionVector>> motionVectors;

    Experiment(const Folder& folder, map<int64,String>&& files) : folder(""_,folder), files(move(files)) {}

    void process() {
        if(!frames) { // Loads and preprocess frames
            Folder cache(".cache"_, folder, true);
            int64 T0 = files.keys.first(); // First timestamp
            for(uint fileIndex: range(files.size())) {
                // Asserts regular time sampling (inserts null frames otherwise)
                int64 T = files.keys[fileIndex] - T0;
                string name = files.values[fileIndex];
                uint frameIndex = frames.size;
                uint actualFrameIndex = T*frameRate/1000; // Actual frameIndex
                if(actualFrameIndex!=frameIndex) { // Dropped frames
                    assert_(actualFrameIndex > frameIndex);
                    uint N = actualFrameIndex - frameIndex;
                    if(!allowFrameDrops) error(N, "frame(s) dropped", fileIndex, files.keys[fileIndex-1]-T0, T, name);
                    if(reportFrameDrops) log(N, "frame(s) dropped", fileIndex, files.keys[fileIndex-1]-T0, T, name);
                    for(uint unused i: range(N-1)) frames << array<float>();
                }

                const uint N = frameSize;
                if((!existsFile(name, cache) || File(name, cache).size() != N*sizeof(float)) && File(name, folder).size() != N*sizeof(float)) {
                    Map file(name, folder);
                    TextData s (file);
                    buffer<real> frame(N);
                    real pt = 0;
                    real U=0;
                    for(uint i: range(N)) {
                        real t = s.decimal(); s.whileAny(" \t"_);
                        if(pt) assert_(abs((t-pt)/dt-1)<exp2(-32), 1/(t-pt)*1e-9, log2(abs((t-pt)/dt-1)));
                        pt=t;
                        real u = s.decimal(); s.skip("\r\n"_);
                        frame[i] = u;
                        U += u;
                    }
                    U /= N;
                    real UU=0;
                    for(real& u: frame) { u -= U; UU += sq(u); }
                    UU /= N;
                    real scale = 1/sqrt(UU);
                    // Caches ASCII conversion to binary file (also quantizes to 24bit single precision (72dBFS))
                    File binary (name, cache, Flags(ReadWrite|Create|Truncate));
                    binary.resize(N*sizeof(float));
                    Map map (binary, Map::Write);
                    mref<float> output = mcast<float,byte>(map);
                    for(uint i: range(N)) output[i] = scale*frame[i];
                }
                Map map (name, existsFile(name, cache) ? cache : folder);
                ref<float> input = cast<float,byte>(map);
                buffer<float> frame(N);
                LowPass filter ( lowPassFrequency );
                for(uint i: range(N)) frame[i] = enableLowPass ? filter(input[i]) : input[i];
                if(enableWindowNormalization) frame = windowNormalization(move(frame), windowNormalizationWindowHalfWidth);
                frames << move(frame);
            }
            analyzedRange = 0;
        }
        if(!analyzedRange) { // Selects best range to analyze
            if(cut) {
                range best = 0; // Longest series of contiguous frames
                int start=-1; // First frame of evolution
                for(uint index: range(frames.size)) {
                    real NCC = frames[index] && index+1 < frames.size && frames[index+1] ? correlation(frames[index], frames[index+1]) : 1;
                    if(start<0 && NCC < startThreshold) {
                        start = index;
                        if(int(index-1) >= 0 && frames[index-1]) start = index-1; // Starts one frame early (just to be sure)
                    }
                    if(start>=0 && NCC > stopThreshold) {
                        uint end = index-1;
                        if(index+1 < frames.size && frames[index+1]) end = index+1; // Ends one frame late (just to be sure)
                        if(int(index - start) > best.stop - best.start) best = range(start, end); // Sets current best candidate
                        start = -1;
                    }
                }
                analyzedRange = best;
            } else analyzedRange = range(frames.size); // Processes all frames
            if(motionCount) analyzedRange = range(motionStart, motionStart+motionCount); // User override
            motionVectors.clear();
        }
        if(!motionVectors) { // Computes motion vectors on selected range
            for(int frameIndex: range(analyzedRange.start, min<int>(frames.size-1,analyzedRange.stop))) {
                array<MotionVector> frameMotionVectors;
                ref<float> current = frames[frameIndex];
                const ref<float> next = frames[frameIndex+1];
                if(current && next) {
                    const uint dxMax = motionVectorMaximumMotion;
                    const uint w = motionVectorWindowHalfWidth;
                    const uint n = current.size-2*(dxMax+w);
                    for(uint mvIndex: range(motionVectorSampleCount)) {
                        const uint x0 = (dxMax+w) + mvIndex * n / motionVectorSampleCount;
                        const ref<float> source = current.slice(x0-w, 2*w);
                        struct Candidate { uint x; real correlation; } best = {0,0};
                        for(int dx: range(-dxMax, dxMax +1)) {
                            uint x = x0+dx;
                            ref<float> target = next.slice(x-w, 2*w);
                            real correlation = ::correlation(source, target);
                            if(correlation > best.correlation) best = {x, correlation};
                        }
                        if(best.correlation > motionVectorCorrelationThreshold) frameMotionVectors << MotionVector{x0, best.x};
                    }
                }
                motionVectors << move(frameMotionVectors);
            }
        }
    }
};

struct MotionView : Widget {
    ref<buffer<float>> frames;
    ref<array<MotionVector>> motionVectors;
    void render(const Image &target) {
        int2 size = target.size();
        uint profileWidth = 128;
        uint N = frames.size;
        uint width = showVelocityProfile ? (size.x - profileWidth) / N * N : size.x;
        profileWidth = size.x - width;
        float vMin=inf, vMax=-inf;
        for(const ref<float>& frame: frames) for(float v: frame) vMin=min(vMin, v), vMax=max(vMax, v);
        if(::vMax) vMax = ::vMax, vMin = -::vMax;
        for(uint T: range(N)) {
            const buffer<float>& frame = frames[T];
            if(!frame) continue;
            const uint n = frame.size;
            int x0 = width * (T+0) / N;
            int x1 = width * (T+1) / N;
            for(uint y: range(size.y)) {
                int i0 = n * (y+0) / size.y;
                int i1 = n * (y+1) / size.y;
                float v = 0;
                for(uint i: range(i0, i1)) v += frame[i];
                v /= (i1-i0);
                vec3 c = (v-vMin)/(vMax-vMin); // [-1,1] -> [0,1]
                fill(target, Rect(int2(x0, size.y-1-y), int2(x1, size.y-y)), c);
            }
        }
        if(showMotionVectors) {
            for(uint T: range(motionVectors.size)) {
                for(MotionVector v: motionVectors[T]) {
                    const uint n = frames[T].size;
                    line(target, int2(width* (T+1./2) / N, size.y-1- size.y * v.t0 / n), int2(width * (T+1+1./2) / N, size.y-1- size.y * v.t1 / n), blue);
                }
            }
        }
        if(showVelocityProfile) {
            fill(target, Rect(int2(width, 0),size), white);
            array<uint> origins = apply(motionVectors.first(), [](const MotionVector& mv) { return mv.t0; }); // Assumes constant vector origins
            buffer<real> motions (origins.size); motions.clear(0);
            for(uint T: range(motionVectors.size)) {
                for(uint i: range(motionVectors[T].size)) {
                    const MotionVector& mv = motionVectors[T][i];
                    motions[i] += int(mv.t1 - mv.t0);
                }
            }
            for(real& v: motions) v /= motionVectors.size;
            real mvMax = max(motions);
            const uint n = frames.first().size;
            for(uint i: range(origins.size-1)) {
                uint t0 = origins[i+0];
                uint t1 = origins[i+1];
                real v0 = motions[i+0];
                real v1 = motions[i+1];
                real x0 = profileWidth * v0 / mvMax;
                real x1 = profileWidth * v1 / mvMax;
                real y0 =  size.y-1- size.y * t0 / n;
                real y1 =  size.y-1- size.y * t1 / n;
                line(target, int2(width+x0, y0), int2(width+x1, y1), blue);
            }
        }
    }
};

struct Application {
    Folder rootFolder = replace(arguments() ? arguments()[0] : ""_, "~"_, homePath());
    array<unique<Experiment>> experiments; // Using unique to support resizing experiments without invalidating any live reference
    uint currentExperiment = 0;
    Plot plot;
    MotionView motionView;
    Window window {graph ? (Widget*)&plot : &motionView, int2(768), rootFolder.name()};

    Application() {
        plot.plotPoints = false, plot.plotLines = true;
        window.actions[Escape] = []{ exit(); };
        window.actions['g'] = [this]{ graph=!graph; update(); };
        window.actions['l'] = [this]{ enableLowPass=!enableLowPass; reset(); update(); };
        window.actions['n'] = [this]{ enableWindowNormalization = !enableWindowNormalization; reset(); update(); };
        window.actions['m'] = [this]{ showMotionVectors=!showMotionVectors; window.render(); };
        window.actions['v'] = [this]{ showVelocityProfile=!showVelocityProfile; window.render(); };
        window.actions[RightArrow] = [this]{ currentExperiment = (currentExperiment+1)%experiments.size; update(); };
        window.actions[LeftArrow] = [this]{ currentExperiment = (currentExperiment+experiments.size-1)%experiments.size; update(); };
#if PROFILE
        window.frameSent = [this] { if(++currentExperiment < experiments.size) update(); else exit(); };
#endif

        experiments = listExperiments( rootFolder  );
        if(!experiments) error("No valid experiment in", rootFolder.name());
        update();
        window.show();
    }

    // Recursively appends all folders containing frames
    array<unique<Experiment> > listExperiments(const Folder& folder) {
        array<unique<Experiment>> experiments;
        for(string subfolder: folder.list(Folders|Sorted)) experiments << listExperiments(Folder(subfolder, folder));
        map<int64, String> frames = listFrames(folder);
        if(frames) experiments << unique<Experiment>(folder,move(frames));
        return experiments;
    }

    map<int64, String> listFrames(const Folder& folder) {
        map<int64, String> frames;
        for(String& name: folder.list(Files|Sorted)) {
            // Parses file name for frame timestamp (skips files on any name format mismatch)
            TextData s (name);
            if(!s.match("data_"_)) continue;
            Date date;
            date.year = 2000 + fromInteger(s.read(2));
            date.month = fromInteger(s.read(2));
            date.day = fromInteger(s.read(2));
            if(!s.match("_"_)) continue;
            date.hours = fromInteger(s.read(2));
            date.minutes = fromInteger(s.read(2));
            date.seconds = fromInteger(s.read(2));
            if(!s.match("."_)) continue;
            int milliseconds = fromInteger(s.read(3));
            if(!s.match(".txt"_)) continue;
            int64 unixTime = date; // Converts calendar date to unix time
            int64 T = unixTime*1000+milliseconds;
            frames.insertSorted(T, move(name));
        }
        return frames;
    }

    void viewExperiment(Experiment& experiment) {
        experiment.process();
        if(graph) {
            window.background = Window::White;
            window.widget = &plot;
            plot.dataSets.clear();
            int64 T0 = experiment.files.keys.first(); // First timestamp
            for(uint frameIndex: range(graphStart, graphFrameCount)) {
                int64 T = experiment.files.keys[frameIndex];
                ref<float> frame = experiment.frames[frameIndex];
                map<real,real>& graph = plot.dataSets[dec(T-T0)];
                graph.reserve(frame.size);
                for(uint64 i: slice ? range(tMin*dt,min<size_t>(frame.size, tMax*dt)) : range(frame.size))
                    graph.insert(i, frame[i]);
            }
        } else {
            window.background = Window::NoBackground;
            window.widget = &motionView;
            range r = experiment.analyzedRange;
            motionView.frames = experiment.frames.slice(r.start, r.stop-r.start);
            motionView.motionVectors = experiment.motionVectors;
        }
    }

    void update() {
        Experiment& experiment = experiments[currentExperiment];
        viewExperiment(experiment);
        String title = str(experiment.folder.name(), Date(experiment.files.keys.first()/1000), "["_+dec(experiment.frames.size)+"]"_);
        if(enableLowPass) title << " Lowpass"_;
        if(enableWindowNormalization) title << " Normalize"_;
        window.setTitle(title);
        window.render();
    }

    // Resets all experiments cache on parameter changes
    void reset() {
        for(Experiment& e: experiments) e.frames.clear();
    }
} application;
