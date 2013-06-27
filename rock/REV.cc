#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Returns relative deviation versus cylinder radius of 8 volume samples
class(REV, Tool) {
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, ResultManager& results) override {
        Volume input = toVolume(results.getResult("connected"_, arguments));
        int margin = max(max(input.margin.x, input.margin.y), input.margin.z), size=min(min(input.sampleCount.x, input.sampleCount.y), input.sampleCount.z);
        NonUniformSample relativeDeviations;
        const real ratio = (real)(margin+2)/(margin+1);
        for(double r=margin+1; round(r)<(size-margin)/4; r*=ratio) {
            int radius = int(round(r));
            array<NonUniformSample> nonUniformSamples;
            for(int3 octant: (int3[]){int3{-1,-1,-1},int3{1,-1,-1},int3{-1,1,-1},int3{1,1,-1},int3{-1,-1,1},int3{1,-1,1},int3{-1,1,1},int3{1,1,1}}) {
                int3 center = int3(size/2) + radius * octant;
                Dict args = copy(arguments);
                args.insert("histogram-squaredRadius.crop"_);
                args.insert(String("histogram-squaredRadius.cylinder"_), str((int[]){center.x, center.y, radius, center.z-radius, center.z+radius},','));
                shared<Result> result = results.getResult("volume-distribution-radius"_, args); // Pore size distribution (values are volume in voxels)
                nonUniformSamples << parseNonUniformSample( result->data );
            }
            array<UniformSample> samples = resample(nonUniformSamples);
            UniformSample mean = ::mean(samples);
            /// Computes deviation of a sample of distributions
            real sumOfSquareDifferences = 0; for(const UniformSample& sample: samples) sumOfSquareDifferences += sum(sq(sample-mean));
            real unbiasedVarianceEstimator = sumOfSquareDifferences / (samples.size-1);  //assuming samples are uncorrelated
            real relativeDeviation = sqrt(unbiasedVarianceEstimator) / mean.sum();
            log(radius, ftoa(relativeDeviation,2,0,true));
            relativeDeviations.insert(radius, relativeDeviation);
            outputs[1]->elements.insert("R="_+dec(radius,3), ("#Pore size distribution versus cylinder radius (mean of 8 volume samples)\n"_ + toASCII((1/mean.sum())*mean)));
        }
        outputs[0]->metadata = String("ε(R).tsv"_);
        outputs[0]->data = "#Relative deviation of pore size distribution versus cylinder radius of 8 volume samples\n"_ + toASCII(relativeDeviations);
        outputs[1]->metadata = String("PSD(R).tsv"_);
    }
};
