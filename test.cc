#if 1
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

/// Displays information on the played samples
struct SFZViewer {
    Thread thread;
    Sequencer input __(thread);
    Sampler sampler;
    AudioOutput audio __({&sampler, &Sampler::read},thread,true);

    Text text;
    Keyboard keyboard;
    VBox layout;
    Window window __(&layout,int2(0,120),"SFZ Viewer"_);

    SFZViewer() {
        layout << &text << &keyboard;
        sampler.open("/Samples/Boesendorfer.sfz"_);

        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(this,&SFZViewer::noteEvent);
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
        keyboard.contentChanged.connect(&window,&Window::render);

        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Escape).connect(&exit);

        audio.start();
        thread.spawn();
    }
    void noteEvent(int key, int velocity) {
        if(!velocity) return;
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
} test;

#endif

#if 0
#include "process.h"
#include "file.h"
#include "inflate.h"
#include "xml.h"

struct NKI {
    NKI() {
        Element root = parseXML(inflate(readFile("/Samples/Boesendorfer.nki"_).slice(170),true));
        string sfz;
        root.xpath("K2_Container/Programs/K2_Program/Zones/K2_Zone"_, [&sfz](const Element& zone){
            uint lowKey=0, highKey=0, rootKey=0, lowVelocity=0, highVelocity=0;
            zone.xpath("Parameters/V"_, [&](const Element& V){
                ref<byte> name = V["name"_];
#define value toInteger(V["value"_]);
                if(name=="lowKey"_) lowKey=value;
                if(name=="highKey"_) highKey=value;
                if(name=="rootKey"_) rootKey=value;
                if(name=="lowVelocity"_) lowVelocity=value;
                if(name=="highVelocity"_) highVelocity=value;
            });
            if(highKey < 21) return;
            if(zone["groupIdx"_]=="1"_) return; // Hammer noise
            if(zone["groupIdx"_]=="3"_) return; // Sustain

            sfz<<"<region> "_;
            if(zone["groupIdx"_]=="2"_) sfz<<"trigger=release "_;

            string path; for(TextData s (zone("Sample"_)("V"_)["value"_]);s;) {
                if(s.match("@b"_)) {}
                else if(s.match(" "_)) {}
                else if(s.match("F-00"_)) s.advance(8), path<<'/';
                else if(s.match("d0"_)) s.advance(2), path<<'/';
                else if(s.match("WAV"_)) path<<"flac"_;
                else if(s.match("wav"_)) path<<"flac"_;
                else path << s.next();
            }
            sfz<<"sample="_+path+" "_;

            if(lowKey == highKey) sfz<<"key="_+str(lowKey)+" "_;
            else sfz<<"lokey="_+str(lowKey)+" hikey="_+str(highKey);
            if(rootKey && (rootKey!=lowKey || rootKey!=highKey)) sfz<<" pitch_keycenter="_+str(rootKey)+" "_;
            sfz<<"lovel="_+str(lowVelocity)+" hivel="_+str(highVelocity)<<" "_;
            sfz<<"ampeg_release=1"_;
            sfz<<'\n';
        });
        log(sfz);
        writeFile("/Samples/Boesendorfer.sfz"_,sfz);
    }
} test;
#endif

#if 0
#include "window.h"
#include "display.h"
#include "time.h"
#include "flac.h"
#include <fftw3.h>

struct Plot : Widget {
    Window window __(this,int2(1050,3*1050/2),"Plot"_);

    static constexpr uint periodSize = 128;
    uint reverbSize=0; // Reverb filter size
    uint N=0; // reverbSize+periodSize

    float* filter[2];

    float* input; // Input signal
    float* output; // Output signal

    float* reverbFilter[2]={}; // Convolution reverb filter in frequency-domain
    float* reverbBuffer[2]={}; // Mixer output in time-domain

    float* inputBuffer=0; // Buffer to hold transform of reverbBuffer
    float* product=0; // Buffer to hold multiplication of signal and reverbFilter
    float* outputBuffer=0; // Buffer to hold transform of reverbBuffer

    fftwf_plan forward[2]; // FFTW plan to forward transform reverb buffer
    fftwf_plan backward; // FFTW plan to backward transform product

    uint signalSize=0;
    float* signal[2];

    Plot(){
        window.localShortcut(Escape).connect(&exit);
        window.backgroundCenter=window.backgroundColor=0xFF;

        /// Loads filter
        array<byte> reverbFile = readFile("/Samples/reverb.flac"_);
        FLAC reverbMedia(reverbFile);
        assert(reverbMedia.rate == 48000);
        reverbSize = reverbMedia.duration;
        N = reverbSize+periodSize;
        float* stereoFilter = allocate64<float>(2*reverbSize);
        for(uint i=0;i<reverbSize;) {
            uint read = reverbMedia.read((float2*)(stereoFilter+2*i),min(1u<<16,reverbSize-i));
            i+=read;
        }

        {
            // Computes normalization
            float sum=0; for(uint i: range(2*reverbSize)) sum += stereoFilter[i]*stereoFilter[i];
            const float scale = 1.f/sqrt(sum); //8 (24bit->32bit) - 3 (head room)

            // Reverses, scales and deinterleaves filter
            for(int c=0;c<2;c++) filter[c] = allocate64<float>(N), clear(filter[c],N);
            for(uint i: range(reverbSize)) for(int c=0;c<2;c++) filter[c][N-1-i] = scale*stereoFilter[2*i+c];
            unallocate(stereoFilter,reverbSize);
        }

        /// Transforms filter
        fftwf_init_threads();
        fftwf_plan_with_nthreads(4);
        fftwf_import_wisdom_from_filename("/Samples/wisdom");

        // Transform reverb filter to frequency domain
        for(int c=0;c<2;c++) {
            reverbFilter[c] = allocate64<float>(N); clear(reverbFilter[c],N);
            fftwf_plan p = fftwf_plan_r2r_1d(N, filter[c], reverbFilter[c], FFTW_R2HC, FFTW_ESTIMATE);
            fftwf_execute(p);
            fftwf_destroy_plan(p);
        }

        // Allocates reverb buffer and temporary buffers
        input = allocate64<float>(periodSize*2);
        output = allocate64<float>(periodSize*2);
        inputBuffer = allocate64<float>(N);
        for(int c=0;c<2;c++) {
            reverbBuffer[c] = allocate64<float>(N), clear(reverbBuffer[c],N);
            forward[c] = fftwf_plan_r2r_1d(N, reverbBuffer[c], inputBuffer, FFTW_R2HC, FFTW_ESTIMATE);
        }
        product = allocate64<float>(N);
        outputBuffer = allocate64<float>(N);
        backward = fftwf_plan_r2r_1d(N, product, outputBuffer, FFTW_HC2R, FFTW_ESTIMATE);

        fftwf_export_wisdom_to_filename("/Samples/wisdom");

        /// Loads signal
        array<byte> signalFile = readFile("/Samples/Salamander/A4v8.flac"_);
        FLAC signalMedia(signalFile);
        assert(signalMedia.rate == 48000);
        signalSize = min(signalMedia.duration,reverbSize);
        float* stereoSignal = allocate64<float>(2*signalSize); clear(stereoSignal,2*signalSize);
        for(uint i=0;i<24000;) {
            uint read = signalMedia.read((float2*)(stereoSignal+2*i),min(1u<<16,24000-i));
            i+=read;
        }

        {
            // Computes normalization
            float sum=0; for(uint i: range(2*signalSize)) sum += stereoSignal[i]*stereoSignal[i];
            const float scale = 1/sqrt(sum/signalSize); //8 (24bit->32bit) - 3 (head room)

            // Reverses, scales and deinterleaves filter
            for(int c=0;c<2;c++) signal[c] = allocate64<float>(signalSize), clear(signal[c],signalSize);
            for(uint i: range(signalSize)) for(int c=0;c<2;c++) signal[c][i] = scale*stereoSignal[2*i+c];
            unallocate(stereoSignal,signalSize);
        }
    }

    void plot(int2 position, int2 size, float* Y, uint N, uint stride=1) {
        float min=-1, max=1;
        for(uint x=0;x<N;x++) {
            float y = Y[stride*x];
            min=::min(min,y);
            max=::max(max,y);
        }
        GLBuffer buffer(Line);
        buffer.allocate(0,2*(N-1),sizeof(vec2));
        vec2* vertices = (vec2*)buffer.mapVertexBuffer();
        for(uint x=0;x<N-1;x++) {
            vertices[2*x+0] = project(vec2(position)+vec2((float)(x-0.5)*size.x/N,  size.y-size.y*(Y[stride*x]-min)/(max-min)));
            vertices[2*x+1] = project(vec2(position)+vec2((float)(x+0.5)*size.x/N, size.y-size.y*(Y[stride*(x+1)]-min)/(max-min)));
        }
        buffer.unmapVertexBuffer();
        fillShader().bind();
        fillShader()["color"] = vec4(1,1,1,1);
        buffer.bindAttribute(fillShader(),"position",2);
        buffer.draw();
    }

    uint t=0;
    void render(int2 position, int2 size) {
        clear(input, periodSize*2);
        for(uint i: range(periodSize)) {
            input[2*i+0]=signal[0][t];
            input[2*i+1]=signal[1][t];
            t++;
            if(t==signalSize) t=0;
        }

        // Deinterleaves mixed signal into reverb buffer
        for(uint i: range(periodSize)) for(int c=0;c<2;c++) reverbBuffer[c][reverbSize+i] = input[2*i+c];

        for(int c=0;c<1;c++) {
            // Transforms reverb buffer to frequency-domain ( reverbBuffer -> inputBuffer )
            fftwf_execute(forward[c]);

            for(uint i: range(reverbSize)) reverbBuffer[c][i] = reverbBuffer[c][i+periodSize]; // Shifts buffer for next frame

            // Complex multiplies input (reverb buffer) with kernel (reverb filter)
            float* x = inputBuffer;
            float* y = reverbFilter[c];
            product[0] = x[0] * y[0];
            for(uint j = 1; j < N/2; j++) {
                float a = x[j];
                float b = x[N - j];
                float c = y[j];
                float d = y[N - j];
                product[j] = a*c-b*d;
                product[N - j] = a*d+b*c;
            }
            product[N/2] = x[N/2] * y[N/2];

            // Transforms product back to time-domain ( product -> outputBuffer )
            fftwf_execute(backward);

            for(uint i: range(N)) outputBuffer[i]/=N; // Normalizes
            for(uint i: range(periodSize)) { //Writes samples back in output buffer
                output[2*i+c] = outputBuffer[/*N-periodSize-1+*/periodSize+i];
                //output[2*i+c] = (1.f/N)*outputBuffer[reverbSize/2-1+i];
            }
        }

        int2 plotSize = int2(size.x/2,size.y/3);
        // Filter / Time - Frequency
        //plot(position+int2(0,0)*plotSize,plotSize,filter[0],N); plot(position+int2(1,0)*plotSize,plotSize,reverbFilter[0],N);
        // Signal / Input - Output
        plot(position+int2(0,0)*plotSize,plotSize,input,periodSize,2); plot(position+int2(1,0)*plotSize,plotSize,output,periodSize,2);
        // Buffer / Time - Frequency
        plot(position+int2(0,1)*plotSize,plotSize,reverbBuffer[0],N); plot(position+int2(1,1)*plotSize,plotSize,inputBuffer,N);
        // Product / Time - Frequency
        plot(position+int2(0,2)*plotSize,plotSize,outputBuffer,N); plot(position+int2(1,2)*plotSize,plotSize,product,N);

        window.render();
    }
} test;

#endif

#if 0
#include "window.h"
#include "text.h"
struct TextInputTest {
    TextInput input __(readFile("ternary.l"_,cwd()));
    Window window __(&input,int2(525,525),"TextInput"_);

    TextInputTest() {
        window.localShortcut(Escape).connect(&exit);
    }
} test;
#endif

#if 0
#include "window.h"
#include "display.h"
#include "raster.h"

// Test correct top-left rasterization rules using a simple triangle fan
struct PolygonTest : Widget {
    Window window __(this,int2(4*64,6*64),"PolygonTest"_);
    PolygonTest(){
        window.localShortcut(Escape).connect(&exit);
        window.backgroundColor=window.backgroundCenter=0xFF;
    }
    void render(int2 position, int2 size) {
        RenderTarget target(4*64*4,6*64*4);
        RenderPass<vec4,1> pass(target,4);
        mat4 M; M.scale(64*4);
        pass.submit(M*vec3(2,3,0),M*vec3(1,1,0),M*vec3(3,1,0),(vec3[]){vec3(0,0,0)},vec4(0,0,0,1./2)); //bottom
        pass.submit(M*vec3(2,3,0),M*vec3(3,1,0),M*vec3(3,5,0),(vec3[]){vec3(0,0,0)},vec4(0,0,1,1./2)); // right
        pass.submit(M*vec3(2,3,0),M*vec3(3,5,0),M*vec3(1,5,0),(vec3[]){vec3(0,0,0)},vec4(0,1,0,1./2)); //top
        pass.submit(M*vec3(2,3,0),M*vec3(1,5,0),M*vec3(1,1,0),(vec3[]){vec3(0,0,0)},vec4(1,0,0,1./2)); //left
        function<vec4(vec4,float[0])> flat = [](vec4 color,float[0]){return color;};
        pass.render(flat);
        target.resolve(position,size);
    }
} test ;
#endif

#if 0
#include "process.h"
#include "window.h"
#include "text.h"
struct KeyTest : Text {
    Window window __(this,int2(640,480),"KeyTest"_);
    KeyTest(){ focus=this; window.localShortcut(Escape).connect(&exit); }
    bool keyPress(Key key) { setText(str("'"_+str((char)key)+"'"_,dec(int(key)),"0x"_+hex(int(key)))); return true; }
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
#include "process.h"
#include "data.h"
#include "string.h"
#include "display.h"
#include "text.h"
#include "widget.h"
#include "window.h"
#include "pdf.h" //mat32
#include "png.h"

struct Wing : Widget {
    int width = 2*1280, height = width/sqrt(2);
    Window window __(this,int2(1280,height/2),"Graph"_);
    struct Point : vec2 { Point(vec2 v, string&& label, float p):vec2(v),label(move(label)),p(p){} string label; float p; };
    struct Curve : array<Point> { vec2 min,mean,max; string name; float integral=0; };
    array< Curve > curves;
    Image page;
    Wing() {
        window.localShortcut(Escape).connect(&exit);
        window.backgroundColor=window.backgroundCenter=0xFF;

        parse("M1PAM/wing"_);

        Image page __(width,height);
        framebuffer=share(page); currentClip = Rect(page.size());
        fill(Rect(framebuffer.size()),white);
        render(int2(0,0),page.size());
        writeFile("plot.png"_,encodePNG(page),home());
        this->page=move(page);
    }
    void parse(const ref<byte>& path) {
        TextData text = readFile(path,home());
        typedef ref<byte> Field;
        map<ref<byte>, double> parameters;
        for(Field e: split(text.line(),'\t')) parameters[section(e,'=')]=toDecimal(section(e,'=',1,2));
        array<Field> headers = split(text.line(),'\t');
        int columns = headers.size();
        array< array<Field> > data; data.grow(columns);
        while(text) {
            array<Field> fields = split(text.line(),'\t');
            assert(fields.size()==headers.size(),fields);
            uint i=0; for(Field field: fields) data[i++] << field;
        }
        array<string> labels; for(Field label: data[0]) labels<< string(label);
        array<float> X; for(Field x: data[1]) X<< toDecimal(x);
        array<float> Y; for(Field y: data[2]) Y<< toDecimal(y);
        array<Curve> curves;
        for(uint a: range(3,data.size())) {
            Curve curve;
            curve.name = string(headers[a]);
            float angle = toDecimal(curve.name)*PI/180;
            mat32 M(cos(angle),-sin(angle),sin(angle),cos(angle),0,0);
            const array<Field>& H = data[a];
            for(uint i: range(H.size())) {
                float p = (toDecimal(H[i])-parameters["hS"_])/(parameters["hT"_]-parameters["hS"_]);
                vec2 pos(X[i],Y[i]);
                curve<< Point(M*pos, copy(labels[i]), p);
            }
            curves << move(curve);
        }
        vec2 min=0,max=0;
        for(Curve& curve: curves) {
            vec2 sum=0;
            for(vec2 p: curve) {
                if(p.x<min.x) min.x=p.x; else if(p.x>max.x) max.x=p.x;
                if(p.y<min.y) min.y=p.y; else if(p.y>max.y) max.y=p.y;
                sum += p;
            }
            curve.mean = sum/float(curve.size());
        }
        float delta = (max.x-min.x)-(max.y-min.y);
        min.y -= delta/2, max.y += delta/2;
        assert((max.x-min.x)==(max.y-min.y));
        for(Curve& curve: curves) {
            curve.min=min, curve.max=max;
            this->curves << move(curve);
        }
    }
    void render(int2 position, int2 size) {
        if(page) { blit(position,resize(page,size.x,page.height*size.x/page.width)); return; }
        int width = size.x/6, margin=width/6;
        int height = size.y/1;
        position+=int2(0,height/4);
        for(uint a: range(curves.size())) {
            int x = margin/2+a*(width+margin);
            Text(string("Pitch = "_+curves[a].name+"°"_),32).render(position+int2(x+width/2,0)+int2(-width/2,0),int2(width,0)); //column title
            plot(curves[a], position+int2(x,(height-width)/4), int2(width,width));
        }
    }
    void plot(const Curve& curve, int2 position, int2 size) {
        vec2 scale = vec2(size)/(curve.max-curve.min), offset = -curve.min*scale;
        mat32 M(scale.x,0,0,-scale.y,position.x+offset.x,position.y+size.y-offset.y); //[min..max] -> [position..position+size]
        const int N = curve.size();
        vec2 R=0; float t=0;
        for(uint i: range(N)) {
            //const Point& prev = curve[(i-1+N)%N];
            const Point& curr = curve[i];
            const Point& next = curve[(i+1)%N];
            float p = (curr.p+next.p)/2.f;
            vec2 F = -p*normal(next-curr); //-curr.p/2*normal(next-prev); //positive pression is towards inside => minus sign; each segment is used twice => /2
            vec2 x = (curr+next)/2.f;
            vec2 r = curr-curve.mean;
            t += cross(r,F);
            R += F;
            line(M*curr, M*next, 2);
            line(M*x, M*(x+F), 2, p>0?red:blue);
        }
        line(M*curve.mean, M*(curve.mean+R), 2);
        float drag = R.x;
        line(M*curve.mean, M*(curve.mean+vec2(drag,0)), 2, red); //drag
        float lift = R.y;
        line(M*curve.mean, M*(curve.mean+vec2(0,lift)), 2, green); //lift
        Text(str("Lift =",lift/100),                32).render(position+int2(0,size.y+0*32),int2(size.x,0));
        Text(str("Drag =",drag/100),          32).render(position+int2(0,size.y+1*32),int2(size.x,0));
        Text(str("Lift/Drag =",lift/drag),32).render(position+int2(0,size.y+2*32),int2(size.x,0));
        Text(str("Torque =",t),32).render(position+int2(0,size.y+3*32),int2(size.x,0));
    }
} application;
#endif

#if 0
#include "process.h"
#include "window.h"
#include "interface.h"
#include "png.h"
struct SnapshotTest : TriggerButton {
    Window window __(this,0,"SnapshotTest"_);
    SnapshotTest(){ writeFile("snapshot.png"_,encodePNG(window.snapshot()),home()); exit();}
} test;
#endif

#if 0
#include "process.h"
#include "widget.h"
#include "display.h"
#include "window.h"
struct VSyncTest : Widget {
    Window window __(this,0,"VSync"_);
    VSyncTest(){ window.localShortcut(Escape).connect(&exit); }
    bool odd; void render(int2 position, int2 size) {fill(position+Rect(size),(odd=!odd)?black:white); window.render();}
} test;
#endif

#if 0
#include "window.h"
#include "interface.h"
#include "ico.h"
struct ImageTest : ImageView {
    Window window __(this,0,"Image"_);

    ImageTest():ImageView(resize(decodeImage(readFile("feedproxy.google.com/favicon.ico"_,cache())),16,16)) {
        assert(image.own);
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
        page.go("http://feedproxy.google.com/~r/Phoronix/~3/LdcmrpZu6FA/vr.php"_);
    }
} test;
#endif

#if 0
#include "window.h"
#include "calendar.h"
#include "png.h"
struct WeekViewTest : Widget {
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
    WeekViewTest(){
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
