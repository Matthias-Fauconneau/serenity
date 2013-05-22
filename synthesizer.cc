#include "thread.h"
#include "window.h"
#include "display.h"
#include "sequencer.h"
#include "asound.h"
#include "text.h"
#include "layout.h"
#include "spectrogram.h"

/// Displays a plot of Y
template<Type Array> void plot(int2 position, int2 size, const Array& Y) {
    float min=-1, max=1;
    for(uint x: range(Y.size)) {
        float y = Y[x];
        min=::min(min,y);
        max=::max(max,y);
    }
    vec2 scale = vec2(size.x/(Y.size-1.), size.y/(max-min));
    for(uint x: range(Y.size-1)) {
        vec2 a = vec2(position)+scale*vec2(x,  (max-min)-(Y[x]-min));
        vec2 b = vec2(position)+scale*vec2(x+1, (max-min)-(Y[x+1]-min));
        line(a,b);
    }
}
template<Type Array> void subplot(int2 position, int2 size, uint w, uint h, uint i, const Array& Y) {
    int2 subplot = int2(size.x/w,size.y/h);
    plot(position+int2(i%w,i/w)*subplot, subplot, Y);
}

/// Abstract base class for synthesized notes
struct Note {
    uint rate, key, velocity;
    Note(uint rate, uint key, uint velocity):rate(rate),key(key),velocity(velocity){}
    float period() { return rate/pitch(key); }
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
    buffer<float> delay;
    uint index=0;
    Delay(uint delay):delay(delay,0.f) { assert(delay>0 && delay<=65536); }
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
    LowPass loss {1./2, 1./2}; // 1st order low pass loss filter G(ω) = cos(ωT/2)
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

    static constexpr uint rate = 48000;
    signal<float* /*data*/, uint /*size*/> frameReady;
    map<uint, String> strings; // one string per key

#if ECHO
    const float h = 50; //m distance from floor/wall/ceiling (TODO: compute from geometry + source + target)
    const float d = 50; //m distance from source
    const float r = sqrt(sqr(h) + sqrt(d/2)); //m half indirect distance
    const float c = 340; //m/s speed of sound
    const float T = 1./rate; //s sampling interval
    const uint M = (2*r - d) / (c*T); //sample count in delay line
    const float g = (1/(2*r)) / (1/d); //dissipation along indirect path (relative to direct path)
    Delay echo {M};
#endif

    void noteEvent(uint key, uint velocity) {
        if(velocity) {
            if(!strings.contains(key)) strings.insert(key, String{rate, key, velocity});
            strings.at(key).pluck();
            clear(); //DEBUG: show only last visualization
            *this << &strings.at(key);
        } else if(strings.contains(key)) {
            strings.at(key).release();
        }
    }

    uint read(int32* output, uint periodSize) {
        float buffer[2*periodSize];
        ::clear(buffer,2*periodSize);

        // Synthesize all notes
        for(uint i=0; i<strings.size;) { Note& note=strings.values[i];
            if(note.read(buffer,periodSize)) i++;
            else { strings.keys.removeAt(i); strings.values.removeAt(i); clear(); if(strings) *this << &strings.values.last(); }
        }

#if ECHO
        for(uint i: range(periodSize)) {
            float direct = buffer[2*i];
            float indirect = g*echo;
            echo(direct);
            buffer[2*i+1]=buffer[2*i] = direct + indirect;
        }
#endif

        //TODO: reverb

        for(uint i: range(2*periodSize)) buffer[i] *= 0x1p28f; //32bit
        frameReady(buffer,periodSize);
        for(uint i: range(2*periodSize)) output[i] = buffer[i]; // Converts buffer to signed 32bit output
        contentChanged(); //FIXME: rate limit
        return periodSize;
    }
};

struct SynthesizerApp {
    Sequencer input;
    //KeyboardInput input;
    Synthesizer synthesizer;
    AudioOutput output {{&synthesizer, &Synthesizer::read}, synthesizer.rate, 1024};

    Spectrogram spectrogram {16384, output.rate, 32}; //~synthesizer.rate/(pitch(22)-pitch(21))
    Keyboard keyboard;

    VBox layout;
    Window window {&layout,int2(0,spectrogram.sizeHint().y),"Synthesizer"_};


    SynthesizerApp() {
        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);

        // Synthesizer
        //layout << &synthesizer;
        synthesizer.contentChanged.connect(&window,&Window::render);
        input.noteEvent.connect(&synthesizer,&Synthesizer::noteEvent);

        // Spectrogram
        synthesizer.frameReady.connect(&spectrogram,&Spectrogram::write);
        layout << &spectrogram;

        // Keyboard
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        keyboard.contentChanged.connect(&window,&Window::render);
        //layout << &keyboard;

        output.start();
    }
} application;
