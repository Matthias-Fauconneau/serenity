#include "sample.h"
#include "data.h"
#include "math.h"

float sum(const Sample& A) { float sum=0; for(uint i: range(A.size)) sum += A[i]; return sum; }
float mean(const Sample& A) { return sum(A)/A.size; }
float variance(const Sample& A) { float mean=::mean(A), ssd=0; for(uint i: range(A.size)) ssd += sq(A[i]-mean); return ssd/A.size; }

float histogramSum(const Sample& A) { float sum=0; for(uint i: range(A.size)) sum += i*A[i]; return sum; }
float histogramMean(const Sample& A) { return histogramSum(A)/sum(A); }
float histogramVariance(const Sample& A) { float mean=::histogramMean(A), ssd=0; for(uint i: range(A.size)) ssd += A[i]*sq(i-mean); return ssd/sum(A); }

Sample operator*(float s, const Sample& A) {
    uint N=A.size; Sample R(N);
    for(uint i: range(N)) R[i]=s*A[i];
    return R;
}

Sample operator-(const Sample& A, const Sample& B) {
    uint N=A.size; assert(B.size==N); Sample R(N);
    for(uint i: range(N)) R[i]=max(0.f, A[i]-B[i]);
    return R;
}

Sample operator*(const Sample& A, const Sample& B) {
    uint N=A.size; assert(B.size==N); Sample R(N);
    for(uint i: range(N)) R[i]=A[i]*B[i];
    return R;
}

Sample sqrtHistogram(const Sample& A) {
    uint N=round(sqrt(float(A.size-1)))+1; Sample R(N,N,0);
    for(uint i: range(A.size)) R[round(sqrt(float(i)))] += A[i];
    return R;
}

Sample parseSample(const ref<byte>& file) {
    TextData s (file);
    int maximum=0; while(s) { maximum=max(maximum, int(s.decimal())); s.skip("\t"_); s.decimal(); s.skip("\n"_); }
    s.index=0;
    Sample sample(maximum+1);
    while(s) { double x=s.decimal(); assert_(x==double(int(x)) && x<sample.size, x, double(int(x)), maximum); s.skip("\t"_); sample[int(x)]=s.decimal(); s.skip("\n"_); }
    return sample;
}

string toASCII(const Sample& sample, float scale) {
    string s;
    for(uint i=0; i<sample.size; i++) if(sample[i]) s << ftoa(i*scale, scale==1?0:4) << '\t' << ftoa(sample[i], 4, 0, true) << '\n';
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

#if 0
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
#endif
