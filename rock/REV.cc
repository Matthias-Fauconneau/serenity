#include "tool.h"
#include "volume-operation.h"
#include "analysis.h"

class(REV, Tool) {
    void execute(Process& process) {
        const Dict& arguments = process.arguments;
        Volume input = toVolume(process.getResult("crop-connected"_, arguments));
        int margin = max(max(input.margin.x, input.margin.y), input.margin.z), size=min(min(input.sampleCount.x, input.sampleCount.y), input.sampleCount.z);
        NonUniformSample relativeDeviations;
        for(double r=margin+1; round(r)<(size-margin)/4; r=r*(margin+2)/(margin+1)) {
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
        writeFile("REV.tsv", toASCII(relativeDeviations), "/ptmp/results"_);
    }
};
