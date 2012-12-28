#if 1
#include "process.h"
struct LogTest {
    LogTest(){ log("Hello World"_); }
} test;
#endif

#if 0
#include "window.h"
#include "text.h"
struct TextInputTest {
    TextInput input __(readFile("TODO"_,cwd()));
    Window window __(&input,int2(1024,525),"TextInput"_);

    TextInputTest() {
        window.localShortcut(Escape).connect(&exit);
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

#if 1
#include "process.h"
#include "window.h"
#include "feeds.h"

struct FeedsTest {
    Feeds feeds;
    Scroll<HTML> page;
    Window window __(&feeds,0,"Feeds"_);
    Window browser __(0,0,"Browser"_);
    FeedsTest() {
        window.localShortcut(Escape).connect(&exit);
        feeds.listChanged.connect(&window,&Window::render);
        feeds.pageChanged.connect(this,&FeedsTest::showPage);
        browser.localShortcut(Escape).connect(&browser, &Window::destroy);
        browser.localShortcut(RightArrow).connect(&feeds, &Feeds::readNext);
    }
    void showPage(const ref<byte>& link, const ref<byte>& title, const Image& favicon) {
        if(!link) { browser.destroy(); exit(); return; } // Exits application to free any memory leaks (will be restarted by .xinitrc)
        page.delta=0;
        page.contentChanged.connect(&browser, &Window::render);
        if(!browser.created) browser.create(); //might have been closed by user
        browser.setTitle(title);
        browser.setIcon(favicon);
        browser.setType("_NET_WM_WINDOW_TYPE_NORMAL"_);
        page.go(link);
        browser.widget=&page.area(); browser.show();
    }
} application;
#endif

#if 0
#include "process.h"
#include "window.h"
#include "display.h"
#include "sequencer.h"
#include "asound.h"
#include "text.h"
#include "layout.h"
#include <fftw3.h>

/// Trigonometric builtins
const double PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
inline float log2(float x) { return __builtin_log2f(x); }
inline float exp2(float x) { return __builtin_exp2f(x); }

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

// Analog synthesis

struct LinearADSR : Note {
    float amplitudeStep;
    float amplitude = 0;
    LinearADSR(uint rate, uint key, uint velocity):Note(rate, key, velocity),amplitudeStep(1./rate){}
    bool next() {
        if(state==Attack) {
            amplitude += amplitudeStep;
            if(amplitude >= 1) amplitude=1, state=Decay;
        }
        /*else if(state==Decay) {
        amplitude -= amplitudeStep;
        if(amplitude <= 1./2) amplitude=1./2, state=Sustain;
        }*/ else if(state==Release) {
            amplitude -= amplitudeStep;
            if(amplitude<=0) return false;
        }
        return true;
    }
};

struct Square : LinearADSR {
    float x=0;
    float step;
    Square(uint rate, uint key, uint velocity) : LinearADSR(rate, key, velocity), step(2/Note::period()/*-1 / 1 in one period*/){}
    bool read(float* buffer, uint periodSize) override {
        for(uint i : range(periodSize)) {
            float sample = amplitude*(x > 0);
            buffer[2*i+0] += sample, buffer[2*i+1] += sample;

            x += step;
            if(x >= 1) x = -1;

            if(!next()) return false;
        }
        return true;
    }
};

struct Saw : LinearADSR {
    float y=0;
    float step;
    Saw(uint rate, uint key, uint velocity) : LinearADSR(rate, key, velocity), step(4/Note::period()/*-1 to 1 to -1 in one period*/){}
    bool read(float* buffer, uint periodSize) override {
        for(uint i : range(periodSize)) {
            float sample = amplitude*y;
            buffer[2*i+0] += sample, buffer[2*i+1] += sample;

            y += step;
            if(y >= 1) y = 1, step = -step;
            if(y <= -1) y = -1, step = -step;

            if(!next()) return false;
        }
        return true;
    }
};

struct Sinus : LinearADSR {
    float angle=0;
    float step;
    Sinus(uint rate, uint key, uint velocity) : LinearADSR(rate, key, velocity), step(2*PI/Note::period()){}
    bool read(float* buffer, uint periodSize) override {
        for(uint i : range(periodSize)) {
            float sample = amplitude*sin(angle);
            buffer[2*i+0] += sample, buffer[2*i+1] += sample;
            angle += step;
            if(!next()) return false;
        }
        return true;
    }
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
    Delay(uint delay):buffer(allocate<float>(delay)),delay(delay) { clear(buffer,delay); }
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
        const uint pluck = L/8; // position on the string of the maximum of the triangle excitation
        for(uint x: range(0,pluck)) {
            delay1[x] = float(x)/pluck/2;
            delay2[L-1-x] = float(x)/pluck/2;
        }
        //FIXME: smooth triangle profile (avoid aliasing on attack)
        for(uint x: range(pluck,L)) {
            delay1[x] = float(L-x)/(L-1-pluck)/2;
            delay2[L-1-x] = float(L-x)/(L-1-pluck)/2;
        }
        zero = delay1[pickup] + delay2[pickup]; //avoid discontinuity at first sample
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
    const uint rate = 96000;
    signal<float* /*data*/, uint /*size*/> frameReady;
    signal<> contentChanged;
    map<uint, String> strings; // one string per key

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

    int2 sizeHint() { return int2(-1,4*256); }
    bool read(int32* output, uint periodSize) {
        float buffer[2*periodSize];
        ::clear(buffer,2*periodSize);

        // Synthesize all notes
        for(uint i=0; i<strings.size();) { Note& note=strings.values[i];
            if(note.read(buffer,periodSize)) i++;
            else { strings.keys.removeAt(i); strings.values.removeAt(i); clear(); if(strings) *this << &strings.values.last(); }
        }

        //TODO: simulated (i.e not sampled convolution) reverb

        for(uint i: range(2*periodSize)) buffer[i] *= 0x1p28f;
        frameReady(buffer,periodSize);
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

/// Displays information on the played samples
struct SynthesizerTest : Widget {
    Thread thread;
    Sequencer input __(thread);
    Synthesizer synthesizer;
    AudioOutput audio __({&synthesizer, &Synthesizer::read}, synthesizer.rate, 1024, thread);

    Keyboard keyboard;
    VBox layout;
    Window window __(&layout,int2(0,0),"Synthesizer"_);

    float* buffer;
    float* hann;
    float* windowed;
    float* spectrum;
    const uint T = 1050;
    uint N = audio.rate; //*8 to get lowest notes
    const uint Y = 1050;
    uint t=0;
    float max=0x1p24f;
    Image spectrogram __(T,Y);

    int lastVelocity=0;

    SynthesizerTest() {
        layout << &synthesizer << this << &keyboard;

        synthesizer.frameReady.connect(this,&SynthesizerTest::viewFrame);
        synthesizer.contentChanged.connect(&window,&Window::render);

        input.noteEvent.connect(&synthesizer,&Synthesizer::noteEvent);
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        keyboard.contentChanged.connect(&window,&Window::render);

        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);

        fftwf_init_threads();
        fftwf_plan_with_nthreads(4);
        buffer = allocate64<float>(N); clear(buffer,N);
        hann = allocate64<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
        windowed = allocate64<float>(N);
        spectrum = allocate64<float>(N);

        audio.start();
        thread.spawn();
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
} test;

#endif

#if 0
#include "process.h"
#include "window.h"
#include "display.h"
#include "sequencer.h"
#include "sampler.h"
#include "asound.h"
#include "text.h"
#include "layout.h"

/// Displays active notes on a keyboard representation
struct Keyboard : Widget {
    array<int> midi, input;
    signal<> contentChanged;
    void inputNoteEvent(int key, int vel) { if(vel) { if(!input.contains(key)) input << key; } else input.removeAll(key); contentChanged(); }
    void midiNoteEvent(int key, int vel) { if(vel) { if(!midi.contains(key)) midi << key; } else midi.removeAll(key); contentChanged(); }
    int2 sizeHint() { return int2(-1,120); }
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

struct sRGB {
    uint8 lookup[256];
    inline float evaluate(float c) { if(c>=0.0031308) return 1.055*pow(c,1/2.4)-0.055; else return 12.92*c; }
    sRGB() { for(uint i=0;i<256;i++) { uint l = round(255*evaluate(i/255.f)); assert(l<256); lookup[i]=l; } }
    inline uint8 operator [](uint c) { assert(c<256,c); return lookup[c]; }
} sRGB;

const double PI = 3.14159265358979323846;
inline double cos(double t) { return __builtin_cos(t); }
inline float log2(float x) { return __builtin_log2f(x); }
inline float exp2(float x) { return __builtin_exp2f(x); }

/// Displays information on the played samples
struct SFZViewer : Widget {
    Thread thread;
    Sequencer input __(thread);
    Sampler sampler;
    AudioOutput audio __({&sampler, &Sampler::read}, 44100, 512, thread);

    Text text;
    Keyboard keyboard;
    VBox layout;
    Window window __(&layout,int2(0,1050+16+16+120+120),"SFZ Viewer"_);

    float* buffer;
    float* hann;
    float* windowed;
    float* spectrum;
    const uint T = 1050;
    uint N = audio.rate; //*8 to get lowest notes
    const uint Y = 1050;
    uint t=0;
    float max=0x1p24f;
    Image spectrogram __(T,Y);

    int lastVelocity=0;

    SFZViewer() {
        layout << this << &text << &keyboard;

        sampler.open("/Samples/Boesendorfer.sfz"_);
        sampler.frameReady.connect(this,&SFZViewer::viewFrame);

        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(this,&SFZViewer::noteEvent);
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        keyboard.contentChanged.connect(&window,&Window::render);

        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(Key('r')).connect(this,&SFZViewer::toggleReverb);

        fftwf_init_threads();
        fftwf_plan_with_nthreads(4);
        buffer = allocate64<float>(N); clear(buffer,N);
        hann = allocate64<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
        windowed = allocate64<float>(N);
        spectrum = allocate64<float>(N);

        audio.start();
        thread.spawn();
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
        float Nmin = 27.0*(N/2)/sampler.rate;

        // Updates spectrogram
        for(uint y: range(Y)) {
            uint n0 = floor(Nmin+exp2(log2(Nmax-Nmin)*(float(y)/Y))), n1=ceil(Nmin+exp2(log2(Nmax-Nmin)*(float(y)/Y)));
            //FIXME: all bins in a pixel will have same contribution
            float sum=0;
            for(uint n=n0; n<n1; n++) {
                float a = spectrum[n]; // amplitude
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
    int2 sizeHint() { return int2(T,Y+120); }
    void render(int2 position, int2 size) {
        blit(position,spectrogram);
        if(lastVelocity) { // Displays volume of all notes for a given velocity layer
            int margin = (size.x-size.x/88*88)/2;
            int dx = size.x/88;
            int y0 = position.y+1050;
            int y1 = size.y;
            float level[88]={}; float max=0;
            for(int key=0;key<88;key++) {
                for(const Sample& s : sampler.samples) {
                    if(s.trigger == 0 && s.lokey <= (21+key) && (21+key) <= s.hikey) {
                        if(s.lovel<=lastVelocity && lastVelocity<=s.hivel) {
                            if(level[key]!=0) { error("Multiple match"); }
                            level[key] = s.data.actualLevel(0.25*44100);
                            max = ::max(max, level[key]);
                            break;
                        }
                    }
                }
            }
            for(int key=0;key<88;key++) {
                int x0 = position.x + margin + key * dx;
                fill(x0,y1-level[key]/max*(y1-y0),x0+dx,y1);
            }
        }
    }

    void noteEvent(int key, int velocity) {
        if(!velocity) return;
        lastVelocity=velocity;
        string text;
        for(int last=0;;) {
            int lovel=0xFF; int hivel;
            for(const Sample& s : sampler.samples) {
                if(s.trigger == 0 && s.lokey <= key && key <= s.hikey) {
                    if(s.lovel>last && s.lovel<lovel) lovel=s.lovel, hivel=s.hivel;
                }
            }
            if(lovel==0xFF) break;
            if(lovel <= velocity && velocity <= hivel)
                text << str(lovel)<<"<"_<<format(Bold)<<dec(velocity,2)<<format(Regular)<<"<"_;
            else
                text << str(lovel)<<"    "_;
            last = lovel;
        }
        this->text.setText(text);
    }
    void toggleReverb() {
        sampler.enableReverb=!sampler.enableReverb;
        window.setTitle(sampler.enableReverb?"Wet"_:"Dry"_);
    }
} test;

#endif

#if 0
#include "window.h"
#include "pdf.h"
#include "interface.h"
struct Book {
    string file;
    Scroll<PDF> pdf;
    Window window __(&pdf.area(),int2(0,0),"Book"_);
    Book() {
        if(arguments()) file=string(arguments().first());
        else if(existsFile("Books/.last"_)) {
            string mark = readFile("Books/.last"_);
            ref<byte> last = section(mark,0);
            if(existsFile(last)) {
                file = string(last);
                pdf.delta.y = toInteger(section(mark,0,1,2));
            }
        }
        pdf.open(readFile(file,root()));
        window.backgroundCenter=window.backgroundColor=0xFF;
        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(UpArrow).connect(this,&Book::previous);
        window.localShortcut(LeftArrow).connect(this,&Book::previous);
        window.localShortcut(RightArrow).connect(this,&Book::next);
        window.localShortcut(DownArrow).connect(this,&Book::next);
        window.localShortcut(Power).connect(this,&Book::next);
    }
    void previous() { pdf.delta.y += window.size.y/2; window.render(); save(); }
    void next() { pdf.delta.y -= window.size.y/2; window.render(); save(); }
    void save() { writeFile("Books/.last"_,string(file+"\0"_+dec(pdf.delta.y))); }
} application;
#endif

#if 0
#include "window.h"
#include "calendar.h"
#include "png.h"
struct WeekViewRenderer : Widget {
    struct WeekView : Widget {
        uint time(Date date) { assert(date.hours>=0 && date.minutes>=0, date); return date.hours*60+date.minutes; }
        inline uint floor(uint width, uint value) { return value/width*width; }
        inline uint ceil(uint width, uint value) { return (value+width-1)/width*width; }
        void render(int2 position, int2 size) {
            const int workWeek=4;
            int w = size.x/workWeek, y=0;
            for(int i=0;i<workWeek;i++) {
                constexpr ref<byte> days[5]={"Lundi"_,"Mardi"_,"Mercredi"_,"Jeudi"_,"Vendredi"_};
                Text day(string(days[i]),64);
                y=max(y,day.sizeHint().y);
                day.render(position+int2(i*w,0),int2(w,0));
            }
            array< ::Event> events = getEvents(Date());
            uint min=-1,max=0;
            for(const ::Event& e: events) if(e.date.day==-1 && e.date.weekDay!=-1 && e.end!=e.date) min=::min(min,floor(60,time(e.date))), max=::max(max,ceil(60,time(e.end)));
            for(const ::Event& e: events) if(e.date.day==-1 && e.date.weekDay!=-1 && e.end!=e.date) { // Displays only events recurring weekly
                int x = e.date.weekDay*w;
                int begin = y+(size.y-y)*(time(e.date)-min)/(max-min);
                fill(position+int2(x,begin-2)+Rect(int2(w,3)));
                int end = y+(size.y-y)*(time(e.end)-min)/(max-min);
                fill(position+int2(x,end-2)+Rect(int2(w,3)));
                fill(position+int2(x,begin)+Rect(int2(3,end-begin)));
                fill(position+int2(x+w-2,begin)+Rect(int2(3,end-begin)));
                Text time(str(e.date,"hh:mm"_)+(e.date!=e.end?string("-"_+str(e.end,"hh:mm"_)):string()),64);
                time.render(position+int2(x, begin+3),int2(w,0));
                Text title(copy(e.title),64);
                title.render(position+int2(x, begin),int2(w,end-begin));
            }
        }
    };

    Window window __(this,0,"WeekView"_);
    Image page __(2480,3508);
    WeekViewRenderer(){
        renderPage();
        writeFile("week.png"_,encodePNG(page),home());
        window.localShortcut(Escape).connect(&exit); window.backgroundCenter=window.backgroundColor=0xFF;
    }
    void renderPage() {
        framebuffer=share(page); currentClip = Rect(page.size());
        fill(Rect(framebuffer.size()),white);
        WeekView().render(int2(16,16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
        WeekView().render(int2(16,framebuffer.size().y/2+16),int2(framebuffer.size().x-32,framebuffer.size().y/2-32));
    }
    void render(int2 position, int2 unused size) { blit(position,resize(page,page.width/2,page.height/2)); }
} test;
#endif
