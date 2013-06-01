#include "volume-operation.h"
#include "sample.h"
#include "time.h"
#include "thread.h"
#include "simd.h"

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
#if DEBUG
    pore.maximum=0xFFFFFFFF, rock.maximum=0xFFFFFFFF;  // for the assert
#else
    pore.maximum=(pore.sampleCount.x-1)*(pore.sampleCount.x-1), rock.maximum=(rock.sampleCount.x-1)*(rock.sampleCount.x-1); // for visualization
#endif
}

/// Segments between either rock or pore space by comparing density against a uniform threshold
class(Threshold, Operation), virtual VolumeOperation {
    ref<byte> parameters() const override { return "threshold resultFolder"_; }
    uint outputSampleSize(uint) override { return 4; }
    void execute(const Dict& args, array<Volume>& outputs, const ref<Volume>& inputs) override {
        const Volume& source = inputs[0];
        float densityThreshold=0;
        if(args.contains("threshold"_)) {
            densityThreshold = toDecimal(args.at("threshold"_));
            while(densityThreshold >= 1) densityThreshold /= 1<<8; // Accepts 16bit, 8bit or normalized threshold
        }
        if(!densityThreshold) {
            Time time;
            Sample density = histogram(source,  args.contains("cylinder"_)); //FIXME: histogram the unsmoothed data
            log("density", time);
            bool plot = false;
            ref<byte> name = "threshold"_; // FIXME
            Folder resultFolder = args.at("resultFolder"_); // FIXME: split + folder output
            if(plot) writeFile(name+".density.tsv"_, toASCII(density), resultFolder);
            if(name != "validation"_) density[0]=density[density.size-1]=0; // Ignores clipped values
#if 0 // Lorentzian peak mixture estimation. Works for well separated peaks (intersection under half maximum), proper way would be to use expectation maximization
            Lorentz rock = estimateLorentz(density); // Rock density is the highest peak
            if(plot) writeFile(name+".rock.tsv"_, toASCII(sample(rock,density.size)), resultFolder);
            Sample notrock = density - sample(rock, density.size); // Substracts first estimated peak in order to estimate second peak
            if(plot) writeFile(name+".notrock.tsv"_, toASCII(notrock), resultFolder);
            Lorentz pore = estimateLorentz(notrock); // Pore density is the new highest peak
            pore.height = density[pore.position]; // Use peak height from total data (estimating on not-rock yields too low estimate because rock is estimated so wide its tail overlaps pore peak)
            if(plot) writeFile(name+".pore.tsv"_, toASCII(sample(pore,density.size)), resultFolder);
            Sample notpore = density - sample(pore, density.size);
            if(plot) writeFile(name+".notpore.tsv"_, toASCII(notpore), resultFolder);
            uint threshold=0; for(uint i: range(pore.position, rock.position)) if(pore[i] <= notpore[i]) { threshold = i; break; } // First intersection between pore and not-pore (same probability)
            if(name=="validation"_) threshold = (pore.position+rock.position)/2; // Validation threshold cannot be estimated with this model
            densityThreshold = float(threshold) / float(density.size);
            log("Automatic threshold", densityThreshold, "between pore at", float(pore.position)/float(density.size), "and rock at", float(rock.position)/float(density.size));
#else // Exhaustively search for inter-class variance maximum ω₁ω₂(μ₁ - μ₂)² (shown by Otsu to be equivalent to intra-class variance minimum ω₁σ₁² + ω₂σ₂²)
            uint threshold=0; double maximum=0;
            uint64 totalCount=0, totalSum=0; double totalMaximum=0;
            for(uint64 t: range(density.size)) totalCount+=density[t], totalSum += t * density[t], totalMaximum=max(totalMaximum, double(density[t]));
            uint64 backgroundCount=0, backgroundSum=0;
            Sample interclass (density.size, density.size, 0);
            double variances[density.size];
            for(uint64 t: range(density.size)) {
                backgroundCount += density[t];
                if(backgroundCount == 0) continue;
                backgroundSum += t*density[t];
                uint64 foregroundCount = totalCount - backgroundCount, foregroundSum = totalSum - backgroundSum;
                if(foregroundCount == 0) break;
                double variance = double(foregroundCount)*double(backgroundCount)*sqr(double(foregroundSum)/double(foregroundCount) - double(backgroundSum)/double(backgroundCount));
                if(variance > maximum) maximum=variance, threshold = t;
                variances[t] = variance;
            }
            for(uint t: range(density.size)) interclass[t]=variances[t]*totalMaximum/maximum; // Scales to plot over density
            if(plot) writeFile("interclass.tsv"_,toASCII(interclass), resultFolder);
            densityThreshold = float(threshold) / float(density.size);
            log("Automatic threshold", densityThreshold);
#endif
        } else log("Manual threshold", densityThreshold);
        threshold(outputs[0], outputs[1], source, densityThreshold);
    }
};

/// Maps intensity to either red or green channel depending on binary classification
void colorize(Volume24& target, const Volume32& binary, const Volume16& intensity) {
    assert(!binary.offsetX && !binary.offsetY && !binary.offsetZ);
    int X = target.sampleCount.x, Y = target.sampleCount.y, Z = target.sampleCount.z, XY = X*Y;
    const uint32* const binaryData = binary;
    const uint16* const intensityData = intensity;
    const uint maximum = intensity.maximum;
    bgr* const targetData = target;
    parallel(Z, [&](uint, uint z) {
        const uint32* const binaryZ = binaryData+z*XY;
        const uint16* const intensityZ = intensityData+z*XY;
        bgr* const targetZ = targetData+z*XY;
        for(int y=0; y<Y; y++) {
            const uint32* const binaryZY = binaryZ+y*X;
            const uint16* const intensityZY = intensityZ+y*X;
            bgr* const targetZY = targetZ+y*X;
            for(int x=0; x<X; x++) {
                uint8 c = 0xFF*intensityZY[x]/maximum;
                targetZY[x] = binaryZY[x]==0xFFFFFFFF ? bgr{0,c,0} : bgr{0,0,c};
            }
        }
    });
    target.maximum=0xFF;
}

class(Colorize, Operation), virtual VolumeOperation {
    uint outputSampleSize(uint) override { return 3; }
    void execute(const Dict&, array<Volume>& outputs, const ref<Volume>& inputs) override { colorize(outputs[0], inputs[0], inputs[1]); }
};
