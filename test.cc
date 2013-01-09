#if 0
#include "process.h"
struct LogTest {
    LogTest(){ log("Hello World"_); }
} test;
#endif

#if 0
#include "asound.h"
const float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
struct SoundTest {
    AudioOutput audio __({this, &SoundTest::read}, 48000, 4096);
    SoundTest() { audio.start(); }
    float step=2*PI*440/48000;
    float amplitude=0x1p12;
    float angle=0;
    bool read(int16* output, uint periodSize) {
        for(uint i : range(periodSize)) {
            float sample = amplitude*sin(angle);
            output[2*i+0] = sample, output[2*i+1] = sample;
            angle += step;
        }
        return true;
    }
} test;
#endif

#if 0
#include "window.h"
#include "html.h"
struct HTMLTest {
    Scroll<HTML> page;
    Window window __(&page.area(),0,"HTML"_);

    HTMLTest() {
        window.localShortcut(Escape).connect(&exit);
        page.contentChanged.connect(&window, &Window::render);
        page.go(""_);
    }
} test;
#endif

#include "data.h"
#include "window.h"
#include "display.h"
#include "text.h"

typedef float real;
const float PI = 3.14159265358979323846;
inline float exp(float x) { return __builtin_expf(x); }
inline float cos(float t) { return __builtin_cosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
inline float log10(float x) { return __builtin_log10f(x); }
//inline float log10(float x) { return log2(x)/log2(10); }
inline float dB(float x) { return 10*log10(x); }

struct complex { real re, im; complex(real re, real im):re(re),im(im){} };
//string str(complex z) { return "(r="_+ftoa(norm(z),2,0)+" θ="_+ftoa(arg(z),2,0)+")"_; }
float norm(complex z) { return sqrt(z.re*z.re+z.im*z.im); }
float angle(complex z) { return __builtin_atan2f(z.im, z.re); }
complex polar(real r, real a) { return complex(r*cos(a),r*sin(a)); }

complex operator+(complex a, complex b) { return complex(a.re+b.re, a.im+b.im); }
complex operator-(complex a, complex b) { return complex(a.re-b.re, a.im-b.im); }
complex operator*(complex a, complex b) { return complex(a.re*b.re-a.im*b.im, a.re*b.im + a.im*b.re); }
complex operator/(complex a, float b) { return complex(a.re/b, a.im/b); }
complex operator/(complex a, complex b) { return complex(a.re*b.re+a.im*b.im, a.re*b.im - a.im*b.re) / (b.re*b.re + b.im*b.im); }

complex& operator+=(complex& a, complex b) { a.re+=b.re, a.im+=b.im; return a;}

// Vector operations on dynamic arrays
typedef array<real> vec;

vec zeros(uint N) { vec r(N); r.setSize(N); for(uint i: range(N)) r[i] = 0; return r; }

vec operator+(const vec& A, const vec& B) {
    uint N=A.size(); assert(B.size()==N); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=A[i]+B[i];
    return R;
}
vec operator-(const vec& A, const vec& B) {
    uint N=A.size(); assert(B.size()==N); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=A[i]-B[i];
    return R;
}
vec operator*(float A, const vec& B) {
    uint N=B.size(); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=A*B[i];
    return R;
}
vec operator*(const vec& A, float B) {
    uint N=A.size(); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=A[i]*B;
    return R;
}
vec operator/(const vec& A, float B) {
    uint N=A.size(); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=A[i]/B;
    return R;
}

vec& operator+=(vec& A, const vec& B) {
    uint N=A.size(); assert(B.size()==N);
    for(uint i: range(N)) A[i]+=B[i];
    return A;
}

vec flip(const vec& A) {
    uint N=A.size(); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=A[N-1-i];
    return R;
}

vec dB(const vec& A) {
    uint N=A.size(); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=dB(A[i]);
    return R;
}

/// Filters data x using digital filter b/a
vec filter(const vec& b, const vec& a, const vec& x) {
    uint N = x.size();
    vec y(N); y.setSize(N);
    for(uint n: range(N)) {
        y[n] = 0;
        for(uint i: range(0,min(n,b.size()))) y[n] += b[i]*x[n-1-i]; //feedforward
        for(uint i: range(1,min(n,a.size()))) y[n] -= a[i]*y[n-1-i]; //feedback
    }
    return y;
}
vec filter(const vec& b, const vec& x) { return filter(b,(float[]){1},x); }

typedef array<complex> vecc;

vec norm(const vecc& A) {
    uint N=A.size(); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=norm(A[i]);
    return R;
}
vec angle(const vecc& A) {
    uint N=A.size(); vec R(N); R.setSize(N);
    for(uint i: range(N)) R[i]=angle(A[i]);
    return R;
}

/// Samples frequency response of digital filter b/a on n points from [0, 2π fc]
vecc freqz(const vec& b, const vec& a, uint N, float fc = 1) {
    vecc y(N); y.setSize(N);
    for(uint i: range(N)) {
        float w = 2*PI*fc*i/(N-1);
        complex n (0,0); for(uint k: range(b.size())) n += polar(b[k], -w*k);
        complex d (0,0); for(uint l: range(a.size())) d += polar(a[l], -w*l);
        y[i] = (n/d);
    }
    return y;
}

/// Displays a plot of Y
template<class Array> void plot(int2 position, int2 size, const Array& Y) {
    if(!Y.size()) return;
    float min=0, max=0;
    for(uint x: range(Y.size())) {
        float y = Y[x];
        min=::min(min,y);
        max=::max(max,y);
    }
    min = -(max = ::max(abs(min),abs(max)));
    vec2 scale = vec2(size.x/(Y.size()-1.), size.y/(max-min));
    for(uint x: range(Y.size()-1)) {
        vec2 a = vec2(position)+scale*vec2(x,  (max-min)-(Y[x]-min));
        vec2 b = vec2(position)+scale*vec2(x+1, (max-min)-(Y[x+1]-min));
        line(a,b);
    }
    line(position+int2(0,size.y/2),position+int2(size.x,size.y/2), darkGray);
}

struct PassiveReflectancePlot : Widget {
    struct Plot {
        vec data; string title, xlabel, ylabel;
        Plot(const vec& data, ref<byte> title, ref<byte> xlabel="X"_, ref<byte> ylabel="Y"_) :
            data(copy(data)),title(string(title)),xlabel(string(xlabel)),ylabel(string(ylabel)){}
    };
    array<Plot> plots;
    PassiveReflectancePlot() {
        const uint fs = 8192;  // Sampling rate in Hz (small for display clarity)
        ref<byte> data = "4.64 10; 96.52 10; 189.33 10; 219.95 10"_; // Measured guitar body resonances

        vec A1, A2; //  2nd-order section coeff
        for(TextData s(data);s;) {
            s.whileAny(" \t"_);
            float f = s.decimal() / fs; // frequency
            s.whileAny(" \t"_);
            float b = s.decimal() / fs; // bandwidth
            s.whileAny(" \t;\n"_);

            float radius = exp(-PI*b);
            float angle = 2*PI*f;
            A1 << -2*radius*cos(angle); // 2nd-order section coeff
            A2 << radius*radius; // 2nd-order section coeff
        }
        const uint N = A1.size();
        vec A; A << 1 << zeros(2*N);
        for(uint i: range(N)) { // A=Π[i](1/(1+a1[i]z[-1]+a2[i]z[-2]) (polynomial multiplication = feedforward filter)
            float denominator[] = {1, A1[i], A2[i]};
            A = filter(denominator, A);
        }
        // Now A contains the (stable) denominator of the desired bridge admittance.
#if 1 //Constructs a numerator which gives a positive-real result by first creating a passive reflectance and then computing the corresponding PR admittance.
        const float g = 0.9;         // Uniform loss factor
        vec B = g*flip(A); // Flip(A)/A = desired allpass
#else //Construct a resonator as a sum of arbitrary modes with unit residues, adding a near-zero at dc ==
        vec B = zeros(2*N+1);
        vec impulse; impulse << 1 << zeros(2*N);
        for(uint i: range(N)) { // B=Γ[0]·(1-z[-1])·Σ[i](1/(1+a1[i]z[-1]+a2[i]z[-2]) polynomial division = feedback filter
            float denominator[] = {1,A1[i],A2[i]};
            B += filter(A,denominator,impulse);
        }
        // near-zero at dc
        B = filter((float[]){1, -0.995},B);
#endif
        plots << Plot(B,"B"_);
        plots << Plot(A,"A"_);

        vec Badm = A - B, Aadm = A + B; // Converts reflectance to admittance
        plots << Plot(Badm,"Badm"_);
        plots << Plot(Aadm,"Aadm"_);
        //assert(Aadm[0]);
        Badm = Badm/Aadm[0], Aadm = Aadm/Aadm[0]; // Renormalize

        const uint fc = fs;//300; // Plot every Hz up to 300Hz
        vecc H = freqz(Badm, Aadm, fc, float(fc)/fs);
        plots << Plot(dB(norm(H)),"Admittance"_,"Frequency (Hz)"_,"Magnitude (dB)"_);
        plots << Plot(angle(H),"Admittance"_,"Frequency (Hz)"_,"Phase (radians)"_);

        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);
    }

    Window window __(this,int2(0,1680/2),"Passive Reflectance"_);
    int2 sizeHint() { return int2(-1,-1); }
    void render(int2 position, int2 size) {
        const uint w = 1, h = plots.size();
        for(uint i : range(plots.size())) {
            int2 plotSize = int2(size.x/w,size.y/h);
            int2 plotPosition = position+int2(i%w,i/w)*plotSize;
            const Plot& plot = plots[i];
            ::plot(plotPosition, plotSize, plot.data);
            Text(plot.title).render(plotPosition);
        }
    }
} test;
