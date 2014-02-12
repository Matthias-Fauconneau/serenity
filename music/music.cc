/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "file.h"
#include "sampler.h"
#include "midi.h"

#define SCORE 1
#define KEYBOARD 1
#define ENCODE 1
#if !ENCODE
#define WINDOW 1
#define SAMPLE 1
#define AUDIO 0
#define THREAD 1
#else
#define SAMPLE 0
#endif

#if AUDIO
#include "audio.h"
#endif
#if INPUT
#include "sequencer.h"
#endif
#if SCORE
#if WINDOW
#include "window.h"
#endif
#include "interface.h"
#include "pdf.h"
#include "score.h"
#if KEYBOARD
#include "keyboard.h"
#endif
#if MIDISCORE
#include "midiscore.h"
#endif
#endif
#if ENCODE
#include "encoder.h"
#endif

#if ANNOTATION
// Simple human readable/editable format for score synchronization annotations
map<uint, Chord> parseAnnotations(String&& annotations) {
    map<uint, Chord> chords;
    uint t=0,i=0;
    for(TextData s(annotations);s;) {
        while(!s.match('\n')) {
            uint key = s.integer(); s.match(' ');
            chords[t] << MidiNote{key,t,1};
            i++;
        }
        t++;
    }
    return chords;
}

struct PDFScore : PDF {
    array<vec2> positions;
    signal<const ref<vec2>&> positionsChanged;
    void clear() { PDF::clear(); positions.clear(); }
    void loadPositions(const ref<byte>& data) {
        positions.clear();
        for(vec2 pos: cast<vec2>(data)) {
            if(pos.x>0 && pos.y<-y2 && pos.x<1) {
                positions << pos;
            }
        }
        {int i=0; for(vec2 position: positions) {
            onGlyph(i++,position,32,"Manual"_,0,0);
        }}
        {int i=0; for(vec2 position: positions) {
            onGlyph(i++,position,32,"Manual"_,0,0);
        }}
    }
    bool autoScroll=false;
    bool editMode=false;
    void toggleEdit() { editMode=!editMode; }
    bool mouseEvent(int2 cursor, int2, Event event, Button button) override {
        autoScroll = false;
        if(!editMode || event!=Press) return false;
        vec2 position = vec2(cursor);
        int best=-1; float D=60;
        for(int i: range(positions.size)) {
            vec2 delta = positions[i]-position;
            float d = norm(delta);
            if(d<D) { D=d; if(abs(delta.x)<16) best=i; else best=-1; }
        }
        if(button == LeftButton) {
            if(best>=0) positions.insertAt(positions[best].y<position.y?best:best+1,position); else positions << position;
        } else if(button == RightButton) {
            if(best>=0) positions.removeAt(best);
        } else return false; //TODO: move, insert
        positionsChanged(positions);
        return true;
    }
    void render(int2 position, int2 size) override {
        PDF::render(position,size);
        if(annotations) for(vec2 pos: positions) fill(position+int2(scale*pos)-int2(2)+Rect(4),red);
        if(positions) for(const_pair<int,vec4> highlight: (const map<int,vec4>&)colors) {
            fill(position+int2(scale*positions[highlight.key])-int2(3)+Rect(6),green);
        }
    }
};
#else
typedef PDF PDFScore;
#endif

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music {
    Folder folder{"Scores"_};
    MidiFile midi;

    const uint rate = 48000;
#if THREAD
    Thread thread{-20};
    Sampler sampler {true}; // Audio mixing (consumer thread) preempts decoder running in advance (in producer thread (main thread))
#if AUDIO
    AudioOutput audio{{&sampler, &Sampler::read}, rate, Sampler::periodSize, thread};
#endif
#if INPUT
    Sequencer input{thread};
#endif
#else
#if SAMPLE
    Sampler sampler {false}; // Decodes as needed in same consumer thread
#endif
#if AUDIO
    AudioOutput audio{{&sampler, &Sampler::read}, rate, Sampler::periodSize};
#endif
#if INPUT
    Sequencer input;
#endif
#endif

#if SCORE
    VBox layout;
#if WINDOW
    ICON(music)
    Window window {&layout,int2(0,720),"Piano"_,musicIcon()};
    List<Text> scores;
#endif

    String name;
    // Smooth scroll (FIXME: inherit ScrollArea)
     vec2 position=0, target=0, speed=0;
     signal<> contentChanged;
    Scroll<PDFScore> pdfScore;
    Score score;
#if MIDISCORE
    Scroll<MidiScore> midiScore;
#endif
#if KEYBOARD
    Keyboard keyboard;
#endif
#endif

    Music() {
#if SAMPLE
        // Sampler
        if(arguments() && endsWith(arguments()[0],".sfz"_)) {
            sampler.open(rate, arguments()[0], Folder("Samples"_));
        } else {
            sampler.open(rate, "Salamander.sfz"_, Folder("Samples"_));
        }
        midi.noteEvent.connect(&sampler,&Sampler::noteEvent);
#endif
#if INPUT
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
#endif

#if SCORE
        layout << &pdfScore.area();

        midi.noteEvent.connect(&score,&Score::noteEvent);
        pdfScore.onGlyph.connect(&score,&Score::onGlyph);
        pdfScore.onPath.connect(&score,&Score::onPath);

        score.activeNotesChanged.connect(&pdfScore,&PDF::setColors);
        score.nextStaff.connect(this,&Music::nextStaff);

#if INPUT
        input.noteEvent.connect(&score,&Score::noteEvent);
#endif
#if WINDOW
        pdfScore.scrollbar = true;
        pdfScore.contentChanged.connect(&window,&Window::render);
        window.frameSent.connect(this,&Music::smoothScroll);


        window.actions[Escape] = []{exit();};
        window.actions[Key(' ')] = {this,&Music::togglePlay);
        window.actions[Key('o')] = {this,&Music::showScoreList);
        window.actions[Key('r')] = {[this]{ sampler.enableReverb=!sampler.enableReverb; });
#if AUDIO
        window.actions[Key('1')] = [this]{
            sampler.open(rate, "Salamander.raw.sfz"_, Folder("Samples"_));
            window.setTitle("Salamander - Raw"_);
        });
        window.actions[Key('2')] = [this]{
            sampler.open(rate, "Salamander.flat.sfz"_, Folder("Samples"_));
            window.setTitle("Salamander - Flat"_);
        });
        window.actions[Key('3')] = [this]{
            sampler.open(rate, "Blanchet.raw.sfz"_, Folder("Samples"_));
            window.setTitle("Blanchet"_);
        });
#endif
#if MIDISCORE
        midiScore.contentChanged.connect(&window,&Window::render);
        score.activeNotesChanged.connect(&midiScore,&MidiScore::setColors);
#endif
#if ANNOTATION
        //pdfScore.positionsChanged.connect(this,&Music::positionsChanged);
        score.annotationsChanged.connect(this,&Music::annotationsChanged);
        window.actions[Key('e')] = {&score,&Score::toggleEdit);
        window.actions[Key('p')] = {&pdfScore,&PDFScore::toggleEdit);
        window.actions[LeftArrow] = {&score,&Score::previous);
        window.actions[RightArrow] = {&score,&Score::next);
        window.actions[Insert] = {&score,&Score::insert);
        window.actions[Delete] = {&score,&Score::remove);
#endif
        window.actions[Return] = {this,&Music::toggleDebug);
        array<String> files = folder.list(Files);
        for(String& file : files) {
            if(endsWith(file,".mid"_)||endsWith(file,".pdf"_)) {
                for(const Text& text: scores) if(text.text==toUTF32(section(file,'.'))) goto break_;
                /*else*/ scores << String(section(file,'.'));
                break_:;
            }
        }
        scores.itemPressed.connect(this,&Music::openScore);
        showScoreList();
#endif // WINDOW
#endif // SCORE
#if KEYBOARD
        layout<<&keyboard;
        midi.noteEvent.connect(&keyboard,&Keyboard::midiNoteEvent);
#if SAMPLE
        keyboard.noteEvent.connect(&sampler,&Sampler::noteEvent);
#endif
        keyboard.noteEvent.connect(&score,&Score::noteEvent);
        keyboard.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
#if INPUT
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);
#endif
#if WINDOW
        keyboard.contentChanged.connect(&window,&Window::render);
#endif
#endif
        for(string arg: arguments()) if(!endsWith(arg,".sfz"_)) { openScore(arg); break; }

#if !ENCODE
#if AUDIO
        //midi.noteEvent.connect([this](uint,uint){audio.start();}); // Ensures audio output is running (sampler automatically pause)
#if INPUT
        //input.noteEvent.connect([this](uint,uint){audio.start();}); // Ensures audio output is running (sampler automatically pause)
#endif
        audio.start();
#else
        toggleDebug();
#endif
#if WINDOW
        window.show();
#endif
#if THREAD
        thread.spawn();
#endif
#else
        bool contentChanged = true;
        pdfScore.contentChanged.connect([&contentChanged]{ contentChanged=true; });
        keyboard.contentChanged.connect([&contentChanged]{ contentChanged=true; });
        this->contentChanged.connect([&contentChanged]{ contentChanged=true; });
#if SAMPLE
        midi.endOfFile.connect([this,&endOfFile]{ sampler.silence.connect([&endOfFile]{ endOfFile=true; }); });
#endif
        assert(!play);
        togglePlay();
        assert(name);
        Encoder encoder (name /*, {&sampler, &Sampler::read}*/);
        int lastReport=0;
        for(Image image; midi.time <= midi.duration;) { // Renders score as quickly as possible
            midi.read(encoder.videoTime*encoder.rate/encoder.fps);
            smoothScroll();
            if(contentChanged) {
                image = renderToImage(layout, encoder.size()); contentChanged=false;
                int percent = round(100.*midi.time/midi.duration);
                if(percent!=lastReport) { log(percent); lastReport=percent; }
            }
            encoder.writeVideoFrame(image);
        }
#endif
    }

    /// Toggles MIDI playing
    bool play=false;
    void togglePlay() {
        play=!play;
        if(play) {
            midi.seek(0);
#if SCORE
            score.seek(0);
            score.showActive=true;
#endif
#if SAMPLE
            sampler.time = midi.time;
            sampler.timeChanged.connect(&midi,&MidiFile::read);
#endif
#if WINDOW
        window.focus=0;
#endif
        } else {
#if SCORE
            score.showActive=false;
#endif
#if SAMPLE
            sampler.timeChanged.delegates.clear();
#endif
        }
    }

#if SCORE
    /// Called by score to scroll PDF as needed when playing
    void nextStaff(float top /*previous bottom, current top*/, float bottom /*current bottom, next top*/, float x) {
        if(top==bottom) return; // last staff
        assert(x>=0 && x<=1);
        target = vec2(0, -(( (1-x)*top + x*bottom )*pdfScore.lastSize.x-pdfScore.size.y/2)); // Align center between current top and current bottom
        if(!position) position=target, pdfScore.delta=int2(round(position));
#if MIDISCORE
        midiScore.center(int2(0,bottom));
#endif
    }
    /// Smoothly scrolls towards target
    void smoothScroll() { //FIXME: inherit ScrollArea
#if WINDOW
        if(window.focus == &pdfScore.area()) return;
#endif
        const float k=1./60, b=1./60; // Stiffness and damping constants
        speed = b*speed + k*(target-position);
        position = position + speed; //Euler integration
        int2 delta = min(int2(0,0), max(pdfScore.size-abs(pdfScore.widget().sizeHint()), int2(round(position))));
        if(delta!=pdfScore.delta) { pdfScore.delta = delta; contentChanged(); }
#if WINDOW
        if(round(target)!=round(position)) window.render();
#endif
    }

#if WINDOW
    /// Shows PDF+MIDI scores selection to open
    void showScoreList() {
        layout.first()= &scores;
        scores.expanding=true;
        window.render();
    }
    void toggleDebug() {
        if(pdfScore.annotations) pdfScore.annotations.clear(), window.render(); else pdfScore.setAnnotations(score.debug);
    }
#endif
#endif

#if WINDOW
    void openScore(uint index) { openScore(toUTF8(scores[index].text)); }
#endif
    /// Opens the given PDF+MIDI score
    void openScore(const string& name) {
        if(play) togglePlay();
        midi.clear();
        if(existsFile(String(name+".mid"_),folder)) midi.open(readFile(String(name+".mid"_),folder));
#if SCORE
        score.clear();
        pdfScore.clear();
        this->name=String(name);
#if WINDOW
        window.setTitle(name);
        window.backgroundCenter=window.backgroundColor=1;
#endif
        if(existsFile(String(name+".pdf"_),folder)) {
            pdfScore.open(readFile(String(name+".pdf"_),folder));
#if ANNOTATION
            if(existsFile(String(name+".pos"_),folder)) {
                score.clear();
                pdfScore.loadPositions(readFile(String(name+".pos"_),folder));
            }
#endif
            score.parse();
            if(midi.notes) score.synchronize(copy(midi.notes));
#if ANNOTATION
            else if(existsFile(String(name+".not"_),folder)) score.annotate(parseAnnotations(readFile(String(name+".not"_),folder)));
#endif
            layout.first()= &pdfScore.area();
            pdfScore.delta = 0; position=0,speed=0,target=0;
#if WINDOW
            window.focus = &pdfScore.area();
#endif
        }
#if MIDISCORE
        else if(existsFile(String(name+".mid"_),folder)) {
            midiScore.parse(move(midi.notes),midi.key,midi.tempo,midi.timeSignature,midi.ticksPerBeat);
            layout.first()= &midiScore.area();
            midiScore.delta=0; position=0,speed=0,target=0;
            midiScore.widget().render(int2(0,0),int2(1280,0)); //compute note positions for score scrolling
            score.chords = copy(midiScore.notes);
            score.staffs = move(midiScore.staffs);
            score.positions = move(midiScore.positions);
        }
#endif
        score.seek(0);
#if WINDOW
        window.render();
#endif
#endif
    }

#if ANNOTATION
    void annotationsChanged(const map<uint, Chord>& chords) {
        pdfScore.setAnnotations(score.debug);
        String annotations;
        for(const_pair<uint, Chord> chord: chords) {
            for(MidiNote note: chord.value) annotations <<dec(note.key)<<' ';
            annotations <<'\n';
        }
        writeFile(String(name+".not"_),annotations,folder);
    }

    void positionsChanged(const ref<vec2>& positions) {
        writeFile(String(name+".pos"_),cast<byte>(positions),folder);
        score.clear();
        pdfScore.loadPositions(readFile(String(name+".pos"_),folder));
        score.parse();
        if(midi.notes) score.synchronize(copy(midi.notes));
        else if(existsFile(String(name+".not"_),folder)) score.annotate(parseAnnotations(readFile(String(name+".not"_),folder)));
        pdfScore.setAnnotations(score.debug);
    }
#endif
} application;
