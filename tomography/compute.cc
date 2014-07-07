#include "variant.h"
#include "synthetic.h"
#include "operators.h"
#include "SART.h"
#include "MLTR.h"
#include "CG.h"
#include "view.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "png.h"

/// Returns the first divisor of \a n below √\a n
inline uint nearestDivisorToSqrt(uint n) { uint i=round(sqrt(float(n))); for(; i>1; i--) if(n%i==0) break; return i; }

/// Computes reconstruction of a synthetic sample on a series of cases with varying parameters
struct Compute {
    Plot plot; /// NMSE versus iterations plot
    map<string, Variant> parameters = parseParameters(arguments(),{"ui"_,"reference"_,"volumeSize"_,"projectionSize"_,"trajectory"_,"rotationCount"_,"photonCount"_,"projectionCount"_,"method"_,"subsetSize"_});
    unique<Window> window = parameters.value("ui"_, false) ? unique<Window>() : nullptr; /// User interface for reconstruction monitoring, enabled by the "ui" command line argument

    Compute() {
        const int3 volumeSize = parameters.value("volumeSize"_, int3(256)); // Reconstruction sample count along each dimensions
        int2 projectionSize = parameters.value("projectionSize"_, int2(volumeSize.z, volumeSize.z*3/4)); // Detector sample count along each dimensions. Defaults to a 4:3 detector with a resolution comparable to the reconstruction.

        // Reference sample generation
        PorousRock rock (volumeSize);
        const VolumeF referenceVolume = rock.volume();
        if(parameters.value("reference"_, false)) {
            Folder folder ("Reference"_, currentWorkingDirectory(), true);
            Image target ( referenceVolume.size.xy() );
            for(uint z: range(referenceVolume.size.z)) {
                convert(target,slice(referenceVolume,z)); // Normalizes each slice by its maximum value
                writeFile(dec(z), encodePNG(target),  folder);
            }
            exit();
            return;
        }
        const float centerSSQ = sq(sub(referenceVolume,  int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2)));
        const float extremeSSQ = sq(sub(referenceVolume, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4))) + sq(sub(referenceVolume, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4)));

        // Explicits configurations
        array<Dict> configurations;
        for(string trajectory: split(parameters.value("trajectory"_,"single,double,adaptive"_),',')) {
            for(uint rotationCount: apply(split(parameters.value("rotationCount"_,"4,2,1"_),','), [](string s)->uint{ return fromInteger(s); })) {
                if(trajectory=="adaptive") rotationCount = rotationCount + 1;
                for(const uint photonCount: apply(split(parameters.value("photonCount"_,"8192,4096,2048"_),','), [](string s)->uint{ return fromInteger(s); })) {
                    for(const uint projectionCount: apply(split(parameters.value("projectionCount"_,"128,256,512"_),','), [](string s)->uint{ return fromInteger(s); })) {
                        for(const string method: split(parameters.value("method"_,"SART,MLTR,CG"_),',')) {
                            for(const uint subsetSize: method=="CG"_?ref<uint>({projectionCount}):ref<uint>({nearestDivisorToSqrt(projectionCount), nearestDivisorToSqrt(projectionCount)*2, projectionCount})) {
                                Dict configuration;
                                configuration["volumeSize"_] = volumeSize;
                                configuration["projectionSize"_] = projectionSize;
                                configuration["trajectory"_] = trajectory;
                                configuration["rotationCount"_] = rotationCount;
                                configuration["photonCount"_] = photonCount;
                                configuration["projectionCount"_] = projectionCount;
                                configuration["method"_] = method;
                                configuration["subsetSize"_] = subsetSize;
                                configurations << move(configuration);
                            }
                        }
                    }
                }
            }
        }

        Folder results = "Results"_;

        // Counts missing results
        uint missing = sum(apply(configurations,[&](const Dict& configuration){ return !existsFile(toASCII(configuration), results); }));
        uint total = configurations.size;

        Time totalTime, reconstructionTime, projectionTime, poissonTime;
        uint completed = 0;

        // Caches projection data between test configurations
        String attenuation0Key; VolumeF attenuation0;
        String intensity0Key; VolumeF intensity0;
        String intensityKey; VolumeF intensity;/*MLTR*/ VolumeF attenuation;/*UI, SART, CG*/
        uint maxProjectionCount=0; for(const Dict& configuration: configurations) maxProjectionCount=max<uint>(maxProjectionCount,configuration["projectionCount"_]);
        for(const Dict& configuration: configurations) {
            if(existsFile(toASCII(configuration), results)) continue;
            log(configuration);

            // Configuration parameters
            int3 volumeSize = configuration["volumeSize"_];
            int2 projectionSize = configuration["projectionSize"_];
            Trajectory trajectory = Trajectory(ref<string>({"single"_,"double"_,"adaptive"_}).indexOf(configuration["trajectory"_]));
            uint rotationCount = configuration["rotationCount"_];
            uint photonCount = configuration["photonCount"_];
            uint projectionCount = configuration["projectionCount"_];
            string method = configuration["method"_];
            uint subsetSize = configuration["subsetSize"_];

            {// Full resolution exact projection data
                String key = str(volumeSize, int3(projectionSize, maxProjectionCount), trajectory, rotationCount);
                if(attenuation0Key != key) { // Cache miss
                    log_("Analytic projection ["_+str(maxProjectionCount)+"]... "_);
                    Time time; projectionTime.start();
                    attenuation0 = project(rock, Projection(volumeSize, int3(projectionSize, maxProjectionCount), trajectory, rotationCount));
                    projectionTime.stop(); log(time);
                    attenuation0Key = move(key);
                }}

            {// Full resolution poisson projection data
                String key = str(volumeSize, int3(projectionSize, maxProjectionCount), trajectory, rotationCount, photonCount);
                if(intensity0Key != key) { // Cache miss
                    intensity0 = VolumeF(attenuation0.size);
                    log_("Poisson noise ["_+str(maxProjectionCount)+"]... "_);
                    Time time; poissonTime.start();
                    for(uint i: range(intensity0.data.size)) intensity0.data[i] = (poisson(photonCount) * exp(-attenuation0.data[i])) / float(photonCount);
                    poissonTime.stop(); log(time);
                    intensity0Key = move(key);
                }}

            {// Partial resolution poisson projection data
                String key = str(volumeSize, int3(projectionSize, maxProjectionCount), trajectory, rotationCount, photonCount, projectionCount);
                if(intensityKey != key) { // Cache miss
                    intensity = VolumeF(int3(projectionSize, projectionCount), 0, "b"_);
                    assert_(intensity0.size.z%intensity.size.z==0, intensity0.size.z, intensity.size.z);
                    for(uint index: range(intensity.size.z)) copy(slice(intensity, index).data, slice(intensity0, index*intensity0.size.z/intensity.size.z).data); // Decimates projection data slices (FIXME: upload slice-wise instead of host copy + full upload)
                    attenuation = VolumeF(int3(projectionSize, projectionCount), 0, "-ln b"_);
                    for(uint i: range(intensity.size.z*intensity.size.y*intensity.size.x)) attenuation[i] = -ln(intensity[i]);
                    intensityKey = move(key);
                }}

            {// Reconstruction
                const Projection A (volumeSize, int3(projectionSize, projectionCount), trajectory, rotationCount);
                unique<Reconstruction> reconstruction = nullptr;
                if(method=="SART"_) reconstruction = unique<SART>(A, attenuation, subsetSize);
                if(method=="MLTR"_) reconstruction = unique<MLTR>(A, intensity, subsetSize);
                if(method=="CG"_) reconstruction = unique<CG>(A, attenuation);
                assert_(reconstruction, method);

                // Interface
                Value sliceIndex = Value((volumeSize.z-1) / 2);
                SliceView x0 {referenceVolume, max(1, 256 / referenceVolume.size.x), sliceIndex};
                SliceView x {reconstruction->x, max(1, 256 / reconstruction->x.size.x), sliceIndex};
                HBox slices {{&x0, &x}};

                Value projectionIndex = Value((A.projectionSize.z-1) / 2);
                SliceView b0 {attenuation, max(1, 256 / attenuation.size.x), projectionIndex};
                VolumeView b {reconstruction->x, A, max(1, 256 / A.projectionSize.x), projectionIndex};
                HBox projections {{&b0, &b}};

                VBox layout {{&slices, &projections, &plot}};
                if(window) {
                    window->widget = &layout;
                    window->setSize(min(int2(-1), -window->size));
                    window->setTitle(str(completed)+"/"_+str(missing)+"/"_+str(total)+" "_+str(configuration));
                    window->show();
                }

                // Evaluation
                uint bestK = 0;
                float bestCenterSSE = inf, bestExtremeSSE = inf, bestSNR = 0;
                VolumeF best (volumeSize);
                Time time; reconstructionTime.start();
                const uint minIterationCount = 16, maxIterationCount = 512;
                String result;
                uint k=0; for(;k < maxIterationCount; k++) {
                    reconstruction->step();

                    vec3 center = rock.largestGrain.xyz(); float r = rock.largestGrain.w;
                    int3 size = ceil(r); int3 origin = int3(round(center))-size;
                    VolumeF grain = reconstruction->x.read(2*size, origin);
                    center -= vec3(origin);
                    float sum=0; float count=0;
                    for(int z: range(grain.size.z)) for(int y: range(grain.size.y)) for(int x: range(grain.size.x))  if(sq(vec3(x,y,z)-center)<=sq(r-1./2)) sum += grain(x,y,z), count++;
                    float mean = sum / count; float deviation = 0;
                    for(int z: range(grain.size.z)) for(int y: range(grain.size.y)) for(int x: range(grain.size.x))  if(sq(vec3(x,y,z)-center)<=sq(r-1./2)) deviation += abs(grain(x,y,z)-mean);
                    deviation /= count;
                    float SNR = mean/deviation;

                    float centerSSE = ::SSE(referenceVolume, reconstruction->x, int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2));
                    float extremeSSE = ::SSE(referenceVolume, reconstruction->x, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4)) + ::SSE(referenceVolume, reconstruction->x, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4));
                    float totalNMSE = (centerSSE+extremeSSE)/(centerSSQ+extremeSSQ);
                    result << str(k, 100*centerSSE/centerSSQ, 100*extremeSSE/extremeSSQ, 100*totalNMSE, SNR, time.toFloat())+"\n"_;
                    plot[str(configuration)].insert(k, -10*log10(totalNMSE));
                    if(window) {
                        window->needRender = true;
                        window->event();
                    }
                    if(centerSSE + extremeSSE < bestCenterSSE + bestExtremeSSE) {
                        bestK=k;
                        reconstruction->x.read(best);
                        if(k >= maxIterationCount-1) log("Slow convergence stopped after maximum iteration count");
                    } else {
                        if(k >= minIterationCount-1 && k>2*bestK) { log("Divergence stopped after minimum iteration count"); break; }
                    }
                    bestCenterSSE = min(bestCenterSSE, centerSSE), bestExtremeSSE = min(bestExtremeSSE, extremeSSE), bestSNR = max(bestSNR, SNR);

                    extern bool terminate;
                    if(terminate) { log("Terminated"_); return; }
                }
                reconstructionTime.stop();
                writeFile(toASCII(configuration), result, results);
                writeFile(toASCII(configuration)+".best"_, cast<byte>(best.data), results);
                log(bestK, 100*bestCenterSSE/centerSSQ, 100*bestExtremeSSE/extremeSSQ, 100*(bestCenterSSE+bestExtremeSSE)/(centerSSQ+extremeSSQ), bestSNR, time);
            }
            completed++;
            assert_(CLMem::handleCount == 0, "Holding OpenCL MemObjects after completion"); // Asserts all MemObjects have been released, as this single process runs all cases (to reuse projection data and monitor window).
        }
        log(projectionTime, poissonTime, reconstructionTime, totalTime, completed, total, missing, totalTime/missing);
        exit();
    }
} app; // Static object constructors executes before main. In this application, events are processed explicitly by calling window->event() in the innermost loop instead of using the event loop in main() as it makes parameter sweeps easier to write.
