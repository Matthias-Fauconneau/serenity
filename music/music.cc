/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "file.h"
#include "time.h"

#include "sequencer.h"
#include "sampler.h"
#include "audio.h"
#include "midi.h"

#include "window.h"
#include "interface.h"
#include "pdf.h"
#include "score.h"
//include "keyboard.h"

#if MIDISCORE
//include "midiscore.h"
#endif

#define RECORD 0
#if RECORD
//include "record.h"
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
    Folder folder{"Sheets"_};
    MidiFile midi;

    Thread thread{-20};
    Sequencer input{thread};

    Sampler sampler;
#define AUDIO 0
#if AUDIO
    AudioOutput audio{{&sampler, &Sampler::read}, 48000, Sampler::periodSize, thread};
#endif

#define UI 1
    ICON(music)
#if UI
    VBox layout;
#if RECORD
    Window window {&layout,int2(1280,720),"Piano"_,musicIcon()};
#elif GL
    Window window {&layout,int2(0,0),"Piano"_,musicIcon(), Window::OpenGL};
#else
    Window window {&layout,int2(0,720),"Piano"_,musicIcon()};
#endif
    List<Text> sheets;

    String name;
    Scroll<PDFScore> pdfScore;
    Score score;
#if MIDISCORE
    Scroll<MidiScore> midiScore;
#endif

    vec2 position=0, target=0, speed=0; //smooth scroll
#endif

#if RECORD
    Record record;
#endif


    Music() {

#if AUDIO
        if(arguments() && endsWith(arguments()[0],".sfz"_)) {
            sampler.open(audio.rate, arguments()[0], Folder("Samples"_));
        } else {
            sampler.open(audio.rate, "Salamander.sfz"_, Folder("Samples"_));
        }
#endif

        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        //input.noteEvent.connect([this](uint,uint){audio.start();}); // Ensures audio output is running (sampler automatically pause)

        midi.noteEvent.connect(&sampler,&Sampler::noteEvent);
        //midi.noteEvent.connect([this](uint,uint){audio.start();}); // Ensures audio output is running (sampler automatically pause)

#if UI
        window.localShortcut(Escape).connect([]{exit();});

        input.noteEvent.connect(&score,&Score::noteEvent);
        midi.noteEvent.connect(&score,&Score::noteEvent);
        pdfScore.contentChanged.connect(&window,&Window::render);
        window.frameSent.connect(this,&Music::smoothScroll);

        pdfScore.onGlyph.connect(&score,&Score::onGlyph);
        pdfScore.onPath.connect(&score,&Score::onPath);
        //pdfScore.positionsChanged.connect(this,&Music::positionsChanged);
        pdfScore.scrollbar = true;

        score.activeNotesChanged.connect(&pdfScore,&PDF::setColors);
        score.nextStaff.connect(this,&Music::nextStaff);

        window.localShortcut(Key(' ')).connect(this,&Music::togglePlay);
        window.localShortcut(Key('o')).connect(this,&Music::showSheetList);
        window.localShortcut(Key('r')).connect([this]{ sampler.enableReverb=!sampler.enableReverb; });
#if AUDIO
        window.localShortcut(Key('1')).connect([this]{
            sampler.open(audio.rate, "Salamander.raw.sfz"_, Folder("Samples"_));
            window.setTitle("Salamander - Raw"_);
        });
        window.localShortcut(Key('2')).connect([this]{
            sampler.open(audio.rate, "Salamander.flat.sfz"_, Folder("Samples"_));
            window.setTitle("Salamander - Flat"_);
        });
        window.localShortcut(Key('3')).connect([this]{
            sampler.open(audio.rate, "Blanchet.raw.sfz"_, Folder("Samples"_));
            window.setTitle("Blanchet"_);
        });
#endif
        //window.localShortcut(Key('y')).connect([this]{ if(layout.tryRemove(&keyboard)==-1) layout<<&keyboard; });
#if MIDISCORE
        midiScore.contentChanged.connect(&window,&Window::render);
        score.activeNotesChanged.connect(&midiScore,&MidiScore::setColors);
#endif
#if ANNOTATION
        score.annotationsChanged.connect(this,&Music::annotationsChanged);
        window.localShortcut(Key('e')).connect(&score,&Score::toggleEdit);
        window.localShortcut(Key('p')).connect(&pdfScore,&PDFScore::toggleEdit);
        window.localShortcut(LeftArrow).connect(&score,&Score::previous);
        window.localShortcut(RightArrow).connect(&score,&Score::next);
        window.localShortcut(Insert).connect(&score,&Score::insert);
        window.localShortcut(Delete).connect(&score,&Score::remove);
#endif
        window.localShortcut(Return).connect(this,&Music::toggleDebug);

        layout << &sheets; sheets.expanding=true;
        array<String> files = folder.list(Files);
        for(String& file : files) {
            if(endsWith(file,".mid"_)||endsWith(file,".pdf"_)) {
                for(const Text& text: sheets) if(text.text==toUTF32(section(file,'.'))) goto break_;
                /*else*/ sheets << String(section(file,'.'));
                break_:;
            }
        }
        sheets.itemPressed.connect(this,&Music::openSheet);
        showSheetList();
#endif
        //for(string arg: arguments()) for(const Text& text: sheets) if(toUTF8(text.text)==arg) { openSheet(arg); break; }
        for(string arg: arguments()) if(!endsWith(arg,".sfz"_)) { openSheet(arg); break; }
#if RECORD
        window.localShortcut(Key('t')).connect(this,&Music::toggleRecord);
        sampler.frameSent.connect(&record,&Record::capture);
        layout<<&keyboard;
#endif
#if UI
        window.show();
#else
        togglePlay();
#endif
#if AUDIO
        input.recordMID("Archive/Stats/"_+str(Date(currentTime()))+".mid"_);
        thread.spawn();
        audio.start();
#endif
        toggleDebug();
    }

    /// Toggles MIDI playing
    bool play=false;
    void togglePlay() {
        play=!play;
        if(play) {
            midi.seek(0);
#if UI
            score.seek(0); score.showActive=true;
#endif
            sampler.timeChanged.connect(&midi,&MidiFile::update); }
        else {
#if UI
            score.showActive=false;
#endif
            sampler.timeChanged.delegates.clear(); }
    }

#if UI
    /// Called by score to scroll PDF as needed when playing
    void nextStaff(float top /*previous bottom, current top*/, float bottom /*current bottom / next top*/, float x) {
        float t = x / (pdfScore.size.y?:window.size.y);
        target = vec2(0, -(( (1-t)*top + t*bottom )-pdfScore.ScrollArea::size.y/2)); // Align center between current top and current bottom
        if(!position) position=target, pdfScore.delta=int2(round(position));
#if MIDISCORE
        midiScore.center(int2(0,bottom));
#endif
        window.focus=0;
        smoothScroll();
    }
    /// Smoothly scrolls towards target
    void smoothScroll() {
        if(window.focus == &pdfScore.area()) return;
        const float k=1./60, b=1./60; //stiffness and damping constants
        speed = b*speed + k*(target-position);
        position = position + speed; //Euler integration
        pdfScore.delta =  min(int2(0,0), max(pdfScore.size-abs(pdfScore.widget().sizeHint()), int2(round(position))));
        if(round(target)!=round(position)) window.render();
#if RECORD
        if(!record) toggleRecord();
#endif
    }

    /// Shows PDF+MIDI sheets selection to open
    void showSheetList() {
        layout.first()=&sheets;
        window.render();
    }
    void toggleDebug() {
        if(pdfScore.annotations) pdfScore.annotations.clear(), window.render(); else pdfScore.setAnnotations(score.debug);
    }
#endif
#if RECORD
    void toggleRecord() {
        if(!name) name=String("Piano"_);
        if(record) { record.stop(); window.setTitle(name); }
        else {
            window.setTitle(String(name+"*"_));
            window.setSize(int2(record.width,record.height));
            record.start(name);
            if(!play) togglePlay();
        }
    }
#endif

#if UI
    void openSheet(uint index) { openSheet(toUTF8(sheets[index].text)); }
#endif
    /// Opens the given PDF+MIDI sheet
    void openSheet(const string& name) {
        if(play) togglePlay();
        midi.clear();
        if(existsFile(String(name+".mid"_),folder)) midi.open(readFile(String(name+".mid"_),folder));
#if UI
        score.clear();
        pdfScore.clear();
        //this->name=String(name);
        window.setTitle(name);
        window.backgroundCenter=window.backgroundColor=1;
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
            window.focus = &pdfScore.area();
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
        window.render();
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
