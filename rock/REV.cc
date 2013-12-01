#include "volume-operation.h"
#include "sample.h"

/// Returns relative deviation versus cylinder radius of 8 volume samples
class(REV, Operation) {
    string parameters() const override { return "path"_; }
    void execute(const Dict& arguments, const Dict&, const ref<Result*>& outputs, const ref<const Result*>&, ResultManager& results) override {
        real resolution = parseScalar(results.getResult("resolution"_, arguments)->data);
        Volume input = toVolume(results.getResult("maximum"_, arguments));
        int margin = max(max(input.margin.x, input.margin.y), input.margin.z), size=min(min(input.sampleCount.x, input.sampleCount.y), input.sampleCount.z)-2*margin;
        map<String, buffer<byte>> PSD_R;
        map<real, array<UniformSample>> PSD_octants;
        NonUniformSample relativeDeviations[3];
        const real start=8, ratio = (start+1)/start;
        const int3 octants[] = {int3{-1,-1,-1},int3{1,-1,-1},int3{-1,1,-1},int3{1,1,-1},int3{-1,-1,1},int3{1,-1,1},int3{-1,1,1},int3{1,1,1}};
        int i=0; for(real r=start; round(r)<size/4; r*=ratio, i++) {
            int radius = int(round(r));
            array<NonUniformSample> nonUniformSamples;
            for(int3 octant: octants) {
                int3 center = input.sampleCount/2 + (size/4) * octant;
                Dict args = copy(arguments);
                args.insert(String("histogram.cylinder"_), str((int[]){center.x, center.y, radius, center.z-radius, center.z+radius},','));
                shared<Result> result = results.getResult("volume-distribution-radius"_, args); // Pore size distribution (values are volume in voxels)
                nonUniformSamples << parseNonUniformSample( result->data );
            }
            array<UniformSample> samples = resample(nonUniformSamples);
            for(UniformSample& sample: samples) sample.scale *= resolution;
            UniformHistogram mean = ::mean(samples);
            uint median = mean.median();
            for(uint i: range(3)) { /// Computes deviation of a sample of distributions
                uint begin = (uint[]){0,0,median}[i], end = (uint[]){(uint)mean.size,median,(uint)mean.size}[i]; // 0-size, 0-median, median-size
                assert_(begin<end, r, i, begin, end, mean.size, median);
                array<UniformSample> slices = apply(samples, [&](const UniformSample& sample){ return UniformSample(sample.slice(begin,end-begin)); } );
                UniformSample mean = ::mean(slices);
                real sumOfSquareDifferences = 0; for(const UniformSample& slice: slices) sumOfSquareDifferences += sum(sq(slice-mean));
                real unbiasedVarianceEstimator = sumOfSquareDifferences / (slices.size-1);  //assuming samples are uncorrelated
                real relativeDeviation = sqrt( unbiasedVarianceEstimator ) / sqrt( sq(mean).sum() );
                relativeDeviations[i].insert(resolution*radius, relativeDeviation);
            }
            if(i%2) PSD_R.insert(pad(dec(resolution*radius),4)+" μm"_, ("#Pore size distribution versus cylinder radius (mean of 8 volume samples)\n"_+toASCII((1./mean.sampleCount())*mean)));
            {array<UniformSample> PSD_octant;
            for(uint i: range(8)) PSD_octant << (1./mean.sampleCount())*samples[i];
            PSD_octant << (1./mean.sampleCount())*mean;
            PSD_octants.insert(resolution*radius, move(PSD_octant));}
        }
        const auto& e=relativeDeviations[0]; real representativeRadius=0; for(uint i: range(e.size())) { representativeRadius=e.keys[i]; if(e.values[i] < 0.25) break; } // Deviation < 0.25
        const string title = "#Relative deviation of pore size distribution versus cylinder radius of 8 volume samples\n"_; //#logx\n#logy\n
        output(outputs, "ε(R)"_, "ε(R [μm]).tsv"_, [&]{return title + toASCII(relativeDeviations[0]);});
        output(outputs, "ε(R|r<median)"_, "ε(R [μm]).tsv"_, [&]{return title + toASCII(relativeDeviations[1]);});
        output(outputs, "ε(R|r>median)"_, "ε(R [μm]).tsv"_, [&]{return title + toASCII(relativeDeviations[2]);});
        outputElements(outputs, "PSD(R)"_, "V(r [μm]).tsv"_, [&]{return move(PSD_R);});
        output(outputs, "representative-radius"_, "scalar"_, [&]{return toASCII(representativeRadius/resolution);});
        for(uint i: range(3)) {
            real radius = i==0 ? PSD_octants.keys[3] : i==1 ? representativeRadius : PSD_octants.keys.last();
            string name = i==0 ? "first"_ : i==1 ? "inflection"_ : "last"_;
            outputElements(outputs, "PSD(octant|R:"_+name+")"_, "V(r [μm]).tsv"_, [&]{
                const array<UniformSample>& PSD_octant = PSD_octants.at(radius);
                const String title = "#Pore size distribution versus octants (R="_+dec(radius)+"μm)\n"_;
                map<String, buffer<byte>> elements;
                for(uint i: range(8)) elements.insert(str(octants[i]), title+toASCII(PSD_octant[i]));
                elements.insert(String("mean"_), title+toASCII(PSD_octant.last()));
                return elements;
            });
        }
    }
};
