#include "thread.h"
#include "math.h"
#include "layout.h"
#include "window.h"
#include "display.h"
#include "biquad.h"
#include "ffmpeg.h"
#include "pitch.h"
#include <fftw3.h> //fftw3f

const uint rate = 96000;
const uint N = rate; // Frequency resolution

struct Plot : Widget {
    default_move(Plot);

    float minY=inf, meanY=0, maxY=0;
    buffer<float> Y;
    Plot(ref<float> data, uint X=0) : Y(X?:data.size) {
        assert_(data.size%Y.size==0);
        float sumY = 0;
        for(int x: range(Y.size)) {
            float sum=0; for(uint n: range(x*data.size/Y.size,(x+1)*data.size/Y.size)) sum += data[n];
            float y = sum / (data.size/Y.size);
            Y[x] = y;
            sumY += y;
            maxY = max(maxY, y);
            minY = min(minY, y);
        }
        meanY = sumY / Y.size;
        //for(int x: range(Y.size)) Y[x] = Y[x] / maxY; // Linear plot
        //for(int x: range(Y.size)) Y[x] = (Y[x]-minY) / (maxY-minY); // Affine plot
        for(int x: range(Y.size)) Y[x] = (log2(Y[x])-log2(minY)) / (log2(maxY)-log2(minY)); // Log plot (shown noise)
        //for(int x: range(Y.size)) Y[x] = (log2(Y[x])-log2(meanY)) / (log2(maxY)-log2(meanY)); // Log plot (hidden noise)
    }
    void render(int2 position, int2 size) {
        assert_(position.x==0 && uint(size.x)>=Y.size, position, size, Y.size);
        for(uint x: range(Y.size)) for(uint y: range(clip(0,int((1-Y[x])*size.y),size.y))) framebuffer(x,position.y+y) = 0xFF;
        for(uint y: range(size.y)) framebuffer(1*50*N/rate,position.y+y).g = 0;// = byte4(0,0,0xFF,0xFF);
        for(uint y: range(size.y)) framebuffer(3*50*N/rate,position.y+y).g = 0;
        for(uint y: range(size.y)) framebuffer(5*50*N/rate,position.y+y).g = 0;
    }
};

struct DSP {
    const uint X = 1050; // Plot display resolution

    void filter(mref<float> signal) {
        HighPass highpass {2*50./rate}; // High pass filter to remove low frequency noise
        Notch notch1 {1*50./rate, 1}; // Notch filter to remove 50Hz noise
        Notch notch3 {3*50./rate, 1./(3*12)}; // Notch filter to remove 150Hz noise
        for(int i: range(N)) {
            float x = signal[i];
            x = highpass(x);
            x = notch1(x);
            x = notch3(x);
            signal[i] = x;
        }
    }

    FFT fft {N};

    // Filter transfer
    ref<float> transfer() {
        real sine[N];
        for(int t: range(N)) sine[t] = sin(2*PI*t/N); // Precomputes sine lookup table

        buffer<float> signal {N};
        clear(signal.begin(), signal.size);
        for(int64 t: range(N)) {
            real s = 0; // Accumulate in a double to be sure no quantization noise is introduced (FIXME: useful?)
            for(int64 f=0; f<X; f+=2) { // Adds all sine in a single signal up to X/N
                s += sine[(f*t)%N]; // Synthesize 5 seconds of sine up to frequency X/N ~ 1050/N*rate ~ 1050/5 ~ 210Hz
            }
            signal[t] = s;
        }
        filter(signal); // Filters it with the same filter as noise signal
        return fft.powerSpectrum(signal).slice(0, X);
    }

    // Noise profile
    ref<float> profile() {
        Audio audio = decodeAudio("/Samples/F3-F5.flac"_, N);
        assert_(audio.rate == rate && audio.data.size == 2*N);
        buffer<float> signal {N};
        for(int i: range(N)) signal[i] = float(audio.data[i*2+0])+float(audio.data[i*2+1]); // Downmix
        //float DC=0; for(int i: range(N)) DC += signal[i]; DC /= N; // Computes DC (average) (High pass would be proper way)
        //for(int i: range(N)) signal[i] -= DC; // Removes DC (reduces rounding noise in FFT)
        //float maximum = 0; for(int i: range(N)) maximum=max(maximum, abs(signal[i])); // Computes signal range
        //for(int i: range(N)) signal[i] /= maximum; // Normalizes (FIXME: to what end?)

        filter(signal);
        return fft.powerSpectrum(signal).slice(0, X);
    }

    VList<Plot> plots {{transfer(), profile()}};
    Window window {&plots, int2(X, 2*X), "Filter"};
    DSP() {
        window.backgroundColor=window.backgroundCenter=0;
        window.show();
        window.localShortcut(Escape).connect([]{exit();});
    }
} app;
