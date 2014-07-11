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
#include "deflate.h"

/// Returns the first divisor of \a n below √\a n
inline uint nearestDivisorToSqrt(uint n) { uint i=round(sqrt(float(n))); for(; i>1; i--) if(n%i==0) break; return i; }

/// Computes reconstruction of a synthetic sample on a series of cases with varying parameters
struct Compute {
    Plot plot; /// NMSE versus iterations plot
    map<string, Variant> parameters = parseParameters(arguments(),{"ui"_,"reference"_,"update"_,"volumeSize"_,"projectionSize"_,"trajectory"_,"rotationCount"_,"photonCount"_,"projectionCount"_,"method"_,"subsetSize"_});
    unique<Window> window = parameters.value("ui"_, false) ? unique<Window>() : nullptr; /// User interface for reconstruction monitoring, enabled by the "ui" command line argument

    Compute() {
        const int3 volumeSize = parameters.value("volumeSize"_, int3(256)); // Reconstruction sample count along each dimensions
        int2 projectionSize = parameters.value("projectionSize"_, int2(volumeSize.z, volumeSize.z*3/4)); // Detector sample count along each dimensions. Defaults to a 4:3 detector with a resolution comparable to the reconstruction.

        // Reference sample generation
        PorousRock rock (volumeSize);
        const VolumeF referenceVolume = rock.volume();
        if(parameters.value("reference"_, false)) {
            {Folder folder ("Reference"_, currentWorkingDirectory(), true);
                Image target ( referenceVolume.size.xy() );
                for(uint z: range(referenceVolume.size.z)) {
                    convert(target,slice(referenceVolume,z)); // Normalizes each slice by its maximum value
                    writeFile(dec(z), encodePNG(target),  folder);
                }}
            {Folder folder ("Projection"_, currentWorkingDirectory(), true);
                const Projection A (volumeSize, int3(projectionSize, volumeSize.z), Single, 1);
                ImageF targetf ( A.projectionSize.xy() );
                Image target ( A.projectionSize.xy() );
                for(uint index: range(A.projectionSize.z)) {
                    log(index);
                    rock.project(targetf, A, index);
                    convert(target,targetf); // Normalizes each slice by its maximum value
                    writeFile(dec(index), encodePNG(target), folder);
                }}
            exit();
            return;
        }
        const float centerSSQ = sq(sub(referenceVolume,  int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2)));
        const float extremeSSQ = sq(sub(referenceVolume, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4))) + sq(sub(referenceVolume, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4)));

        // Explicits configurations
        array<Dict> configurations;
        for(string rotationCountParameter: split(parameters.value("rotationCount"_,"optimal,4,2,1"_),',')) {
            for(string trajectory: split(parameters.value("trajectory"_,"single,double,adaptive"_),',')) {
                if(rotationCountParameter=="optimal"_ && trajectory!="single"_) continue;
                for(const uint photonCount: apply(split(parameters.value("photonCount"_,"8192,4096,2048,0"_),','), [](string s)->uint{ return fromInteger(s); })) {
                    for(const uint projectionCount: apply(split(parameters.value("projectionCount"_,"128,256,512"_),','), [](string s)->uint{ return fromInteger(s); })) {
                        float rotationCount;
                        if(rotationCountParameter=="optimal"_) rotationCount = (sqrt(16*PI*projectionCount) - 1) / (4*PI);
                        else rotationCount = fromDecimal(rotationCountParameter);
                        if(trajectory=="adaptive"_) rotationCount = rotationCount + 1;
                        for(const string method: split(parameters.value("method"_,"SART,MLTR,CG"_),',')) {
                            uint subsetSize;
                            /**/  if(method=="CG"_) subsetSize = projectionCount;
                            else if(method=="SART"_) subsetSize = nearestDivisorToSqrt(projectionCount) * 2;
                            else /*method=="MLTR"_*/ subsetSize = nearestDivisorToSqrt(projectionCount);
                            Dict configuration;
                            configuration["volumeSize"_] = volumeSize;
                            configuration["projectionSize"_] = projectionSize;
                            configuration["trajectory"_] = trajectory;
                            if(round(rotationCount)==rotationCount) configuration["rotationCount"_] = int(rotationCount); // Backward compatibility: Avoids updating results computed as rotationCount was integer
                            else configuration["rotationCount"_] = rotationCount;
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

        // Filters configuration requiring an update
        Folder results = "Results"_;
        int64 updateTime = realTime() - 7*24*60*60*1000000000ull; // Updates any old results (default)
        if(parameters.value("update"_,""_)=="missing"_) updateTime = 0; // Updates missing results
        if(parameters.value("update"_,""_)=="all"_) updateTime = realTime();  // Updates all results
        map<int64, Dict> update;
        for(const Dict& configuration: configurations) {
            int64 mtime = existsFile(toASCII(configuration), results) ? File(toASCII(configuration), results).modifiedTime() : 0;
            bool best = existsFile(toASCII(configuration)+".best"_, results);
            if(mtime <= updateTime || (!best && parameters.value("update"_,""_)=="best"_)) update.insertSortedMulti(mtime, copy(configuration)); // Updates oldest evaluation first
        }

        Time totalTime, reconstructionTime, projectionTime, poissonTime;
        uint completed = 0;

        // Caches projection data between test configurations
        String attenuation0Key; VolumeF attenuation0;
        String intensity0Key; VolumeF intensity0;
        String intensityKey; VolumeF intensity;/*MLTR*/ VolumeF attenuation;/*UI, SART, CG*/
        uint maxProjectionCount=0; for(const Dict& configuration: update.values) maxProjectionCount=max<uint>(maxProjectionCount,configuration.at("projectionCount"_));
        log("Missing", update.size(), "on total", configurations.size);

        // Checks available disk space
        int64 volumeByteSize = volumeSize.x*volumeSize.y*volumeSize.z*sizeof(float);
        if(available(results) < update.size()*volumeByteSize) log("Warning: Currently available space will only fit",available(results)/volumeByteSize,"/",update.size(), "reconstructions");

        for(const Dict& configuration: update.values) {
            uint index = configurations.indexOf(configuration); // Original index for total progress report
            log(str(completed)+"/"_+str(update.size()), index, configuration);
            if(available(results) < volumeByteSize) log("Warning: Currently available space will not fit reconstruction");

            // Configuration parameters
            int3 volumeSize = configuration["volumeSize"_];
            int2 projectionSize = configuration["projectionSize"_];
            Trajectory trajectory = Trajectory(ref<string>({"single"_,"double"_,"adaptive"_}).indexOf(configuration["trajectory"_]));
            float rotationCount = configuration["rotationCount"_];
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
                    intensity0 = VolumeF(attenuation0.size, "intensity0"_);
                    log_("Poisson noise ["_+str(maxProjectionCount)+"]... "_);
                    Time time; poissonTime.start();
                    if(photonCount) for(uint i: range(intensity0.data.size)) intensity0.data[i] = (poisson(photonCount) * exp(-attenuation0.data[i])) / float(photonCount);
                    else for(uint i: range(intensity0.data.size)) intensity0.data[i] = exp(-attenuation0.data[i]);
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
                    window->setTitle(str(completed)+"/"_+str(update.size())+" "_+str(index)+"/"_+str(configurations.size)+" "_+str(configuration));
                    window->show();
                }

                // Evaluation
                uint bestK = 0;
                float bestCenterSSE = inf, bestExtremeSSE = inf;
                VolumeF best (volumeSize, "best"_);
                Time time; reconstructionTime.start();
                const uint minIterationCount = 32, maxIterationCount = 512;
                array<map<string, Variant>> result;
                uint k=0; for(;k < maxIterationCount;k++) {
                    // Evaluates before stepping as initial volume might be the best if the method does not converge at all
                    float centerSSE = ::SSE(referenceVolume, reconstruction->x, int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2));
                    float extremeSSE = ::SSE(referenceVolume, reconstruction->x, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4)) + ::SSE(referenceVolume, reconstruction->x, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4));
                    float totalNMSE = (centerSSE+extremeSSE)/(centerSSQ+extremeSSQ);
                    {map<string, Variant> values;
                        values["Iterations"_] = k;
                        values["Iterations·Projection/Subsets"_] = k * subsetSize;
                        values["Central NMSE %"_] = 100*centerSSE/centerSSQ;
                        values["Extreme NMSE %"_] = 100*extremeSSE/extremeSSQ;
                        values["Total NMSE %"_] = 100*totalNMSE;
                        values["Time (s)"_] = time.toFloat();
                        result << move(values);}
                    plot[str(configuration)].insert(k, -10*log10(totalNMSE));
                    if(window) {
                        window->needRender = true;
                        window->event();
                    }
                    if(centerSSE + extremeSSE < bestCenterSSE + bestExtremeSSE) { bestK=k; reconstruction->x.read(best); }
                    else if(centerSSE < bestCenterSSE || extremeSSE < bestExtremeSSE) {} // Keep running if any region is still converging
                    else if(k >= minIterationCount-1 /*&& k>2*bestK*/) { log("Divergence stopped after", k, "iterations"); break; }
                    if(k >= maxIterationCount-1) { log("Slow convergence stopped after maximum iteration count"); break; }
                    bestCenterSSE = min(bestCenterSSE, centerSSE), bestExtremeSSE = min(bestExtremeSSE, extremeSSE);

                    reconstruction->step();
                }
                reconstructionTime.stop();
                // Stores statistics for all iterations
                assert_(str(result, '\n'));
                writeFile(toASCII(configuration), str(result, '\n'), results);
                // Stores best reconstruction
                {buffer<byte> data = deflate(cast<byte>(best.data)); //FIXME: Inefficient on raw float data (only useful mostly to skips zeroes (⅔ compression))
                    assert_(data);
                    if(available(results) < (int64)data.size) log("Not enough available disk space for reconstruction");
                    else writeFile(toASCII(configuration)+".best"_, data, results);
                }
                { // Stores 2 slices of the best reconstruction
                    Image target ( best.size.xy() );
                    {int z=volumeSize.z/2; convert(target,slice(best,z)); writeFile(toASCII(configuration)+"."_+str(z), encodePNG(target), results); }
                    {int z=volumeSize.z/8; convert(target,slice(best,z)); writeFile(toASCII(configuration)+"."_+str(z), encodePNG(target), results); }
                }
                log(bestK, 100*bestCenterSSE/centerSSQ, 100*bestExtremeSSE/extremeSSQ, 100*(bestCenterSSE+bestExtremeSSE)/(centerSSQ+extremeSSQ), time);
                if(window) window->widget = 0;
            }
            completed++;
            assert_(CLMem::handleCount == 0, "Holding OpenCL MemObjects after completion", CLMem::handleCount); // Asserts all MemObjects have been released, as this single process runs all cases (to reuse projection data and monitor window).
            extern bool terminate;
            if(terminate) { log("Terminated"_); break; }
        }
        log("Total", totalTime, "Projection", projectionTime, "Poisson", poissonTime, "Reconstruction", reconstructionTime, "Completed", completed, "from", update.size(), "on total", configurations.size, "in average", totalTime.toFloat()/update.size(), "s/configuration");
        exit();
    }
} app; // Static object constructors executes before main. In this application, events are processed explicitly by calling window->event() in the innermost loop instead of using the event loop in main() as it makes parameter sweeps easier to write.
