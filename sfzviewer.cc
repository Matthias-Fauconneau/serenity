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
