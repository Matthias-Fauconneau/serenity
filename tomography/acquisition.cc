#include "thread.h"
#include "operators.h"
#include "project.h"
#include "view.h"
#include "layout.h"
#include "window.h"

const uint N = fromInteger(arguments()[0]);
const int3 projectionSize = int3(N);
const bool oversample = true;
const int3 volumeSize = int3(oversample ? 2*N : N);
const float photonCount = 4096; // Photon count per pixel for a blank scan (without attenuation) of same duration
const bool noise = true;

CLVolume x (VolumeF(volumeSize, Map(strx(volumeSize)+".ref"_,"Data"_), "x"_));
VolumeF Ax (projectionSize, Map(File(strx(projectionSize)+".proj"_,"Data"_,Flags(ReadWrite|Create|Truncate)).resize(cb(N)*sizeof(float)), Map::Prot(Map::Read|Map::Write)), "Ax"_);

Random random;
extern "C" double lgamma(double x);
uint poisson(double lambda) {
    double c = 0.767 - 3.36/lambda;
    double beta = PI/sqrt(3.0*lambda);
    double alpha = beta*lambda;
    double k = ln(c) - lambda - ln(beta);
    for(;;) {
        float u = random();
        double x = (alpha - ln((1.0 - u)/u))/beta;
        int n = floor(x + 1./2);
        if(n < 0) continue;
        double v = random();
        double y = alpha - beta*x;
        double lhs = y + ln(v/sq(1.0 + exp(y)));
        double rhs = k + n*ln(lambda) - lgamma(n+1);
        if (lhs <= rhs) return n;
    }
}

struct App {
    App() {
        for(uint index: range(Ax.size.z)) {
            log(index);
            ImageF slice = ::slice(Ax, index);
            if(oversample) {
                ImageF fullSize(2*projectionSize.xy());
                ::project(fullSize, x, Ax.size.z, index);
                downsample(slice, fullSize);
            } else {
                ::project(slice, x, Ax.size.z, index);
            }
            for(float& y: slice.data) y = noise ? poisson(photonCount * exp(-y)) / photonCount : exp(-y);
        }
        double sum = ::sum(x); log(sum / (volumeSize.x*volumeSize.y*volumeSize.z));
        double SSQ = ::SSQ(x); log(sqrt(SSQ / (volumeSize.x*volumeSize.y*volumeSize.z)));
    }
} app;

SliceView sliceView (x, 512/volumeSize.x);
SliceView volumeView (Ax, 512/projectionSize.x, VolumeView::staticIndex);
HBox layout ({&sliceView, &volumeView});
Window window (&layout, strx(volumeSize));
