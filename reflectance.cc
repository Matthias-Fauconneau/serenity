#include "data.h"
#include "window.h"
#include "display.h"
#include "text.h"

/// \name Real functions
typedef float real;
const float PI = 3.14159265358979323846;
inline float exp(float x) { return __builtin_expf(x); }
inline float cos(float t) { return __builtin_cosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
inline float log10(float x) { return __builtin_log10f(x); }
inline float dB(float x) { return 10*log10(x); }
/// \}

/// \name Complex operations
struct complex { real re, im; complex(real re, real im):re(re),im(im){} };
float norm(complex z) { return sqrt(z.re*z.re+z.im*z.im); }
float phase(complex z) { return __builtin_atan2f(z.im, z.re); }
complex polar(real r, real a) { return complex(r*cos(a),r*sin(a)); }
complex operator+(complex a, complex b) { return complex(a.re+b.re, a.im+b.im); }
complex operator-(complex a, complex b) { return complex(a.re-b.re, a.im-b.im); }
complex operator*(complex a, complex b) { return complex(a.re*b.re-a.im*b.im, a.re*b.im + a.im*b.re); }
complex operator/(complex a, float b) { return complex(a.re/b, a.im/b); }
complex operator/(complex a, complex b) { return complex(a.re*b.re+a.im*b.im, a.re*b.im - a.im*b.re) / (b.re*b.re + b.im*b.im); }
complex& operator+=(complex& a, complex b) { a.re+=b.re, a.im+=b.im; return a;}
/// \}

/// Dynamically allocated vector of reals
typedef array<real> vec;

/// Initializes a vector of N values to zero
vec zeros(uint N) { return vec(N,N,0); }
/// Initializes a vector of N values to one
vec ones(uint N) { return vec(N,N,1); }

/// \name Vector operations
vec operator+(const vec& A, const vec& B) {
    uint N=A.size; assert(B.size==N); vec R(N,N);
    for(uint i: range(N)) R[i]=A[i]+B[i];
    return R;
}
vec operator-(const vec& A, const vec& B) {
    uint N=A.size; assert(B.size==N); vec R(N,N);
    for(uint i: range(N)) R[i]=A[i]-B[i];
    return R;
}
vec operator*(float A, const vec& B) {
    uint N=B.size; vec R(N,N);
    for(uint i: range(N)) R[i]=A*B[i];
    return R;
}
vec operator*(const vec& A, float B) {
    uint N=A.size; vec R(N,N);
    for(uint i: range(N)) R[i]=A[i]*B;
    return R;
}
vec operator/(const vec& A, float B) {
    uint N=A.size; vec R(N,N);
    for(uint i: range(N)) R[i]=A[i]/B;
    return R;
}
vec& operator+=(vec& A, const vec& B) {
    uint N=A.size; assert(B.size==N);
    for(uint i: range(N)) A[i]+=B[i];
    return A;
}
/// \}

/// Flips a vector
vec flip(const vec& A) {
    uint N=A.size; vec R(N,N);
    for(uint i: range(N)) R[i]=A[N-1-i];
    return R;
}

/// Applies \a dB to all elements
vec dB(const vec& A) {
    uint N=A.size; vec R(N,N);
    for(uint i: range(N)) R[i]=dB(A[i]);
    return R;
}

/// Filters data x using digital filter b/a
vec filter(const vec& b, const vec& a, const vec& x) {
    uint N = x.size;
    vec y(N,N);
    for(uint n: range(N)) {
        y[n] = 0;
        for(uint i: range(0,min(n,b.size))) y[n] += b[i]*x[n-1-i]; //feedforward
        for(uint i: range(1,min(n,a.size))) y[n] -= a[i]*y[n-1-i]; //feedback
    }
    return y;
}
vec filter(const vec& b, const vec& x) { return filter(b,vec(1,1,1),x); }

/// \name Vector operations on dynamic arrays of complex numbers
typedef array<complex> vecc;

/// Applies norm to all elements
vec norm(const vecc& A) {
    uint N=A.size; vec R(N,N);
    for(uint i: range(N)) R[i]=norm(A[i]);
    return R;
}
/// Applies phase to all elements
vec phase(const vecc& A) {
    uint N=A.size; vec R(N,N);
    for(uint i: range(N)) R[i]=phase(A[i]);
    return R;
}
/// \}

/// Samples frequency response of digital filter b/a on n points from [0, 2π fc]
vecc freqz(const vec& b, const vec& a, uint N, float fc = 1) {
    vecc y(N,N);
    for(uint i: range(N)) {
        float w = 2*PI*fc*i/(N-1);
        complex n (0,0); for(uint k: range(b.size)) n += polar(b[k], -w*k);
        complex d (0,0); for(uint l: range(a.size)) d += polar(a[l], -w*l);
        y[i] = (n/d);
    }
    return y;
}

struct Plot : Widget {
    array<float> data; string title, xlabel, ylabel;
    Plot(array<float>&& data, const ref<byte>& title, const ref<byte>& xlabel="X"_, const ref<byte>& ylabel="Y"_) :
        data(move(data)),title(string(title)),xlabel(string(xlabel)),ylabel(string(ylabel)){}

    void render(int2 position, int2 size) {
        Text(title).render(position);
        if(!data.size) return;
        float min=0, max=0;
        for(uint x: range(data.size)) {
            float y = data[x];
            min=::min(min,y);
            max=::max(max,y);
        }
        min = -(max = ::max(abs(min),abs(max)));
        vec2 scale = vec2(size.x/(data.size-1.), size.y/(max-min));
        for(uint x: range(data.size-1)) {
            vec2 a = vec2(position)+scale*vec2(x,  (max-min)-(data[x]-min));
            vec2 b = vec2(position)+scale*vec2(x+1, (max-min)-(data[x+1]-min));
            line(a,b);
        }
        line(position+int2(0,size.y/2),position+int2(size.x,size.y/2), darkGray);
    }
};

/// Computes a passive reflectance filter from resonances data and plots its frequency response
struct PassiveReflectancePlot : Widget {
    array<Plot> plots;
    array<float> angles;
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
            angles << angle;
            A1 << -2*radius*cos(angle); // 2nd-order section coeff
            A2 << radius*radius; // 2nd-order section coeff
        }
        const uint N = A1.size;
        vec A; A << 1 << zeros(2*N);
        // A=Π[i](1/(1+a1[i]z[-1]+a2[i]z[-2]) (polynomial multiplication = feedforward filter)
        for(uint unused i: range(N-2)) A = filter(ones(N), A);
        A = filter(A1, A);
        A = filter(A2, A);

        // Now A contains the (stable) denominator of the desired bridge admittance.
#if 1 //Constructs a numerator which gives a positive-real result by first creating a passive reflectance and then computing the corresponding PR admittance.
        const float g = 0.9;         // Uniform loss factor
        vec B = g*flip(A); // Flip(A)/A = desired allpass
#else //Construct a resonator as a sum of arbitrary modes with unit residues, adding a near-zero at dc ==
        vec B = zeros(2*N+1);
        vec impulse; impulse << 1 << zeros(2*N);
        // B=Γ[0]·(1-z[-1])·Σ[i](1/(1+a1[i]z[-1]+a2[i]z[-2]) polynomial division = feedback filter
        for(uint unused i: range(N-2)) B += filter(A,ones(N), impulse);
        B += filter(A, A1, impulse);
        B += filter(A, A2, impulse);

        // near-zero at dc
        float dc[] = {1, -0.995};
        B = filter(vec(dc,2),B);
#endif

        vec Badm = A - B, Aadm = A + B; // Converts reflectance to admittance
        //assert(Aadm[0]);
        Badm = Badm/Aadm[0], Aadm = Aadm/Aadm[0]; // Renormalize

        const uint fc = fs;//300; // Plot every Hz up to 300Hz
        vecc H = freqz(Badm, Aadm, fc, float(fc)/fs);

        plots << Plot(move(B),"B"_);
        plots << Plot(move(A),"A"_);
        plots << Plot(move(Badm),"Badm"_);
        plots << Plot(move(Aadm),"Aadm"_);
        plots << Plot(dB(norm(H)),"Admittance"_,"Frequency (Hz)"_,"Magnitude (dB)"_);
        plots << Plot(phase(H),"Admittance"_,"Frequency (Hz)"_,"Phase (radians)"_);

        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);
    }

    Window window{this,int2(0,1680/2),"Passive Reflectance"_};
    int2 sizeHint() { return int2(-1,-1); }
    void render(int2 position, int2 size) {
        const uint w = 1, h = plots.size;
        for(uint i : range(plots.size)) {
            int2 plotSize = int2(size.x/w,size.y/h);
            int2 plotPosition = position+int2(i%w,i/w)*plotSize;
            plots[i].render(plotPosition, plotSize);
        }
        for(float angle: angles) {
            int x = position.x+angle/(2*PI)*size.x;
            line(x,position.y,x,position.y+size.y);
        }
    }
} test;
