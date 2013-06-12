#include "volume-operation.h"
#include "sample.h"
#include "time.h"
#include "thread.h"
#include "simd.h"

/// Exhaustively search for inter-class variance maximum ω₁ω₂(μ₁ - μ₂)² (shown by Otsu to be equivalent to intra-class variance minimum ω₁σ₁² + ω₂σ₂²)
class(Otsu, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        assert_(inputs[0]->metadata == "histogram.tsv"_);
        Sample density = parseUniformSample( inputs[0]->data );
        density[0]=density[density.size-1]=0; // Ignores clipped values
        uint threshold=0; double maximumVariance=0;
        uint64 totalCount=0, totalSum=0;
        for(uint64 t: range(density.size)) totalCount+=density[t], totalSum += t * density[t];
        uint64 backgroundCount=0, backgroundSum=0;
        Sample interclassVariance (density.size); double parameters[4];
        for(uint64 t: range(density.size)) {
            backgroundCount += density[t];
            if(backgroundCount == 0) continue;
            backgroundSum += t*density[t];
            uint64 foregroundCount = totalCount - backgroundCount, foregroundSum = totalSum - backgroundSum;
            if(foregroundCount == 0) break;
            double foregroundMean = double(foregroundSum)/double(foregroundCount);
            double backgroundMean = double(backgroundSum)/double(backgroundCount);
            double variance = double(foregroundCount)*double(backgroundCount)*sq(foregroundMean - backgroundMean);
            if(variance > maximumVariance) {
                maximumVariance=variance, threshold = t;
                parameters[0]=backgroundCount, parameters[1]=foregroundCount, parameters[2]=backgroundMean, parameters[3]=foregroundMean;
            }
            interclassVariance[t] = variance;
        }
        float densityThreshold = float(threshold) / float(density.size);
        //log("Otsu's method estimates threshold at", densityThreshold);
        outputs[0]->metadata = string("scalar"_);
        outputs[0]->data = ftoa(densityThreshold, 6)+"\n"_;
        output(outputs, 1, "otsu"_, [&]{
            return "threshold "_+ftoa(densityThreshold, 6)+"\n"_
                    "backgroundCount "_+dec(parameters[0])+"\n"_
                    "foregroundCount "_+dec(parameters[1])+"\n"_
                    "backgroundMean "_+str(parameters[2])+"\n"_
                    "foregroundMean "_+str(parameters[3])+"\n"_
                    "maximumDeviation "_+str(sqrt(maximumVariance/sq(totalCount))); } );
        output(outputs, 2, "deviation.tsv"_, [&]{ return toASCII((1./(totalCount-1))*squareRoot(interclassVariance)); } );
    }
};

/// Lorentzian peak mixture estimation. Works for well separated peaks (intersection under half maximum), proper way would be to use expectation maximization
class(LorentzianMixtureModel, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        assert_(inputs[0]->metadata == "histogram.tsv"_);
        Sample density = parseUniformSample( inputs[0]->data );
        density[0]=density[density.size-1]=0; // Zeroes extreme values (clipping artifacts)
        const Lorentz rock = estimateLorentz(density); // Rock density is the highest peak
        const Sample notrock = density - sample(rock, density.size); // Substracts first estimated peak in order to estimate second peak
        Lorentz pore = estimateLorentz(notrock); // Pore density is the new highest peak
        pore.height = density[pore.position]; // Use peak height from total data (estimating on not-rock yields too low estimate because rock is estimated so wide its tail overlaps pore peak)
        const Sample notpore = density - sample(pore, density.size);
        uint threshold=0; for(uint i: range(pore.position, rock.position)) if(pore[i] <= notpore[i]) { threshold = i; break; } // First intersection between pore and not-pore (same probability)
        float densityThreshold = float(threshold) / float(density.size);
        log("Lorentzian mixture model estimates threshold at", densityThreshold, "between pore at", float(pore.position)/float(density.size), "and rock at", float(rock.position)/float(density.size));
        outputs[0]->metadata = string("scalar"_);
        outputs[0]->data = ftoa(densityThreshold, 5)+"\n"_;
        output(outputs, 1, "lorentz"_, [&]{ return str("rock",rock)+"\n"_+str("pore",pore); });
        output(outputs, 2, "lorentz.tsv"_, [&]{ return toASCII(sample(rock,density.size)); });
        output(outputs, 3, "density.tsv"_, [&]{ return toASCII(notrock); });
        output(outputs, 4, "lorentz.tsv"_, [&]{ return toASCII(sample(pore,density.size)); });
        output(outputs, 5, "density.tsv"_, [&]{ return toASCII(notpore); });
    }
};

/// Computes the mean gradient for each set of voxels with the same density, and defines threshold as the density of the set of voxels with the maximum mean gradient
/// \note Provided for backward compatibility only
class(MaximumMeanGradient, Operation) {
    void execute(const Dict&, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        const Volume16& source = toVolume(*inputs[0]);
        uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z;
        Sample gradientSum (source.maximum+1, source.maximum+1, 0); // Sum of finite differences for voxels belonging to each density value
        Sample histogram (source.maximum+1, source.maximum+1, 0); // Count samples belonging to each class to compute mean
        for(uint z: range(Z-1)) for(uint y: range(Y-1)) for(uint x: range(X-1)) {
            const uint16* const voxel = &source[z*X*Y+y*X+x];
            uint gradient = abs(int(voxel[0]) - int(voxel[1])) + abs(int(voxel[0]) - int(voxel[X])) /*+ abs(voxel[0] - voxel[X*Y])*/; //[sic] Anistropic for backward compatibility
            const uint binCount = 255; assert(binCount<=source.maximum); // Quantizes to 8bit as this method fails if voxels are not grouped in large enough sets
            uint bin = uint(voxel[0])*binCount/source.maximum*source.maximum/binCount;
            gradientSum[bin] += gradient, histogram[bin]++;
            //gradientSum[voxel[1]] += gradient, histogram[voxel[1]]++; //[sic] Asymetric for backward compatibility
            //gradientSum[voxel[X]] += gradient, histogram[voxel[X]]++; //[sic] Asymetric for backward compatibility
            //gradientSum[voxel[X*Y]] += gradient, histogram[voxel[X*Y]]++; //[sic] Asymetric for backward compatibility
        }
        // Pick pore and rock space as the two highest density maximums (FIXME: is this backward compatible ?)
        Sample density = parseUniformSample( inputs[1]->data );
        uint pore=0, rock=0; float poreMaximum=0, rockMaximum=0;
        for(uint i: range(1,density.size-1)) {
            if(density[i-1]<density[i] && density[i]<density[i+1] && density[i]>poreMaximum) {
                pore=i; poreMaximum = density[i];
                if(poreMaximum > rockMaximum) swap(pore, rock), swap(poreMaximum, rockMaximum);
            }
        }
        log(pore/ float(histogram.size), rock/ float(histogram.size));
        assert(rock > pore); assert(rockMaximum>poreMaximum); assert(rock < histogram.size);
        uint threshold=0; float maximum=0;
        Sample gradientMean (source.maximum+1);
        for(uint i: range(pore, rock)) {
            if(!histogram[i]) continue; // Not enough samples to properly estimate mean gradient for this density threshold
            float mean = gradientSum[i]/histogram[i];
            if(mean>maximum) maximum = mean, threshold = i;
            gradientMean[i] = mean;
        }
        float densityThreshold = float(threshold) / float(histogram.size);
        log("Maximum mean gradient estimates threshold at", densityThreshold, "with mean gradient", maximum, "defined by", dec(histogram[threshold]), "voxels");
        outputs[0]->metadata = string("scalar"_);
        outputs[0]->data = ftoa(densityThreshold, 5)+"\n"_;
        output(outputs, 1, "gradient-mean.tsv"_, [&]{ return toASCII(gradientMean); } );
    }
};

/// Segments by setting values over a fixed threshold to ∞ (2³²-1) and to x² otherwise (for distance X input)
void threshold(Volume32& pore, Volume32& rock, const Volume16& source, float threshold) {
    v4si scaledThreshold = set1(threshold*source.maximum);
    uint X=source.sampleCount.x, Y=source.sampleCount.y, Z=source.sampleCount.z, XY=X*Y;
    assert(X%8==0);
    uint32 sqr[X]; for(uint x=0; x<X; x++) sqr[x]=x*x; // Lookup table of squares
    uint32* const poreData = pore;
    uint32* const rockData = rock;
    parallel(Z, [&](uint, uint z) {
        const uint16* const sourceZ = source + z*XY;
        uint32* const poreZ = poreData + z*XY;
        uint32* const rockZ = rockData + z*XY;
        for(uint y=0; y<Y; y++) {
            const uint16* const sourceY = sourceZ + y*X;
            uint32* const poreZY = poreZ + y*X;
            uint32* const rockZY = rockZ + y*X;
            for(uint x=0; x<X; x+=8) {
                storea(poreZY+x, loada(sqr+x) | (scaledThreshold > unpacklo(loada(sourceY+x), _0h)));
                storea(poreZY+x+4, loada(sqr+x+4) | (scaledThreshold > unpackhi(loada(sourceY+x), _0h)));
                storea(rockZY+x, loada(sqr+x) | (unpacklo(loada(sourceY+x), _0h) > scaledThreshold));
                storea(rockZY+x+4, loada(sqr+x+4) | (unpackhi(loada(sourceY+x), _0h) > scaledThreshold));
            }
        }
    });
    // Sets boundary voxels to ensures threshold volume is closed (non-zero borders) to avoid null/full rows in distance search
    uint marginX=align(4,source.margin.x)+1, marginY=align(4,source.margin.y)+1, marginZ=align(4,source.margin.z)+1;
    pore.margin.x=marginX, pore.margin.y=marginY, pore.margin.z=marginZ;
    rock.margin.x=marginX, rock.margin.y=marginY, rock.margin.z=marginZ;
    setBorders(pore);
    setBorders(rock);
#if ASSERT
    pore.maximum=0xFFFFFFFF, rock.maximum=0xFFFFFFFF;  // for the assert
#else
    pore.maximum=(pore.sampleCount.x-1)*(pore.sampleCount.x-1), rock.maximum=(rock.sampleCount.x-1)*(rock.sampleCount.x-1); // for visualization
#endif
}

/// Segments between either rock or pore space by comparing density against a uniform threshold
class(Binary, Operation), virtual VolumeOperation {
    ref<byte> parameters() const override { return "threshold"_; }
    uint outputSampleSize(uint) override { return 4; }
    void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>& otherInputs) override {
        const Volume& source = inputs[0];
        float densityThreshold;
        if(args.contains("threshold"_) && isDecimal(args.at("threshold"_))) {
            densityThreshold = toDecimal(args.at("threshold"_));
            while(densityThreshold >= 1) densityThreshold /= 1<<8; // Accepts 16bit, 8bit or normalized threshold
            //log("Threshold argument", densityThreshold);
        } else {
            Result* threshold = otherInputs[0];
            densityThreshold = TextData(threshold->data).decimal();
            //log("Threshold input", densityThreshold);
        }
        threshold(outputs[0], outputs[1], source, densityThreshold);
    }
};

/// Maps intensity to either red or green channel depending on binary classification
void colorize(Volume24& target, const Volume32& binary, const Volume16& intensity) {
    assert_(!binary.tiled() && !intensity.tiled() && binary.sampleCount == intensity.sampleCount);
    int X = target.sampleCount.x, Y = target.sampleCount.y, Z = target.sampleCount.z, XY = X*Y;
    const uint maximum = intensity.maximum;
    const uint32* const binaryData = binary;
    const uint16* const intensityData = intensity;
    bgr* const targetData = target;
    for(uint i : range(binary.size())) {
        uint8 c = 0xFF*intensityData[i]/maximum;
        targetData[i] = binaryData[i]==0xFFFFFFFF ? bgr{0,c,0} : bgr{0,0,c};
    }
    target.maximum=0xFF;
}

class(Colorize, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return 3; }
    void execute(const Dict&, const mref<Volume>& outputs, const ref<Volume>& inputs) override { colorize(outputs[0], inputs[0], inputs[1]); }
};
