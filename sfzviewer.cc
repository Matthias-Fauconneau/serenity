#include "process.h"
#include "window.h"
#include "display.h"
#include "sequencer.h"
#include "sampler.h"
#include "asound.h"
#include "text.h"
#include "layout.h"
#include "keyboard.h"
#include "spectrogram.h"

/// Displays information on the played samples
struct SFZViewer : Widget {
    Thread thread;
    Sequencer input {thread};
    Sampler sampler;
    AudioOutput audio {{&sampler, &Sampler::read}, 44100, 512, thread};

    Text text;
    Keyboard keyboard;
    VBox layout;
    Window window {&layout,int2(0,spectrogram.sizeHint().y+16+16+120+120),"SFZ Viewer"_};

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

        audio.start();
        thread.spawn();
    }
    void viewFrame(float* data, uint size) {
        spectrogram.write(data,size);
        window.render();
    }
    void render(int2 position, int2 size) {
        if(lastVelocity) { // Displays volume of all notes for a given velocity layer
            int margin = (size.x-size.x/88*88)/2;
            int dx = size.x/88;
            int y0 = position.y;
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
