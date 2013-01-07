#include "process.h"
#include "window.h"
#include "display.h"
#include "sequencer.h"
#include "asound.h"
#include "text.h"
#include "layout.h"
#if FFTW
#include <fftw3.h>
#endif

/// Trigonometric builtins
const float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
inline float log2(float x) { return __builtin_log2f(x); }
inline float exp2(float x) { return __builtin_exp2f(x); }

/// Displays a plot of Y
template<class Array> void plot(int2 position, int2 size, const Array& Y) {
    float min=-1, max=1;
    for(uint x: range(Y.size())) {
        float y = Y[x];
        min=::min(min,y);
        max=::max(max,y);
    }
    vec2 scale = vec2(size.x/(Y.size()-1.), size.y/(max-min));
    for(uint x: range(Y.size()-1)) {
        vec2 a = vec2(position)+scale*vec2(x,  (max-min)-(Y[x]-min));
        vec2 b = vec2(position)+scale*vec2(x+1, (max-min)-(Y[x+1]-min));
        line(a,b);
    }
}
template<class Array> void subplot(int2 position, int2 size, uint w, uint h, uint i, const Array& Y) {
    int2 subplot = int2(size.x/w,size.y/h);
    plot(position+int2(i%w,i/w)*subplot, subplot, Y);
}

/// Abstract base class for synthesized notes
struct Note {
    uint rate, key, velocity;
    Note(uint rate, uint key, uint velocity):rate(rate),key(key),velocity(velocity){}
    float period() { return rate/(440*exp2((int(key)-69)/12.0)); }
    virtual bool read(float* buffer, uint periodSize)=0;

    enum { Attack, Decay, Sustain, Release } state = Attack;
    virtual void release() { state=Release; }
};

// Physical modeling synthesis

// Low-pass filter: G(ω) = √( b₀² + b₁² + 2b₀b₁cos(ωT))
struct LowPass {
    float b0, b1; // filter coefficients
    float previous=0;
    LowPass(float b0, float b1):b0(b0),b1(b1){}
    float operator()(float in) { float out = b0*in + b1*previous; previous = out; return out; }
};

struct Delay {
    float* buffer=0;
    uint index=0, delay=0;
    Delay(uint delay):buffer(allocate<float>(delay)),delay(delay) { assert(delay>0 && delay<=65536); clear(buffer,delay); }
    move_operator(Delay):buffer(o.buffer),index(o.index),delay(o.delay){o.buffer=0;}
    ~Delay() { if(buffer) unallocate(buffer,delay); }
    operator float() { return buffer[index]; }
    void operator()(float in) { buffer[index]=in; index=(index+1)%delay; }
    float operator[](uint offset) const { uint i=(index+offset)%delay; return buffer[i]; }
    float& operator[](uint offset) { uint i=(index+offset)%delay; return buffer[i]; }
    float* begin() { return buffer; }
    float* end() { return buffer+delay; }
    uint size() const { return delay; }
};

struct String : Note, Widget {
    uint L; // String length in samples
    Delay delay1,delay2; // Propagating waves
    LowPass loss __(1./2, 1./2); // 1st order low pass loss filter G(ω) = cos(ωT/2)
    uint pickup; // Pick up position in samples
    float zero;
    String(uint rate, uint key, uint velocity) : Note(rate, key, velocity), L(Note::period()/2+1), delay1(L), delay2(L), pickup(L/4) {}
    void pluck() {
        zero -= delay1[pickup] + delay2[pickup]; // avoid discontinuity (FIXME)
        const uint pluck = L/8; // position on the string of the maximum of the triangle excitation
        for(uint x: range(0,pluck)) {
            float e = float(x)/pluck/2;
            delay1[x] += e, delay2[L-1-x] += e;
        }
        //FIXME: low pass pluck profile to avoid aliasing on attack | propagate acceleration/curvature impulse
        for(uint x: range(pluck,L)) {
            float e = float(L-x)/(L-1-pluck)/2;
            delay1[x] += e, delay2[L-1-x] += e;
        }
        zero += delay1[pickup] + delay2[pickup]; //avoid discontinuity (FIXME)
    }
    void release() { Note::release(); loss.b0=loss.b1= 1./2 * (1-2.f*L/rate); }
    bool read(float* buffer, uint periodSize) {
        for(uint i : range(periodSize)) {
            zero *= (1-2.f*L/rate); //decay offset to avoid discontinuity at last sample
            float sample = delay1[pickup] + delay2[pickup] - zero; // Pick-up
            buffer[2*i+0] += sample, buffer[2*i+1] += sample;

            float bridge = loss(-delay2);
            float nut = -delay1;
            delay1( bridge );
            delay2( nut );
        }
        if(state==Note::Release) {
            float E=0;
            for(float y : delay1) E += y*y;
            for(float y : delay2) E += y*y;
            E /= L;
            if(E < 0x1p-24) return false; //release decayed notes
        }
        return true;
    }

// Visualization
    int2 sizeHint() { return int2(-1,-1); }
    void render(int2 position, int2 size) {
        subplot(position,size,1,2,0, delay1);
        subplot(position,size,1,2,1, delay2);
    }
};

/// Manages a synthesized instrument
struct Synthesizer : VBox {
    // Display strings
    signal<> contentChanged;
    int2 sizeHint() { return int2(-1,4*256); }

    static constexpr uint rate = 48000; //96000
    signal<float* /*data*/, uint /*size*/> frameReady;
    map<uint, String> strings; // one string per key

    const float h = 50; //m distance from floor/wall/ceiling (TODO: compute from geometry + source + target)
    const float d = 50; //m distance from source
    const float r = sqrt(sqr(h) + sqrt(d/2)); //m half indirect distance
    const float c = 340; //m/s speed of sound
    const float T = 1./rate; //s sampling interval
    const uint M = (2*r - d) / (c*T); //sample count in delay line
    const float g = (1/(2*r)) / (1/d); //dissipation along indirect path (relative to direct path)
    Delay echo __(M);

    void noteEvent(uint key, uint velocity) {
        if(velocity) {
            if(!strings.contains(key)) strings.insert(key, String __(rate, key, velocity));
            strings.at(key).pluck();
            clear(); //DEBUG: show only last visualization
            *this << &strings.at(key);
        } else {
            strings.at(key).release();
        }
    }

    bool read(int16/*int32*/* output, uint periodSize) {
        float buffer[2*periodSize];
        ::clear(buffer,2*periodSize);

        // Synthesize all notes
        for(uint i=0; i<strings.size();) { Note& note=strings.values[i];
            if(note.read(buffer,periodSize)) i++;
            else { strings.keys.removeAt(i); strings.values.removeAt(i); clear(); if(strings) *this << &strings.values.last(); }
        }

        // Echo
        for(uint i: range(periodSize)) {
            float direct = buffer[2*i];
            float indirect = g*echo;
            echo(direct);
            buffer[2*i+1]=buffer[2*i] = direct + indirect;
        }

        //TODO: reverb

        for(uint i: range(2*periodSize)) buffer[i] *= 0x1p28f; //32bit
        frameReady(buffer,periodSize);
        for(uint i: range(2*periodSize)) buffer[i] /= 0x1p16f; //16bit
        for(uint i: range(2*periodSize)) output[i] = buffer[i]; // Converts buffer to signed 32bit output
        contentChanged(); //FIXME: rate limit
        return true;
    }
};

/// Displays active notes on a keyboard representation
struct Keyboard : Widget {
    array<int> midi, input;
    signal<> contentChanged;
    void inputNoteEvent(uint key, uint vel) { if(vel) { if(!input.contains(key)) input << key; } else input.removeAll(key); contentChanged(); }
    void midiNoteEvent(uint key, uint vel) { if(vel) { if(!midi.contains(key)) midi << key; } else midi.removeAll(key); contentChanged(); }
    int2 sizeHint() { return int2(-1,102); }
    void render(int2 position, int2 size) {
        int y0 = position.y;
        int y1 = y0+size.y*2/3;
        int y2 = y0+size.y;
        int margin = (size.x-size.x/88*88)/2;
        for(int key=0; key<88; key++) {
            vec4 white = midi.contains(key+21)?red:input.contains(key+21)?blue: ::white;
            int dx = size.x/88;
            int x0 = position.x + margin + key*dx;
            int x1 = x0 + dx;
            line(x0,y0, x0,y1-1, black);

            int notch[12] = { 3, 1, 4, 0, 1, 2, 1, 4, 0, 1, 2, 1 };
            int l = notch[key%12], r = notch[(key+1)%12];
            if(key==0) l=0; //A-1 has no left notch
            if(l==1) { // black key
                line(x0,y1-1, x1,y1-1, black);
                fill(x0+1,y0, x1+1,y1-1, midi.contains(key+21)?red:input.contains(key+21)?blue: ::black);
            } else {
                fill(x0+1,y0, x1,y2, white); // white key
                line(x0-l*dx/6,y1-1, x0-l*dx/6, y2, black); //left edge
                fill(x0+1-l*dx/6,y1, x1,y2, white); //left notch
                if(key!=87) fill(x1,y1, x1-1+(6-r)*dx/6,y2, white); //right notch
                //right edge will be next left edge
            }
            if(key==87) { //C7 has no right notch
                line(x1+dx/2,y0,x1+dx/2,y2, black);
                fill(x1,y0, x1+dx/2,y1-1, white);
            }
        }
    }
};

#if FFTW
struct Spectrogram : Widget {
    float* buffer;
    float* hann;
    float* windowed;
    float* spectrum;
    const uint T = 1050;
    uint N = audio.rate;
    const uint Y = 1050;
    uint t=0;
    float max=0x1p24f;
    Image spectrogram __(T,Y);

    Spectrogram() {
        fftwf_init_threads();
        fftwf_plan_with_nthreads(4);
        buffer = allocate64<float>(N); clear(buffer,N);
        hann = allocate64<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
        windowed = allocate64<float>(N);
        spectrum = allocate64<float>(N);
    }

    void viewFrame(float* data, uint size) {
        for(uint i: range(N-size)) buffer[i] = buffer[i+size]; // Shifts buffer
        for(uint i: range(size)) buffer[N-size+i] = data[i*2+0]+data[i*2+1]; // Appends latest frame
        for(uint i: range(N)) windowed[i] = hann[i]*buffer[i]; // Multiplies window
        // Transforms
        fftwf_plan p = fftwf_plan_r2r_1d(N, windowed, spectrum, FFTW_R2HC, FFTW_ESTIMATE);
        fftwf_execute(p);
        fftwf_destroy_plan(p);
        for(int i: range(N)) spectrum[i] /= N; // Normalizes

        float Nmax = N/2; //only real part
        float Nmin = 27.0*(N/2)/synthesizer.rate;

        // Updates spectrogram
        for(uint y: range(Y)) {
            uint n0 = floor(Nmin+exp2(log2(Nmax-Nmin)*(float(y)/Y))), n1=ceil(Nmin+exp2(log2(Nmax-Nmin)*(float(y)/Y)));
            //FIXME: all bins in a pixel will have same contribution
            float sum=0;
            for(uint n=n0; n<n1; n++) {
                float a = sqrt(sqr(spectrum[n]) + sqr(spectrum[N-1-n])); // squared amplitude
                if(n>N/4) a *= 0x1p8f;
                sum += a;
            }
            sum /= n1-n0;
            int v = sRGB[clip(0,int(255*((log2(sum)-16)/8)),255)]; // Logarithmic scale from 16-24bit
            spectrogram(t,Y-1-y) = byte4(v,v,v,1);
        }

        t++; if(t>=T) t=0;
        window.render();
    }
    int2 sizeHint() { return int2(T,Y); }
    void render(int2 position, int2) { blit(position,spectrogram); }
};
#endif

/// Dummy input for testing without a MIDI keyboard
struct KeyboardInput : Widget {
    signal<uint,uint> noteEvent;
    bool keyPress(Key key, Modifiers) override {
        int i = "awsedftgyhujkolp;']\\"_.indexOf(key);
        if(i>=0) noteEvent(60+i,100);
        return false;
    }
    bool keyRelease(Key key, Modifiers) override {
        int i = "awsedftgyhujkolp;']\\"_.indexOf(key);
        if(i>=0) noteEvent(60+i,0);
        return false;
    }
    void render(int2, int2){};
};

struct SynthesizerApp {
    //Thread thread;
    //Sequencer input; //__(thread);
    KeyboardInput input;
    Synthesizer synthesizer;
    AudioOutput audio __({&synthesizer, &Synthesizer::read}, synthesizer.rate, 1024);

    VBox layout;
    Window window __(&layout,int2(0,/*0*/512),"Synthesizer"_);

#if FFTW
    Spectrogram spectrogram;
#endif
    Keyboard keyboard;

    SynthesizerApp() {
        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);
        focus=&input;

        // Synthesizer
        layout << &synthesizer;
        synthesizer.contentChanged.connect(&window,&Window::render);
        input.noteEvent.connect(&synthesizer,&Synthesizer::noteEvent);

        // Spectrogram
#if FFTW
        synthesizer.frameReady.connect(&spectrogram,&Spectrogram::viewFrame);
        layout << this;
#endif

        // Keyboard
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        keyboard.contentChanged.connect(&window,&Window::render);
        layout << &keyboard;

        audio.start();
        //thread.spawn();
    }
} application;
