#include "tool.h"
#include "volume-operation.h"
#include "analysis.h"

/// Returns relative deviation versus cylinder radius of 8 volume samples
class(REV, Tool) {
    shared<Result> execute(Process& process) override {
        const Dict& arguments = process.arguments;
        Volume input = toVolume(process.getResult("connected"_, arguments));
        int margin = max(max(input.margin.x, input.margin.y), input.margin.z), size=min(min(input.sampleCount.x, input.sampleCount.y), input.sampleCount.z);
        NonUniformSample relativeDeviations;
        const real ratio = (real)(margin+2)/(margin+1); /*DEBUG*/
        for(double r=margin+1; round(r)<(size-margin)/4; r*=ratio) {
            int radius = int(round(r));
            array<NonUniformSample> samples;
            for(int3 octant: (int3[]){int3{-1,-1,-1},int3{1,-1,-1},int3{-1,1,-1},int3{1,1,-1},int3{-1,-1,1},int3{1,-1,1},int3{-1,1,1},int3{1,1,1}}) {
                int3 center = int3(size/2) + radius * octant;
                Dict args = copy(arguments);
                args.insert(String("histogram-squaredRadius.cylinder"_), str((int[]){center.x, center.y, radius, center.z-radius, center.z+radius},','));
                shared<Result> result = process.getResult("volume-distribution-radius"_, args); // Pore size distribution (values are volume in voxels)
                samples << parseNonUniformSample( result->data );
            }
            double relativeDeviation = ::relativeDeviation(samples);
            log(radius, ftoa(relativeDeviation,2,0,true));
            relativeDeviations.insert(r, relativeDeviation);
        }
        return shared<Result>("REV"_,0,Dict(),String("tsv"_), toASCII(relativeDeviations));
    }
};

