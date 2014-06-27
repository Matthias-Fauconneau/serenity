#include "variant.h"
#include "synthetic.h"
#include "operators.h"
#include "MLTR.h"
#include "view.h"
#include "plot.h"
#include "layout.h"
#include "window.h"

struct Application : Poll {
    // Parameters
    map<string, Variant> parameters = parseParameters(arguments(),{"volumeSize"_,"projectionSize"_,"trajectory"_,"rotationCount"_,"photonCount"_,"method"_,"subsetSize"_});
    const int3 volumeSize = fromInt3(parameters.at("volumeSize"_));
    const int3 projectionSize = fromInt3(parameters.at("projectionSize"_));
    const Projection::Trajectory trajectory = Projection::Trajectory(ref<string>({"single"_,"double"_,"adaptive"_}).indexOf(parameters.at("trajectory"_)));
    const uint rotationCount = fromInteger(parameters.at("rotationCount"_));
    const float photonCount = fromInteger(parameters.at("photonCount"_));
    //const string method = parameters.at("method"_);
    const uint subsetSize = fromInteger(parameters.at("subsetSize"_));
    const uint iterationCount = parameters.value("iterationCount"_,64);

    // Reference volume
    PorousRock rock {volumeSize, parameters.value("radius"_, 16.f)};
    const VolumeF referenceVolume = rock.volume();
    const float centerSSQ = sq(sub(referenceVolume,  int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2)));
    const float extremeSSQ = sq(sub(referenceVolume, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4))) + sq(sub(referenceVolume, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4)));

    // Projection
    Projection projection {volumeSize, projectionSize, trajectory, rotationCount, photonCount};
    const ImageArray intensity = project(rock, projection);
    const ImageArray attenuation = negln(intensity);

    // Reconstruction
    unique<SubsetReconstruction> reconstruction = unique<MLTR>(projection, intensity, subsetSize);

    // Interface
    int upsample = max(1, 256 / projectionSize.x);

    Value sliceIndex = Value((volumeSize.z-1) / 2);
    SliceView x0 {referenceVolume, upsample, sliceIndex};
    SliceView x {reconstruction->x, upsample, sliceIndex};
    HBox slices {{&x0, &x}};

    Value projectionIndex = Value((projectionSize.z-1) / 2);
    SliceView b0 {attenuation, upsample, projectionIndex};
    VolumeView b {reconstruction->x, Projection(volumeSize, projectionSize, Projection::Single, 1), upsample, projectionIndex};
    HBox projections {{&b0, &b}};

    Plot plot;

    VBox layout {{&slices, &projections, &plot}};
    Window window {&layout, str(reconstruction)};

    Application() { assert_(iterationCount); queue(); }
    ~Application() { log(reconstruction, centerSSQ, extremeSSQ); }

    void event() {
        Reconstruction& r = reconstruction;
        r.step();
        float centerSSE = ::SSE(referenceVolume, r.x, int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2));
        float extremeSSE = ::SSE(referenceVolume, r.x, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4)) + ::SSE(referenceVolume, r.x, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4));
        if(r.k == iterationCount && (centerSSE < r.centerSSE || extremeSSE < r.extremeSSE)) log("WARNING: Slow convergence stopped by low iteration count");
        r.centerSSE = centerSSE; r.bestCenterSSE = min(r.bestCenterSSE, centerSSE);
        r.extremeSSE = extremeSSE; r.bestExtremeSSE = min(r.bestExtremeSSE, extremeSSE);
        if(centerSSE + extremeSSE < r.centerSSE + r.extremeSSE) r.bestK=r.k;
        log(str(r,centerSSQ, extremeSSQ));
        writeFile(str(r), bestNMSE(r, centerSSQ, extremeSSQ), Folder("Results"_));
        float NMSE = (r.bestCenterSSE+r.bestExtremeSSE)/(centerSSQ+extremeSSQ);
        plot[""_].insertMulti(/*r.time/1000000000.f*/r.k, NMSE);
        window.render();
        if(r.k < iterationCount)  queue();
    }
} app;
