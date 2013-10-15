#include "tuner.h"
#include "thread.h"
#include "audio.h"
#include "layout.h"
#include "window.h"
#include "display.h"
#include <fftw3.h> //fftw3f
#define CAPTURE 0 //DEBUG
#if !CAPTURE
#include "sequencer.h"
#include "sampler.h"
#include "time.h"
#include "keyboard.h"
#include "text.h"
#endif

generic uint partition(const mref<T>& at, int left, int right, int pivotIndex) {
    swap(at[pivotIndex], at[right]);
    const T& pivot = at[right];
    uint storeIndex = left;
    for(uint i: range(left,right)) {
        if(at[i] < pivot) {
            swap(at[i], at[storeIndex]);
            storeIndex++;
        }
    }
    swap(at[storeIndex], at[right]);
    return storeIndex;
}
generic T quickselect(const mref<T>& at, int left, int right, int k) {
    for(;;) {
        int pivotIndex = partition(at, left, right, (left + right)/2);
        int pivotDist = pivotIndex - left + 1;
        if(pivotDist == k) return at[pivotIndex];
        else if(k < pivotDist) right = pivotIndex - 1;
        else { k -= pivotDist; left = pivotIndex + 1; }
    }
}
generic T median(const mref<T>& at) { assert(at.size%2==1); return quickselect(at, 0, at.size-1, at.size/2); }
generic T median(const ref<T>& at) { T buffer[at.size]; rawCopy(buffer,at.data,at.size); return median(mref<T>(buffer,at.size)); }

FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }

Spectrum::Spectrum(uint sampleRate, uint periodSize) : sampleRate(sampleRate), periodSize(periodSize) {
    signal = buffer<float>(N);
    hann = buffer<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
    windowed = buffer<float>(N);
    transform = buffer<float>(N);
    spectrum = buffer<float>(N/2);
    harmonic = buffer<float>(N/2/octaveCount-smoothRadius);
    fftw = fftwf_plan_r2r_1d(N, windowed.begin(), transform.begin(), FFTW_R2HC, FFTW_ESTIMATE);
}

uint Spectrum::write(int16* data, uint size) {
    for(uint i: range(N-size)) signal[i] = signal[i+size]; // Shifts buffer (FIXME: ring buffer)
    for(uint i: range(size)) signal[N-size+i] = float(data[i*2]+data[i*2+1])/(2<<30); // Appends new data
    return write(size);
}
uint Spectrum::write(int32* data, uint size) {
    for(uint i: range(N-size)) signal[i] = signal[i+size]; // Shifts buffer (FIXME: ring buffer)
    for(uint i: range(size)) signal[N-size+i] = float(data[i*2]+data[i*2+1])/(2<<30); // Appends new data
    return write(size);
}
uint Spectrum::write(uint size) {
    for(uint i: range(N)) windowed[i] = hann[i]*signal[i]; // Multiplies window
    fftwf_execute(fftw); // Transforms
    spectrumMaximum = 0;
    for(int i: range(N/2)) { // Converts to "power" spectrum
        float energy = sq(transform[i]) + sq(transform[N-1-i]);
        spectrum[i] = energy / N;
        if(spectrum[i]>spectrumMaximum) spectrumMaximum=spectrum[i];
    }
    maximum = 0;
    for(int i: range(smoothRadius,N/2/octaveCount-smoothRadius)) { // Evaluates spectral harmonic correlation H(f) = Π(r:1..2) Σ(df:±window]) S(rf+df)
        float h=1;
        for(uint r=1; r<=octaveCount; r++) {
            float s=0;
            for(int di=-smoothRadius; di<=smoothRadius; di++) {
                assert(r*i+di<N/2, r,i,di, r*i, r*i+di);
                s += spectrum[r*i+di];
            }
            h *= s;
        }
        if(h>maximum) maximum=h, f0=(float)i*sampleRate/N;
        harmonic[i] = h;
    }
    if(maximum>threshold) {
        //TODO: merit estimation, multiple candidates, refine using time domain normalized cross correlation
        for(uint i: range(medianSize-1)) pitches[i]=pitches[i+1]; pitches[medianSize-1]=f0; // Shifts history
        pitch = median(ref<float>(pitches));
    }
    contentChanged();
    return size;
}

// TODO: Railsback law
inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }

void Spectrum::render(int2, int2 size) {
    if(maximum<threshold) return;
    //if(!maximum) return; //if(maximum<0x1p-24) return;
    int dx = round(size.x/88.f);
    int margin = (size.x-88*dx)/2;
#if 1
    for(uint x: range(size.x)) { // Integrate bands over single pixels
        int f = x - margin; // Align right
        if(f<0) continue; // if size.x > 88·12px
        uint n0 = floor(keyToPitch(21+(f-6)/12.0)/sampleRate*N);
        uint n1 = ceil(keyToPitch(21+(f-6+1)/12.0)/sampleRate*N);
        assert(n0<=n1);
        float value = 0;
        for(uint n=n0; n<=n1; n++) value = max(value,spectrum[n]); // Downsamples to display resolution by keeping maximum bin
        float h = (value / spectrumMaximum) * (size.y-1);
        int y0 = round(h);
        for(int y: range(size.y-1-y0+1,size.y)) framebuffer(x, y) = 0xFF;
        if(h<size.y-1) framebuffer(x, size.y-1-y0) = sRGB(h-y0);
    }
#else
    for(uint k: range(88)) { // Integrate bands over single keys
        int x0 = k*dx+margin; // Key start
        if(x0+dx<0 || x0>size.x) continue;
        uint n0 = floor(keyToPitch(21+k-0.5+offset)/sampleRate*N);
        uint n1 = ceil(keyToPitch(21+k+0.5+offset)/sampleRate*N);
        assert(n1>n0);
        float value = 0;
        for(uint n=n0; n<=n1; n++) value = max(value,harmonic[n]); // Downsamples to display resolution by keeping maximum bin
        float h = (value / maximum) * (size.y-1);
        int y0 = round(h);
        for(int y: range(size.y-1-y0+1,size.y)) for(int x: range(max(0,x0),min(x0+dx,size.x))) framebuffer(x, y) = 0xFF;
        if(h<size.y-1) for(int x: range(max(0,x0),min(x0+dx,size.x))) framebuffer(x, size.y-1-y0) = sRGB(h-y0);
    }
#endif
    for(uint k: range(1,88)) {
        int x = k*dx+margin; // Key start
        if(x>=0 && x<size.x) for(uint y: range(size.y)) framebuffer(x, y).g = 0xFF;
    }
}

struct Tuner : Widget {
    const uint sampleRate = 48000;
#if CAPTURE
    const uint periodSize = 1024; // Offset between each STFT [48fps]
    AudioInput input{{&spectrum,&Spectrum::write}, sampleRate, periodSize}; //CA0110 driver doesn't work
#else
    Thread thread{-20};
    Sequencer input{thread};
    Sampler sampler;
    const uint periodSize = Sampler::periodSize; // Offset between each STFT [24fps]
#if AUDIO
    AudioOutput audio{{this, &Tuner::read}, sampleRate, Sampler::periodSize, thread};
#else
    Timer timer;
#endif
    float lastPitch=0;
#endif
    Spectrum spectrum {sampleRate, periodSize};
    Keyboard keyboard;
    VBox layout{{ &spectrum, &keyboard, this }};
    Window window {&layout, int2(1024,600),"Tuner"_};
    Tuner() {
        spectrum.contentChanged.connect(&window,&Window::render);
        window.localShortcut(Escape).connect([]{exit();});
        window.backgroundCenter=window.backgroundColor=0; //window.clearBackground = false;
        window.show();
#if CAPTURE
        input.start();
#else
        sampler.open(sampleRate, "Salamander.sfz"_, Folder("Samples"_,root()));
        sampler.enableReverb = false;
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        thread.spawn();
#if AUDIO
        audio.start();
#else
        timer.timeout.connect(this, &Tuner::update);
        update();
#endif
#endif
    }
#if !AUDIO
    void update() {
        int32 output[periodSize*2];
        read(output, periodSize);
        timer.setRelative(periodSize*1000/sampleRate);
    }
#endif
#if !CAPTURE
    uint read(int32* output, uint size) {
        uint read = sampler.read(output, size);
        spectrum.write(output, read);
        return read;
    }
#endif
    void render(int2 position, int2 size) {
        //TODO: display fundamental offset to reference
        if(spectrum.pitch) lastPitch = spectrum.pitch;
        int key = round(pitchToKey(lastPitch))-21+9;
        if(lastPitch) Text(split("C C# D D# E F F# G G# A A# B"_)[key%12]+str((key/12)-1)+"\n"_
                +str(key)+" \n"_
                        +str(lastPitch)+" Hz\n"_
                        +str(sampleRate/lastPitch)+" samples"_
                ,32,1).render(position, size);
    }
} application;
