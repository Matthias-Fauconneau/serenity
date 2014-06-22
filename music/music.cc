#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "encoder.h"
#include "window.h"
#include "audio.h"

struct Music : Widget {
    string name = arguments()[0];
    MusicXML xml = readFile(name+".xml"_);
    const uint oversample = 4;
    Sheet sheet {xml.signs, xml.divisions, uint(720/2*4)};
    AudioFile mp3 = name+".mp3"_; // 48KHz AAC would be better
    MidiFile midi = readFile(name+".mid"_);
    buffer<uint> noteToBlit = sheet.synchronize(apply(filter(midi.notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;}));

    // Highlighting
    map<uint,uint> active; // Maps active keys to notes (indices)
    uint midiIndex = 0, noteIndex = 0;
    float target=0, speed=0, position=0; // X target/speed/position of sheet view

    // Encoding
    bool contentChanged = true;
    bool encode = false;
    Encoder encoder {name /*, {&audioFile, &AudioFile::read}*/}; //TODO: remux without reencoding

    // Preview
    bool preview = true;
    Timer timer;
    Window window {this, int2(1280,720/2), "MusicXML"_};
    AudioOutput audio {{this,&Music::read}};
    uint64 audioTime = 0, videoTime = 0;
    Image image {uint(sheet.measures.last()), uint(window.size.y*oversample)};
    uint lastPosition = 0;

    uint noteIndexToMidiIndex(uint seekNoteIndex) {
        uint midiIndex=0;
        for(uint noteIndex=0; noteIndex<seekNoteIndex; midiIndex++) if(midi.notes[midiIndex].velocity) noteIndex++;
        return midiIndex;
    }

    Music() {
        renderBackground(image, White);
        sheet.render(image, Rect(image.size()));
        if(sheet.synchronizationFailed) { window.background=White; return; }
        window.background = NoBackground; // Only clear on changes
        window.widget = this;
        //seek( midi.notes[noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[100+1]])].time );
        if(preview) audio.start(mp3.rate, 1024);
        //else while(midi.time < midi.duration) step();
    }

    uint read(const mref<short2>& output) {
        size_t readSize = mp3.read(output);
        audioTime += readSize;
        return readSize;
    }

    void seek(uint64 midiTime) {
        audioTime = midiTime * mp3.rate / midi.ticksPerSeconds;
        mp3.seek(audioTime); //FIXME: return actual frame time
        videoTime = audioTime*encoder.fps/mp3.rate; // FIXME: remainder
        //log(videoTime/float(encoder.fps), audioTime/float(mp3.rate), midiTime/float(midi.ticksPerSeconds));
        follow();
        position = target;
    }

    void follow() {
        Rect update = Rect(0);
        for(;midiIndex < midi.notes.size && midi.notes[midiIndex].time*encoder.fps <= videoTime*midi.ticksPerSeconds; midiIndex++) {
            MidiNote note = midi.notes[midiIndex];
            if(note.velocity) {
                if(noteToBlit[noteIndex]!=uint(-1)) {
                    uint blitIndex = noteToBlit[noteIndex];
                    active.insertMulti(note.key, blitIndex);
                    Sheet::Blit& blit = sheet.blits[blitIndex];
                    update |= blit.position+Rect(blit.image.size());
                }
                noteIndex++;
            }
            else if(!note.velocity && active.contains(note.key)) {
                while(active.contains(note.key)) {
                    uint blitIndex = active.take(note.key);
                    Sheet::Blit& blit = sheet.blits[blitIndex];
                    update |= blit.position+Rect(blit.image.size());
                }
            }
        }
        if(!update) return;
        contentChanged = true;

        sheet.colors.clear();
        for(uint index: active.values) sheet.colors.insert(index, red);
        if(active) target = min(apply(active.values,[this](uint index){return sheet.blits[index].position.x;}));

        //sheet.render(image, update);
    }

    // Render loop
    void render(const Image& image) override {
        follow();
        // Smooth scroll animation (assumes constant time step)
        const float k=1./encoder.fps, b=1./encoder.fps; // Stiffness and damping constants
        speed = b*speed + k*(target-position); // Euler integration of speed from forces of spring equation (capped)
        //speed = min(2.f, speed);
        //speed = float((sheet.measures.last()-image.width)*midi.ticksPerSeconds)/float(midi.duration*encoder.fps);
        position = position + speed; // Euler integration of position from speed
        //position = target - (image.size().x-1)/2; // Snap to current note
        //position = sheet.measures[max(0, sheet.measureIndex(target)/3*3-1)]; // Snap several measures at a time
        //if(sheet.position != round(position)) sheet.position = round(position), contentChanged = true;
        if(lastPosition != round(position)) lastPosition = round(position), contentChanged = true;
        if(contentChanged) {
            Image full = clip(this->image, int2(lastPosition, 0)+Rect(/*oversample*/4*image.size()));
            log(image.size(), oversample, full.size());
            copy(image, downsample(downsample(full))); contentChanged=false; }
        if(encode) encoder.writeVideoFrame(image);
        videoTime++;
        window.render();
    }
} app;
