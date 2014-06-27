#include "variant.h"
#include "synthetic.h"
#include "operators.h"
#include "MLTR.h"
#include "view.h"
#include "plot.h"
#include "layout.h"
#include "window.h"

struct Application {
    Plot plot;
    //map<string, Variant> parameters = parseParameters(arguments(),{"volumeSize"_,"radius"_,"projectionSize"_,"trajectory"_,"rotationCount"_,"photonCount"_,"method"_,"subsetSize"_}); TODO: parse sequences
    Application() {
        for(const int3 volumeSize: apply(range(6,9 +1), &exp2)) {
            for(const uint grainRadius:  apply(range(4,8 +1), &exp2)) {
                // Reference volume
                PorousRock rock (volumeSize, grainRadius);
                const VolumeF referenceVolume = rock.volume();
                const float centerSSQ = sq(sub(referenceVolume,  int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2)));
                const float extremeSSQ = sq(sub(referenceVolume, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4))) + sq(sub(referenceVolume, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4)));

                for(const uint projectionWidth:  apply(range(4,9 +1), &exp2)) {
                    for(Projection::Trajectory trajectory: {Projection::Single, Projection::Double, Projection::Adaptive}) {
                        for(uint rotationCount: range(1,5 +1)) {
                            for(const uint photonCount: apply(range(8,16 +1), &exp2)) {
                                for(const uint projectionCount:  apply(range(4,9 +1), &exp2)) {
                                    int3 projectionSize (projectionWidth,projectionWidth*3/4, projectionCount);
                                    // Projection
                                    const Projection A (volumeSize, projectionSize, trajectory, rotationCount, photonCount);
                                    ImageArray intensity = project(rock, A);
                                    ImageArray attenuation = negln(intensity);
                                    //const string method = parameters.at("method"_);
                                    for(const uint subsetSize: {1, int(round(sqrt(float(projectionSize.z)))), projectionSize.z}) {
                                        const uint iterationCount = 64; //parameters.value("iterationCount"_,64);
                                        String parameters = str(strx(A.volumeSize), grainRadius, strx(A.projectionSize), ref<string>({"single"_,"double"_,"adaptive"_})[int(A.trajectory)], A.rotationCount, uint(A.photonCount), subsetSize);
                                        log(parameters);

                                        // Reconstruction
                                        MLTR reconstruction {A, intensity, subsetSize};

                                        // Interface
                                        const int upsample = max(1, 256 / A.projectionSize.x);

                                        Value sliceIndex = Value((volumeSize.z-1) / 2);
                                        SliceView x0 {referenceVolume, upsample, sliceIndex};
                                        SliceView x {reconstruction.x, upsample, sliceIndex};
                                        HBox slices {{&x0, &x}};

                                        Value projectionIndex = Value((A.projectionSize.z-1) / 2);
                                        SliceView b0 {attenuation, upsample, projectionIndex};
                                        VolumeView b {reconstruction.x, A, upsample, projectionIndex};
                                        HBox projections {{&b0, &b}};

                                        VBox layout {{&slices, &projections, &plot}};
                                        Window window {&layout, parameters};

                                        uint bestK = 0;
                                        float bestCenterSSE = inf, bestExtremeSSE = inf;
                                        Folder results = "Results"_;
                                        File resultFile (parameters, results, Flags(WriteOnly|Create|Truncate));
                                        for(uint k: range(iterationCount)) {
                                            reconstruction.step();

                                            float centerSSE = ::SSE(referenceVolume, reconstruction.x, int3(0,0,volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/2));
                                            float extremeSSE = ::SSE(referenceVolume, reconstruction.x, int3(0,0,0), int3(volumeSize.xy(), volumeSize.z/4)) + ::SSE(referenceVolume, reconstruction.x, int3(0,0, 3*volumeSize.z/4), int3(volumeSize.xy(), volumeSize.z/4));
                                            float totalNMSE = (centerSSE+extremeSSE)/(centerSSQ+extremeSSQ);
                                            String result = str(bestK, k, 100*centerSSE/centerSSQ, 100*extremeSSE/extremeSSQ, 100*totalNMSE);
                                            resultFile.write(result);
                                            log(result);
                                            plot[parameters].insert(k, 100*totalNMSE);
                                            window.needRender = true;
                                            window.event();

                                            if(centerSSE + extremeSSE < bestCenterSSE + bestExtremeSSE) {
                                                bestK=k;
                                                writeFile(parameters+".best"_, cast<byte>(reconstruction.x.read().data), results);
                                                if(k == iterationCount-1) log("WARNING: Slow convergence stopped by low iteration count");
                                            }
                                            bestCenterSSE = min(bestCenterSSE, centerSSE), bestExtremeSSE = min(bestExtremeSSE, extremeSSE);
                                        }
                                        log(repeat("-"_,128));
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
} app;
