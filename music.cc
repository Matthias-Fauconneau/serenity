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

extern "C" {
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <x264.h>
#include <libswscale/swscale.h>
}

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
        positions.clear();
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
        if(positions) for(pair<int,vec4> highlight: colors) {
            fill(position+int2(scale*positions[highlight.key])-int2(3)+Rect(6),green);
        }
    }
};

/// SFZ sampler and PDF renderer (tested with Salamander)
struct Music {
    Folder folder __("Sheets"_);
    ICON(music) Window window __(&sheets,int2(0,0),"Piano"_,musicIcon());
    List<Text> sheets;

    string name;
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
                for(const Text& text: sheets) if(text.text==toUTF32(section(file,'.'))) goto break_;
                /*else*/ sheets << string(section(file,'.'));
                break_:;
            }
        }
        sheets.itemPressed.connect(this,&Music::openSheet);

        sampler.open("/Samples/Boesendorfer.sfz"_);
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
        window.localShortcut(Key('r')).connect(this,&Music::toggleRecord);
        window.localShortcut(LeftArrow).connect(&score,&Score::previous);
        window.localShortcut(RightArrow).connect(&score,&Score::next);
        window.localShortcut(Insert).connect(&score,&Score::insert);
        window.localShortcut(Delete).connect(&score,&Score::remove);
        window.localShortcut(Return).connect(this,&Music::toggleAnnotations);
        window.frameReady.connect(this,&Music::recordFrame);

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

    bool record=false;
    void toggleRecord() {
        if(!name) name=string("Piano"_);
        if(record) {
            record = false;
            sampler.stopRecord();
            stopVideoRecord();
            window.setTitle(name);
        } else {
            record = true;
            startVideoRecord(name);
            sampler.startRecord(name);
            window.setTitle(string(name+"*"_));
        }
    }

    x264_t* encoder;
    x264_picture_t pic_in, pic_out;
    uint width=1270, height=720, fps=30;
    SwsContext* swsContext;
    File recordFile=0;

    void startVideoRecord(const ref<byte>& name) {
        window.setSize(int2(1280,720));
        string path = name+".mp4"_;
        if(existsFile(path,home())) { error(path,"already exists"); return; }
        recordFile = File(path,home(),WriteOnly|Create|Truncate);
        x264_param_t param;
        x264_param_default_preset(&param, "veryfast", "stillimage");
        param.i_width = width;
        param.i_height = height;
        log(param.i_timebase_num); param.i_timebase_num = 1;
        param.i_timebase_den = sampler.rate;
        log(param.rc.i_rc_method); param.rc.i_rc_method = X264_RC_CRF;
        log(param.rc.f_rf_constant); param.rc.f_rf_constant = 20;
        log(param.rc.f_rf_constant_max); param.rc.f_rf_constant_max = 29;
        log(param.b_repeat_headers); param.b_repeat_headers = 1;
        log(param.b_annexb); param.b_annexb = 1;
        x264_param_apply_profile(&param, "high");
        encoder = x264_encoder_open(&param);
        x264_encoder_parameters( encoder, &param);

        x264_picture_alloc(&pic_in, X264_CSP_I420, width, height);

        x264_nal_t* nals; int nalCount;
        int size = x264_encoder_headers(encoder, &nals, &nalCount);
        if(size < 0) error("x264_encoder_headers");
        recordFile.write((byte*)nals->p_payload, size);

        swsContext = sws_getContext(width, height, PIX_FMT_BGR0, width, height, PIX_FMT_YUV420P, SWS_FAST_BILINEAR, 0, 0, 0);
    }

    void recordFrame() {
        if(!record) return;
        const Image& image = framebuffer;
        if(width!=image.width || height!=image.height) { error("Window size changed while recording"); return; }

        int stride = image.stride*4;
        sws_scale(swsContext, &(uint8*&)image.data, &stride, 0, width, pic_in.img.plane, pic_in.img.i_stride);
        x264_nal_t* nals; int nalCount;
        pic_in.i_pts = sampler.time;
        int size = x264_encoder_encode(encoder, &nals, &nalCount, &pic_in, &pic_out);
        if(size < 0) error("x264_encoder_encode");
        recordFile.write((byte*)nals->p_payload, size);
    }

    void stopVideoRecord() {
        x264_nal_t* nals; int nalCount;
        for(;;) {
            int frame_size = x264_encoder_encode(encoder, &nals, &nalCount, 0, &pic_out);
            if(frame_size < 0) break;
            for(const x264_nal_t& nal: ref<x264_nal_t>(nals,nalCount)) {
                recordFile.write(ref<byte>((byte*)nal.p_payload, nal.i_payload));
            }
        }
        x264_encoder_close(encoder);
        recordFile = 0;
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
    void openSheet(uint index) { openSheet(toUTF8(sheets[index].text)); }
    void openSheet(const ref<byte>& name) {
        if(play) togglePlay();
        score.clear(); midi.clear(); pdfScore.clear();
        this->name=string(name);
        window.setTitle(name);
        if(existsFile(string(name+".mid"_),folder)) midi.open(readFile(string(name+".mid"_),folder));
        window.backgroundCenter=window.backgroundColor=1;
        if(existsFile(string(name+".pdf"_),folder)) {
            pdfScore.open(readFile(string(name+".pdf"_),folder));
            if(existsFile(string(name+".pos"_),folder)) {
                score.clear();
                pdfScore.loadPositions(readFile(string(name+".pos"_),folder));
            }
            score.parse();
            if(midi.notes) score.synchronize(copy(midi.notes));
            else if(existsFile(string(name+".not"_),folder)) score.annotate(parseAnnotations(readFile(string(name+".not"_),folder)));
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
