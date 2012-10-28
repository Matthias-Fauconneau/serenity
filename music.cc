/// \file music.cc Keyboard (piano) practice application
#include "process.h"
#include "file.h"

#include "sequencer.h"
#include "sampler.h"
#include "asound.h"
#include "midi.h"

#include "window.h"
#include "interface.h"
#include "pdf.h"
#include "score.h"
#include "midiscore.h"

// Simple human readable/editable format for score synchronization annotations
map<uint, Chord> parseAnnotations(string&& annotations) {
    map<uint, Chord> chords;
    uint t=0,i=0;
    for(TextData s(annotations);s;) {
        while(!s.match('\n')) {
            int key = s.integer(); s.match(' ');
            chords[t] << MidiNote __(key,t,1);
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
        for(vec2 pos: cast<vec2>(data)) {
            if(pos.x>0 && pos.y<-y2 && pos.x*scale<1280) {
                positions << pos;
            }
        }
        int i=0; for(vec2 position: positions) {
            onGlyph(i++,position*scale,32,"Manual"_,0);
        }
    }
    bool editMode=false;
    void toggleEdit() { editMode=!editMode; }
    bool mouseEvent(int2 cursor, int2, Event event, Button button) override {
        if(!editMode || event!=Press) return false;
        vec2 position = vec2(cursor)/scale;
        int best=-1; float D=60;
        for(int i: range(positions.size())) {
            vec2 delta = positions[i]-position;
            float d = length(delta);
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
        if(positions) for(pair<int,byte4> highlight: colors) {
            fill(position+int2(scale*positions[highlight.key])-int2(3)+Rect(6),green);
        }
    }
};

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music {
    Folder folder __("Sheets"_);
    ICON(music) Window window __(&sheets,int2(0,0),"Piano"_,musicIcon());
    List<Text> sheets;

    MidiFile midi;
    Scroll<PDFScore> pdfScore;
    Scroll<MidiScore> midiScore;
    Score score;

    Sampler sampler;
    Thread thread __(-20);
    AudioOutput audio __({&sampler, &Sampler::read},thread,true);
    Sequencer input __(thread);

    Music() {
        array<string> files = folder.list(Files);
        for(string& file : files) {
            if(endsWith(file,".mid"_)||endsWith(file,".pdf"_)) {
                for(const Text& text: sheets) if(text.text==section(file,'.')) goto break_;
                /*else*/ sheets << string(section(file,'.'));
                break_:;
            }
        }
        sheets.itemPressed.connect(this,&Music::openSheet);

        sampler.open("/Samples/Salamander.sfz"_);
        input.noteEvent.connect(&sampler,&Sampler::noteEvent);
        midi.noteEvent.connect(&sampler,&Sampler::noteEvent);
        input.noteEvent.connect(&score,&Score::noteEvent);

        pdfScore.contentChanged.connect(&window,&Window::render);
        pdfScore.onGlyph.connect(&score,&Score::onGlyph);
        pdfScore.onPath.connect(&score,&Score::onPath);
        pdfScore.positionsChanged.connect(this,&Music::positionsChanged);

        midiScore.contentChanged.connect(&window,&Window::render);

        score.activeNotesChanged.connect(&pdfScore,&PDF::setColors);
        score.activeNotesChanged.connect(&midiScore,&MidiScore::setColors);
        score.nextStaff.connect(this,&Music::nextStaff);
        score.annotationsChanged.connect(this,&Music::annotationsChanged);
        midi.noteEvent.connect(&score,&Score::noteEvent);

        window.localShortcut(Escape).connect(&exit);
        window.localShortcut(Key(' ')).connect(this,&Music::togglePlay);
        window.localShortcut(Key('o')).connect(this,&Music::showSheetList);
        window.localShortcut(Key('e')).connect(&score,&Score::toggleEdit);
        window.localShortcut(Key('p')).connect(&pdfScore,&PDFScore::toggleEdit);
        window.localShortcut(LeftArrow).connect(&score,&Score::previous);
        window.localShortcut(RightArrow).connect(&score,&Score::next);
        window.localShortcut(Insert).connect(&score,&Score::insert);
        window.localShortcut(Delete).connect(&score,&Score::remove);
        window.localShortcut(Return).connect(this,&Music::toggleAnnotations);

        showSheetList();
        audio.start();
        thread.spawn();
    }

    /// Called by score to scroll PDF as needed when playing
    void nextStaff(float unused previous, float current, float unused next) {
        if(pdfScore.normalizedScale && (pdfScore.x2-pdfScore.x1)) {
            float scale = pdfScore.size.x/(pdfScore.x2-pdfScore.x1)/pdfScore.normalizedScale;
            //pdfScore.delta.y = -min(scale*current, max(scale*previous, scale*next-pdfScore.ScrollArea::size.y));
            pdfScore.center(int2(0,scale*current));
        }
        //midiScore.delta.y = -min(current, max(previous, next-midiScore.ScrollArea::size.y));
        midiScore.center(int2(0,current));
    }

    /// Toggles MIDI playing
    bool play=false;
    void togglePlay() {
        play=!play;
        if(play) { midi.seek(0); score.seek(0); sampler.timeChanged.connect(&midi,&MidiFile::update); } else sampler.timeChanged.delegates.clear();
    }

    /// Shows PDF+MIDI sheets selection to open
    void showSheetList() {
        window.widget=&sheets;
        window.render();
    }

    void toggleAnnotations() {
        if(pdfScore.annotations) pdfScore.annotations.clear(), window.render(); else pdfScore.setAnnotations(score.debug);
    }

    /// Opens the given PDF+MIDI sheet
    void openSheet(uint index) { openSheet(sheets[index].text); }
    string name;
    void openSheet(const ref<byte>& name) {
        if(play) togglePlay();
        score.clear(); midi.clear(); pdfScore.clear();
        this->name=string(name);
        window.setTitle(name);
        if(existsFile(string(name+".mid"_),folder)) midi.open(readFile(string(name+".mid"_),folder));
        window.backgroundCenter=window.backgroundColor=0xFF;
        if(existsFile(string(name+".pdf"_),folder)) {
            pdfScore.open(readFile(string(name+".pdf"_),folder));
            if(existsFile(string(name+".pos"_),folder)) {
                score.clear();
                pdfScore.loadPositions(readFile(string(name+".pos"_),folder));
            }
            score.parse();
            if(midi.notes) score.synchronize(copy(midi.notes));
            else if(existsFile(string(name+".not"_),folder)) score.annotate(parseAnnotations(readFile(string(name+".not"_),folder)));
            //pdfScore.setAnnotations(score.debug);
            window.widget = &pdfScore.area();
            pdfScore.delta = 0;
        } else {
            midiScore.parse(move(midi.notes),midi.key,midi.tempo,midi.timeSignature,midi.ticksPerBeat);
            window.widget = &midiScore.area();
            midiScore.delta=0;
            midiScore.widget().render(int2(0,0),int2(1280,0)); //compute note positions for score scrolling
            score.chords = copy(midiScore.notes);
            score.staffs = move(midiScore.staffs);
            score.positions = move(midiScore.positions);
        }
        score.seek(0);
        window.setSize(int2(0,0));
        window.render();
    }

    void annotationsChanged(const map<uint, Chord>& chords) {
        pdfScore.setAnnotations(score.debug);
        string annotations;
        for(const_pair<uint, Chord> chord: chords) {
            for(MidiNote note: chord.value) annotations <<dec(note.key)<<' ';
            annotations <<'\n';
        }
        writeFile(string(name+".not"_),annotations,folder);
    }

    void positionsChanged(const ref<vec2>& positions) {
        writeFile(string(name+".pos"_),cast<byte>(positions),folder);
        score.clear();
        pdfScore.loadPositions(readFile(string(name+".pos"_),folder));
        score.parse();
        if(midi.notes) score.synchronize(copy(midi.notes));
        else if(existsFile(string(name+".not"_),folder)) score.annotate(parseAnnotations(readFile(string(name+".not"_),folder)));
        pdfScore.setAnnotations(score.debug);
    }
} application;
