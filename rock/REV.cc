#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Returns relative deviation versus cylinder radius of 8 volume samples
class(REV, Tool) {
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, ResultManager& results) override {
        real resolution = parseScalar(results.getResult("resolution"_, arguments)->data);
        Volume input = toVolume(results.getResult("connected"_, arguments));
        int margin = max(max(input.margin.x, input.margin.y), input.margin.z), size=min(min(input.sampleCount.x, input.sampleCount.y), input.sampleCount.z);
        map<String, buffer<byte>> PSD_R, PSD_octant;
        NonUniformSample relativeDeviations[3];
        const real start=margin+2, ratio = (start+1)/start;
        for(double r=start; round(r)<(size-margin)/4; r*=ratio) {
            int radius = int(round(r));
            array<NonUniformSample> nonUniformSamples;
            int3 octants[] = {int3{-1,-1,-1},int3{1,-1,-1},int3{-1,1,-1},int3{1,1,-1},int3{-1,-1,1},int3{1,-1,1},int3{-1,1,1},int3{1,1,1}};
            for(int3 octant: octants) {
                int3 center = int3(size/2) + (size/4) * octant;
                Dict args = copy(arguments);
                args.insert("histogram.crop"_);
                args.insert(String("histogram.cylinder"_), str((int[]){center.x, center.y, radius, center.z-radius, center.z+radius},','));
                shared<Result> result = results.getResult("volume-distribution-radius"_, args); // Pore size distribution (values are volume in voxels)
                nonUniformSamples << parseNonUniformSample( result->data );
            }
            array<UniformSample> samples = resample(nonUniformSamples);
            for(UniformSample& sample: samples) sample.scale *= resolution;
            UniformHistogram mean = ::mean(samples);
            const int inflection = 52; //FIXME: compute
            if(radius==inflection) {
                const String title = "#Pore size distribution versus octants (R="_+dec(resolution*inflection)+"μm)\n"_;
                for(uint i: range(8)) PSD_octant.insert(str(octants[i]), title+toASCII((1./mean.sampleCount())*samples[i]));
                PSD_octant.insert(String("mean"_), title+toASCII((1./mean.sampleCount())*mean));
            }
            uint median = mean.median();
            for(uint i: range(3)) { /// Computes deviation of a sample of distributions
                uint begin = (uint[]){0,0,median}[i], end = (uint[]){(uint)mean.size,median,(uint)mean.size}[i]; // 0-size, 0-median, median-size
                assert_(begin<end);
                array<UniformSample> slices = apply(samples, [&](const UniformSample& sample){ return UniformSample(sample.slice(begin,end-begin)); } );
                UniformSample mean = ::mean(slices);
                real sumOfSquareDifferences = 0; for(const UniformSample& slice: slices) sumOfSquareDifferences += sum(sq(slice-mean));
                real unbiasedVarianceEstimator = sumOfSquareDifferences / (slices.size-1);  //assuming samples are uncorrelated
                real relativeDeviation = sqrt( unbiasedVarianceEstimator ) / sqrt( sq(mean).sum() );
                relativeDeviations[i].insert(resolution*radius, relativeDeviation);
            }
            PSD_R.insert(pad(dec(resolution*radius),4)+" μm"_, ("#Pore size distribution versus cylinder radius (mean of 8 volume samples)\n"_+toASCII((1./mean.sampleCount())*mean)));
        }
        assert_(outputs[0]->name == "ε(R)"_);
        outputs[0]->metadata = String("ε(R [μm]).tsv"_);
        const string title = "#Relative deviation of pore size distribution versus cylinder radius of 8 volume samples\n"_; //#logx\n#logy\n
        outputs[0]->data = title + toASCII(relativeDeviations[0]);
        assert_(outputs[1]->name == "ε(R|r<median)"_);
        outputs[1]->metadata = String("ε(R [μm]).tsv"_);
        outputs[1]->data = title + toASCII(relativeDeviations[1]);
        assert_(outputs[2]->name == "ε(R|r>median)"_);
        outputs[2]->metadata = String("ε(R [μm]).tsv"_);
        outputs[2]->data = title + toASCII(relativeDeviations[2]);
        assert_(outputs[3]->name == "PSD(R)"_);
        outputs[3]->metadata = String("V(r [μm]).tsv"_);
        outputs[3]->elements = move(PSD_R);
        assert_(outputs[4]->name == "PSD(octant|R:50)"_);
        outputs[4]->metadata = String("V(r [μm]).tsv"_);
        outputs[4]->elements = move(PSD_octant);
        assert_(outputs[5]->name == "REV"_);
        outputs[5]->metadata = String("text"_);
        outputs[5]->data = String(R"(
This tool computes the minimal representative elementary volume (REV).
The deviation of the pore size distribution is computed for many volume sizes.
The pore size distribution is computed on cylinders as high as large, centered on each octant of the original volume.
The unbiased sample variance estimator is defined as the sum of squared differences to the sample mean divided by the number of samples minus one.
Relative standard error is the deviation divided by the mean.
The squared difference between two distributions is the integral of the squared difference of their values
The deviation is also computed separately for small and big pores in order to compare their convergence rate.
1) Small pores converges with smaller volumes while correctly estimating the distribution of large pores requires bigger volumes.
2) The mean pore size distribution estimation smoothes as it converges.
3) At the inflection point (R=405μm), all pore size distributions are similar to each other.

for each cylinder radius : R
 pore size distribution of each 8 samples : PSD[i]
 Mean distribution : μ = Σ[i] PSD[i] / 8
 Sum of squared differences : SSD = Σ[octants] Σ[radius] ( PSD(radius) - μ(radius) )²
 Unbiased variance estimator: σ² = SSD / (sample count - 1)
 Relative deviation: ε = σ / |μ|
                               )"_);
    }
};
