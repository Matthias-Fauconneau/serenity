#include "variant.h"
#include "synthetic.h"
#include "operators.h"
#include "MLTR.h"
#include "view.h"
#include "plot.h"
#include "layout.h"
#include "window.h"

inline uint nearestDivisorToSqrt(uint n) { uint i=round(sqrt(float(n))); for(; i>1; i--) if(n%i==0) break; return i; }

struct Application {
    Plot plot;
    Window window;
    Application() {
        Time totalTime, totalReconstructionTime;
        uint completed = 0;
        // Reference parameters
        const int3 volumeSize = 256;
        const uint grainRadius = 16;
        // Reference
        PorousRock rock (volumeSize, grainRadius);
        const VolumeF referenceVolume = rock.volume();
        const float centerSSQ = sq(sub(referenceVolume,  int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2)));
        const float extremeSSQ = sq(sub(referenceVolume, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4))) + sq(sub(referenceVolume, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4)));

        // Projection parameters
        const uint projectionWidth = volumeSize.z;
        for(Projection::Trajectory trajectory: {Projection::Single, Projection::Double, Projection::Adaptive}) {
            for(uint rotationCount: trajectory==Projection::Adaptive?ref<uint>({2,3,5}):ref<uint>({1,2,4})) {
                // Full resolution exact projection data
                int3 projectionSize0 (projectionWidth,projectionWidth*3/4, 512);
                const Projection A0 (volumeSize, projectionSize0, trajectory, rotationCount, 0);
                VolumeF attenuation0 = project(rock, A0);

                for(const uint photonCount: ref<uint>({256,512,1024})) {
                    // Full resolution poisson projection data
                    VolumeF intensity0 (attenuation0.size);
                    {Time time;
                        for(uint i: range(intensity0.data.size)) intensity0.data[i] = poisson(photonCount) / photonCount * exp(-attenuation0.data[i]); // TODO: precompute poisson
                        log("Poisson", time);}

                    for(const uint projectionCount: ref<uint>({64,128,256,512,1024})) {
                        int3 projectionSize (projectionWidth,projectionWidth*3/4, projectionCount);
                        const Projection A (volumeSize, projectionSize, trajectory, rotationCount, photonCount);
                        const uint subsetSize = int(nearestDivisorToSqrt(projectionSize.z));

                        String parameters = str(A.volumeSize.x, grainRadius, strx(A.projectionSize.xy()), ref<string>({"single"_,"double"_,"adaptive"_})[int(A.trajectory)], A.rotationCount, uint(A.photonCount), projectionCount, subsetSize);
                        Folder results = "Results"_;
                        if(existsFile(parameters, results)) { log("WARNING: Skipping", parameters); continue; }
                        log(parameters);
                        File resultFile (parameters, results, Flags(WriteOnly|Create|Truncate));

                        // Partial resolution poisson projection data
                        VolumeF hostIntensity (A.projectionSize, 0, "b"_);
                        assert_(intensity0.size.z%hostIntensity.size.z==0);
                        for(uint index: range(hostIntensity.size.z)) copy(slice(hostIntensity, index).data, slice(intensity0, index*intensity0.size.z/hostIntensity.size.z).data); // FIXME: upload slice-wise instead of host copy + full upload
                        ImageArray intensity = hostIntensity; // Uploads
                        ImageArray attenuation = negln(intensity);

                        // Reconstruction
                        MLTR reconstruction {A, intensity, subsetSize};

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
                        window.widget = &layout;
                        window.setSize(min(int2(-1), -window.size));
                        window.setTitle(str(completed)+"/"_+str(3*5*4*2/*120*/));
                        window.show();

                        // Evaluation
                        uint bestK = 0;
                        float bestCenterSSE = inf, bestExtremeSSE = inf, bestSNR = 0;
                        VolumeF best (volumeSize);
                        Time time; totalReconstructionTime.start();
                        const uint minIterationCount = 16, maxIterationCount = 128;
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
                            resultFile.write(str(k, 100*centerSSE/centerSSQ, 100*extremeSSE/extremeSSQ, 100*totalNMSE, SNR)+"\n"_);
                            plot[parameters].insert(k, -10*log10(totalNMSE));
                            window.needRender = true;
                            window.event();

                            if(centerSSE + extremeSSE < bestCenterSSE + bestExtremeSSE) {
                                bestK=k;
                                reconstruction.x.read(best);
                                if(k >= maxIterationCount-1) log("WARNING: Slow convergence stopped after maximum iteration count");
                            } else if(k >= minIterationCount-1) { log("WARNING: Divergence stopped after minimum iteration count"); break; }
                            bestCenterSSE = min(bestCenterSSE, centerSSE), bestExtremeSSE = min(bestExtremeSSE, extremeSSE), bestSNR = max(bestSNR, SNR);

                            if(totalNMSE > 1) { log("WARNING: Divergence stopped after large error"); break; }

                            extern bool terminate;
                            if(terminate) { log("Terminated"_); return; }
                        }
                        totalReconstructionTime.stop();
                        writeFile(parameters+".best"_, cast<byte>(best.data), results);
                        log(bestK, 100*bestCenterSSE/centerSSQ, 100*bestExtremeSSE/extremeSSQ, 100*(bestCenterSSE+bestExtremeSSE)/(centerSSQ+extremeSSQ), bestSNR, time);
                        completed++;
                    }
                    assert_(CLMem::handleCount == 0);
                }
            }
        }
        log("Done"_,totalReconstructionTime, totalTime);
        exit();
    }
} app;
