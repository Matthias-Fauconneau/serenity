/// \file music.cc Keyboard (piano) practice application
#include "thread.h"
#include "file.h"

#include "sequencer.h"
#include "sampler.h"
#include "audio.h"
#include "midi.h"

#include "window.h"
#include "interface.h"
#include "pdf.h"
#include "score.h"
#include "midiscore.h"
#include "keyboard.h"

#define RECORD 1
#if RECORD
#include "record.h"
#endif

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
            if(pos.x>0 && pos.y<-y2 && pos.x*scale<1280) {
                positions << pos;
            }
        }
        {int i=0; for(vec2 position: positions) {
            onGlyph(i++,position*scale,32,"Manual"_,0,0);
        }}
        {int i=0; for(vec2 position: positions) {
            onGlyph(i++,position*scale,32,"Manual"_,0,0);
        }}
    }
    bool editMode=false;
    void toggleEdit() { editMode=!editMode; }
    bool mouseEvent(int2 cursor, int2, Event event, Button button) override {
        if(!editMode || event!=Press) return false;
        vec2 position = vec2(cursor)/scale;
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
        if(positions) for(const_pair<int,vec4> highlight: colors) {
            fill(position+int2(scale*positions[highlight.key])-int2(3)+Rect(6),green);
        }
    }
};

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music {
    Folder root = "/"_;
    Folder folder{"Sheets"_,root};
    ICON(music)
    VBox layout;
#if RECORD
    Window window {&layout,int2(1280,720),"Piano"_,musicIcon()};
#else
    Window window {&layout,int2(1280,720/*0,0*/),"Piano"_,musicIcon()};
#endif
    List<Text> sheets;

    String name;
    MidiFile midi;
    Scroll<PDFScore> pdfScore;
    Scroll<MidiScore> midiScore;
    Score score;
    Keyboard keyboard;

    Thread thread{-20};
    Sequencer input{thread};

    Sampler sampler;
    AudioOutput audio{{&sampler, &Sampler::read}, 48000, Sampler::periodSize, thread};
#if RECORD
    Record record;
#endif
    vec2 position=0, target=0, speed=0; //smooth scroll

    Music() {
        layout << &sheets; sheets.expanding=true;

        if(arguments() && endsWith(arguments()[0],".sfz"_))
            sampler.open(audio.rate, arguments()[0], root);
        else {
            //sampler.open(audio.rate, "Boesendorfer.sfz"_,Folder("Samples"_,root)); //FIXME
            sampler.open(audio.rate, "Salamander.sfz"_,Folder("Samples"_,root));
        }

        array<String> files = folder.list(Files);
        for(String& file : files) {
            if(endsWith(file,".mid"_)||endsWith(file,".pdf"_)) {
                for(const Text& text: sheets) if(text.text==toUTF32(section(file,'.'))) goto break_;
                /*else*/ sheets << String(section(file,'.'));
                break_:;
            }
        }
        sheets.itemPressed.connect(this,&Music::openSheet);

        midi.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        keyboard.noteEvent.connect(&sampler,&Sampler::noteEvent);

        input.noteEvent.connect(&score,&Score::noteEvent);
        input.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);

        midi.noteEvent.connect(&score,&Score::noteEvent);
        midi.noteEvent.connect(&keyboard,&Keyboard::midiNoteEvent);

        keyboard.noteEvent.connect(&score,&Score::noteEvent);
        keyboard.noteEvent.connect(&keyboard,&Keyboard::inputNoteEvent);

        keyboard.contentChanged.connect(&window,&Window::render);
        midiScore.contentChanged.connect(&window,&Window::render);
        pdfScore.contentChanged.connect(&window,&Window::render);
        window.frameReady.connect(this,&Music::smoothScroll);

        pdfScore.onGlyph.connect(&score,&Score::onGlyph);
        pdfScore.onPath.connect(&score,&Score::onPath);
        pdfScore.positionsChanged.connect(this,&Music::positionsChanged);

        score.activeNotesChanged.connect(&pdfScore,&PDF::setColors);
        score.activeNotesChanged.connect(&midiScore,&MidiScore::setColors);
        score.nextStaff.connect(this,&Music::nextStaff);
        score.annotationsChanged.connect(this,&Music::annotationsChanged);

        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(Key(' ')).connect(this,&Music::togglePlay);
        window.localShortcut(Key('o')).connect(this,&Music::showSheetList);
        window.localShortcut(Key('e')).connect(&score,&Score::toggleEdit);
        window.localShortcut(Key('p')).connect(&pdfScore,&PDFScore::toggleEdit);
        window.localShortcut(Key('r')).connect([this]{ sampler.enableReverb=!sampler.enableReverb; });
#if RECORD
        window.localShortcut(Key('t')).connect(this,&Music::toggleRecord);
        sampler.frameReady.connect(&record,&Record::capture);
#endif
        window.localShortcut(Key('y')).connect([this]{ if(layout.tryRemove(&keyboard)==-1) layout<<&keyboard; });
        window.localShortcut(LeftArrow).connect(&score,&Score::previous);
        window.localShortcut(RightArrow).connect(&score,&Score::next);
        window.localShortcut(Insert).connect(&score,&Score::insert);
        window.localShortcut(Delete).connect(&score,&Score::remove);
        window.localShortcut(Return).connect(this,&Music::toggleAnnotations);

        showSheetList();
        if(arguments()) for(const Text& text: sheets) if(text.text==toUTF32(arguments()[0])) { openSheet(arguments()[0]); break; }
        audio.start();
        thread.spawn();
        window.show();
    }

    /// Called by score to scroll PDF as needed when playing
    void nextStaff(float previous /*previous top*/, float top /*previous bottom, current top*/, float bottom /*current bottom / next top*/, float next /* next bottom*/, float x) {
        if(pdfScore.normalizedScale && (pdfScore.x2-pdfScore.x1)) {
            if(!pdfScore.size) pdfScore.size=window.size; //FIXME: called before first render, no layout
            float scale = pdfScore.size.x/(pdfScore.x2-pdfScore.x1)/pdfScore.normalizedScale;
            // Always set current staff as second staff from bottom edge (allows to repeat page, track scrolling, see keyboard)
            float t = (x/pdfScore.normalizedScale)/(pdfScore.x2-pdfScore.x1); assert(t>=0 && t<=1);
            log(pdfScore.normalizedScale,pdfScore.x1,pdfScore.x2,x/pdfScore.normalizedScale,t);
            //target = vec2(0, -(scale*( (1-t)*bottom + t*next )-pdfScore.ScrollArea::size.y)); // Align bottom edge between current bottom and next bottom
            target = vec2(0, -scale*( (1-t)*previous + t*top )); // Align top edge between previos top and current top
            if(!position) position=target, pdfScore.delta=int2(round(position));
        }
        midiScore.center(int2(0,bottom));
        smoothScroll();
    }
    /// Smoothly scrolls towards target
    void smoothScroll() {
        if(pdfScore.annotations) return;
        const float k=1./(8*60), b=1./2; //stiffness and damping constants
        speed = b*speed + k*(target-position);
        position = position + speed; //Euler integration
        pdfScore.delta=int2(round(position));
        if(round(target)!=round(position)) window.render();
    }
    /// Toggles MIDI playing
    bool play=false;
    void togglePlay() {
        play=!play;
        if(play) { midi.seek(0); score.seek(0); score.showActive=true; sampler.timeChanged.connect(&midi,&MidiFile::update); }
        else { score.showActive=false; sampler.timeChanged.delegates.clear(); }
    }

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

    /// Shows PDF+MIDI sheets selection to open
    void showSheetList() {
        layout.first()=&sheets;
        window.render();
    }

    void toggleAnnotations() {
        if(pdfScore.annotations) pdfScore.annotations.clear(), window.render(); else pdfScore.setAnnotations(score.debug);
    }

    /// Opens the given PDF+MIDI sheet
    void openSheet(uint index) { openSheet(toUTF8(sheets[index].text)); }
    void openSheet(const string& name) {
        if(play) togglePlay();
        score.clear(); midi.clear(); pdfScore.clear();
        this->name=String(name);
        window.setTitle(name);
        if(existsFile(String(name+".mid"_),folder)) midi.open(readFile(String(name+".mid"_),folder));
        window.backgroundCenter=window.backgroundColor=1;
        if(existsFile(String(name+".pdf"_),folder)) {
            pdfScore.open(readFile(String(name+".pdf"_),folder));
            if(existsFile(String(name+".pos"_),folder)) {
                score.clear();
                pdfScore.loadPositions(readFile(String(name+".pos"_),folder));
            }
            score.parse();
            if(midi.notes) score.synchronize(copy(midi.notes));
            else if(existsFile(String(name+".not"_),folder)) score.annotate(parseAnnotations(readFile(String(name+".not"_),folder)));
            layout.first()= &pdfScore.area();
            pdfScore.delta = 0; position=0,speed=0,target=0;
        } else if(existsFile(String(name+".mid"_),folder)) {
            midiScore.parse(move(midi.notes),midi.key,midi.tempo,midi.timeSignature,midi.ticksPerBeat);
            layout.first()= &midiScore.area();
            midiScore.delta=0; position=0,speed=0,target=0;
            midiScore.widget().render(int2(0,0),int2(1280,0)); //compute note positions for score scrolling
            score.chords = copy(midiScore.notes);
            score.staffs = move(midiScore.staffs);
            score.positions = move(midiScore.positions);
        }
        score.seek(0);
        window.render();
    }

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

} application;
