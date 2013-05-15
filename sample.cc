#include "sample.h"
#include "data.h"

Sample histogram(const Volume16& volume, bool cylinder) {
    uint X=volume.x, Y=volume.y, Z=volume.z, XY=X*Y;
    uint marginX=volume.marginX, marginY=volume.marginY, marginZ=volume.marginZ;
    assert(X==Y && marginX==marginY);
    uint radiusSq = cylinder ? (X/2-marginX)*(X/2-marginX) : -1;
    Sample histogram (volume.maximum+1, volume.maximum+1, 0);
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* sourceZ = volume+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* sourceY = sourceZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                if((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2) <= radiusSq) histogram[sourceY[x]]++;
            }
        }
    }
    return histogram;
}

Sample sqrtHistogram(const Volume16& volume, bool cylinder) {
    uint X=volume.x, Y=volume.y, Z=volume.z, XY=X*Y;
    uint marginX=volume.marginX, marginY=volume.marginY, marginZ=volume.marginZ;
    assert(X==Y && marginX==marginY);
    uint radiusSq = cylinder ? (X/2-marginX)*(X/2-marginX) : -1;
    uint maximum = round(sqrt(volume.maximum));
    Sample histogram (maximum+1, maximum+1, 0);
    for(uint z=marginZ; z<Z-marginZ; z++) {
        const uint16* sourceZ = volume+z*XY;
        for(uint y=marginY; y<Y-marginY; y++) {
            const uint16* sourceY = sourceZ+y*X;
            for(uint x=marginX; x<X-marginX; x++) {
                if((x-X/2)*(x-X/2)+(y-Y/2)*(y-Y/2) <= radiusSq) histogram[round(sqrt(sourceY[x]))]++;
            }
        }
    }
    return histogram;
}

Sample parseSample(const ref<byte>& file) {
    Sample sample;
    TextData s (file);
    while(s) { uint i=s.integer(); sample.grow(i+1); s.skip("\t"_); sample[i]=s.integer(); s.skip("\n"_); }
    return sample;
}

inline float log10(float x) { return __builtin_log10f(x); }
string toASCII(const Sample& sample, float scale) {
    string s;
    for(uint i=0; i<sample.size; i++) s << ftoa(i*scale,ceil(-log10(scale))) << '\t' << str(sample[i]) << '\n';
    return s;
}

Lorentz estimateLorentz(const Sample& sample) {
    Lorentz lorentz;
    int x0=0; for(uint x=0; x<sample.size; x++) if(sample[x]>sample[x0]) x0=x;
    int y0 = sample[x0];
    int l0=0; for(int x=x0; x>=0; x--) if(sample[x]<=y0/2) { l0=x; break; } // Left half maximum
    int r0=sample.size; for(int x=x0; x<(int)sample.size; x++) if(sample[x]<=y0/2) { r0=x; break; } // Right half maximum
    lorentz.position = max(x0, (l0+r0)/2); // Position estimated from half maximum is probably more accurate
    lorentz.scale = (r0-l0)/2; // half-width at half-maximum (HWHM)
    lorentz.height = y0;
    return lorentz;
}

Sample sample(const Lorentz& lorentz, uint size) {
    Sample sample(size, size, 0);
    for(int x=0; x<(int)size; x++) sample[x] = lorentz[x];
    return sample;
}

float intersect(const Lorentz& A, const Lorentz& B) {
    float x0=A.position, x1=B.position, y0=A.height, y1=B.height, s0=A.scale, s1=B.scale;
    float b = s0*s0*y0*x1-s1*s1*y1*x0;
    float sy = (s0*s0*y0-s1*s1*y1)*(y1-y0);
    float yx = y0*y1*(x0*x0-x1*x1+2*x0*x1);
    float s = sy+yx;
    log(sy, yx, s);
    float d = s0*s1*sqrt(s);
    float n = s0*s0*y0-s1*s1*y1;
    log(x0, y0, s0);
    log(x1, y1, s1);
    log(b, s, d, n);
    log(b+d, b-d, (b+d)/n, (b-d)/n);
    error(x0,(b-d)/n, (x0+x1)/2, x1);
}
Sample operator-(const Sample& A, const Sample& B) {
    uint N=A.size; assert(B.size==N); Sample R(N,N);
    for(uint i: range(N)) R[i]=max(0, A[i]-B[i]);
    return R;
}
