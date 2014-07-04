#include "variant.h"
#include "synthetic.h"
#include "operators.h"
#include "MLTR.h"
#include "view.h"
#include "plot.h"
#include "layout.h"
#include "window.h"

/// Returns the first divisor of \a n below √\a n
inline uint nearestDivisorToSqrt(uint n) { uint i=round(sqrt(float(n))); for(; i>1; i--) if(n%i==0) break; return i; }

/// Computes reconstruction of a synthetic sample on a series of cases with varying parameters
struct Compute {
    Plot plot; /// NMSE versus iterations plot
    map<string, Variant> parameters = parseParameters(arguments(),{"ui"_, "volumeSize"_,"projectionSize"_,"photonCounts"_,"projectionCounts"_,"method"_});
    unique<Window> window = parameters.value("ui"_, false) ? unique<Window>() : nullptr; /// User interface for reconstruction monitoring, enabled by the "ui" command line argument

    Compute() {
        const int3 volumeSize = parameters.value("volumeSize"_, int3(256)); // Reconstruction sample count along each dimensions
        int2 projectionSize = parameters.value("projectionSize"_, int2(volumeSize.z, volumeSize.z*3/4)); // Detector sample count along each dimensions. Defaults to a 4:3 detector with a resolution comparable to the reconstruction.

        // Reference sample generation
        PorousRock rock (volumeSize);
        const VolumeF referenceVolume = rock.volume();
        const float centerSSQ = sq(sub(referenceVolume,  int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2)));
        const float extremeSSQ = sq(sub(referenceVolume, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4))) + sq(sub(referenceVolume, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4)));

        const buffer<uint> photonCounts = apply(split(parameters.value("photonCounts"_,"8192,4096,2048,1024"_),','), [](string s)->uint{ return fromInteger(s); });
        const buffer<uint> projectionCounts = apply(split(parameters.value("projectionCounts"_,"128,256,512,1024"_),','), [](string s)->uint{ return fromInteger(s); });

        Folder results = "Results"_;

        // Counts missing results
        uint missing = 0;
        for(Projection::Trajectory trajectory: {Projection::Single, Projection::Double, Projection::Adaptive}) {
            for(uint rotationCount: trajectory==Projection::Adaptive?ref<uint>({2,3,5}):ref<uint>({1,2,4})) {
                for(const uint photonCount: photonCounts) {
                    for(const uint projectionCount: projectionCounts) {
                        const uint subsetSize = int(nearestDivisorToSqrt(projectionCount));
                        String parameters = str(volumeSize.x, strx(projectionSize), ref<string>({"single"_,"double"_,"adaptive"_})[int(trajectory)], rotationCount, photonCount, projectionCount, subsetSize);
                        missing += !existsFile(parameters, results);
                    }
                }
            }
        }
        uint total = 3*3*photonCounts.size*projectionCounts.size;

        Time totalTime, reconstructionTime, projectionTime, poissonTime;
        uint completed = 0;

        for(Projection::Trajectory trajectory: {Projection::Single, Projection::Double, Projection::Adaptive}) {
            for(uint rotationCount: trajectory==Projection::Adaptive?ref<uint>({2,3,5}):ref<uint>({1,2,4})) {
                bool skip = true;
                for(const uint photonCount: photonCounts) {
                    for(const uint projectionCount: projectionCounts) {
                        String parameters = str(volumeSize.x, strx(projectionSize), ref<string>({"single"_,"double"_,"adaptive"_})[int(trajectory)], rotationCount, photonCount, projectionCount);
                        skip &= existsFile(parameters, results);
                    }
                }
                if(skip) { log("Skipping trajectory",  ref<string>({"single"_,"double"_,"adaptive"_})[int(trajectory)], rotationCount, max(projectionCounts)); continue; }
                log("Trajectory", ref<string>({"single"_,"double"_,"adaptive"_})[int(trajectory)], rotationCount, max(projectionCounts));

                // Full resolution exact projection data
                Time time; projectionTime.start();
                VolumeF attenuation0 = project(rock, Projection(volumeSize, int3(projectionSize, max(projectionCounts)), trajectory, rotationCount));
                projectionTime.stop(); log("A", time);

                for(const uint photonCount: photonCounts) {
                    bool skip = true;
                    for(const uint projectionCount: projectionCounts) {
                        String parameters = str(volumeSize.x, strx(projectionSize), ref<string>({"single"_,"double"_,"adaptive"_})[int(trajectory)], rotationCount, photonCount, projectionCount);
                        skip &= existsFile(parameters, results);
                    }
                    if(skip) { log("Skipping photon count", str(volumeSize.x, strx(projectionSize), ref<string>({"single"_,"double"_,"adaptive"_})[int(trajectory)], rotationCount, photonCount)); continue; }
                    log("Photon count", photonCount);

                    // Full resolution poisson projection data
                    VolumeF intensity0 (attenuation0.size);
                    Time time; poissonTime.start();
                    for(uint i: range(intensity0.data.size)) intensity0.data[i] = (poisson(photonCount) * exp(-attenuation0.data[i])) / float(photonCount);
                    poissonTime.stop(); log("Poisson", time);

                    for(const uint projectionCount: projectionCounts) {
                        const uint subsetSize = int(nearestDivisorToSqrt(projectionCount));
                        String parameters = str(volumeSize.x, strx(projectionSize), ref<string>({"single"_,"double"_,"adaptive"_})[int(trajectory)], rotationCount, photonCount, projectionCount);
                        if(existsFile(parameters, results)) { log("Skipping projectionCount", parameters); continue; }
                        log(str(completed)+"/"_+str(missing)+"/"_+str(total));
                        log(parameters);

                        // Partial resolution poisson projection data
                        VolumeF hostIntensity (int3(projectionSize, projectionCount), 0, "b"_);
                        assert_(intensity0.size.z%hostIntensity.size.z==0, intensity0.size.z, hostIntensity.size.z);
                        for(uint index: range(hostIntensity.size.z)) copy(slice(hostIntensity, index).data, slice(intensity0, index*intensity0.size.z/hostIntensity.size.z).data); // FIXME: upload slice-wise instead of host copy + full upload
                        ImageArray intensity = hostIntensity; // Uploads

                        // Reconstruction
                        const Projection A (volumeSize, int3(projectionSize, projectionCount), trajectory, rotationCount);
                        MLTR reconstruction {A, intensity, subsetSize};

                        ImageArray attenuation = negln(intensity); //FIXME: skip when no UI
                        // Interface
                        Value sliceIndex = Value((volumeSize.z-1) / 2);
                        SliceView x0 {referenceVolume, max(1, 256 / referenceVolume.size.x), sliceIndex};
                        SliceView x {reconstruction.x, max(1, 256 / reconstruction.x.size.x), sliceIndex};
                        HBox slices {{&x0, &x}};

                        Value projectionIndex = Value((A.projectionSize.z-1) / 2);
                        SliceView b0 {attenuation, max(1, 256 / attenuation.size.x), projectionIndex};
                        VolumeView b {reconstruction.x, A, max(1, 256 / A.projectionSize.x), projectionIndex};
                        HBox projections {{&b0, &b}};

                        VBox layout {{&slices, &projections, &plot}};
                        if(window) {
                            window->widget = &layout;
                            window->setSize(min(int2(-1), -window->size));
                            window->setTitle(str(completed)+"/"_+str(missing)+"/"_+str(total));
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
                            reconstruction.step();

                            vec3 center = rock.largestGrain.xyz(); float r = rock.largestGrain.w;
                            int3 size = ceil(r); int3 origin = int3(round(center))-size;
                            VolumeF grain = reconstruction.x.read(2*size, origin);
                            center -= vec3(origin);
                            float sum=0; float count=0;
                            for(int z: range(grain.size.z)) for(int y: range(grain.size.y)) for(int x: range(grain.size.x))  if(sq(vec3(x,y,z)-center)<=sq(r-1./2)) sum += grain(x,y,z), count++;
                            float mean = sum / count; float deviation = 0;
                            for(int z: range(grain.size.z)) for(int y: range(grain.size.y)) for(int x: range(grain.size.x))  if(sq(vec3(x,y,z)-center)<=sq(r-1./2)) deviation += abs(grain(x,y,z)-mean);
                            deviation /= count;
                            float SNR = mean/deviation;

                            float centerSSE = ::SSE(referenceVolume, reconstruction.x, int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2));
                            float extremeSSE = ::SSE(referenceVolume, reconstruction.x, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4)) + ::SSE(referenceVolume, reconstruction.x, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4));
                            float totalNMSE = (centerSSE+extremeSSE)/(centerSSQ+extremeSSQ);
                            result << str(k, 100*centerSSE/centerSSQ, 100*extremeSSE/extremeSSQ, 100*totalNMSE, SNR)+"\n"_;
                            plot[parameters].insert(k, -10*log10(totalNMSE));
                            if(window) {
                                window->needRender = true;
                                window->event();
                            }
                            if(centerSSE + extremeSSE < bestCenterSSE + bestExtremeSSE) {
                                bestK=k;
                                reconstruction.x.read(best);
                                if(k >= maxIterationCount-1) log("Slow convergence stopped after maximum iteration count");
                            } else {
                                if(k >= minIterationCount-1) { log("Divergence stopped after minimum iteration count"); break; }
                            }
                            bestCenterSSE = min(bestCenterSSE, centerSSE), bestExtremeSSE = min(bestExtremeSSE, extremeSSE), bestSNR = max(bestSNR, SNR);

                            extern bool terminate;
                            if(terminate) { log("Terminated"_); return; }
                        }
                        reconstructionTime.stop();
                        writeFile(parameters, result, results);
                        writeFile(parameters+".best"_, cast<byte>(best.data), results);
                        log(bestK, 100*bestCenterSSE/centerSSQ, 100*bestExtremeSSE/extremeSSQ, 100*(bestCenterSSE+bestExtremeSSE)/(centerSSQ+extremeSSQ), bestSNR, time);
                        completed++;
                    }
                    assert_(CLMem::handleCount == 0, "Holding OpenCL MemObjects after completion"); // Asserts all MemObjects have been released, as this single process runs all cases (to reuse projection data and monitor window).
                }
            }
        }
        log(projectionTime, poissonTime, reconstructionTime, totalTime, completed, total, missing, totalTime/missing);
        exit();
    }
} app; // Static object constructors executes before main. In this application, events are processed explicitly by calling window->event() in the innermost loop instead of using the event loop in main() as it makes parameter sweeps easier to write.