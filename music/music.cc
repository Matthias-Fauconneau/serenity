#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "encoder.h"
#include "window.h"
#include "audio.h"

struct Music : Widget {
    Thread audioThread;
    string name = arguments()[0];
    MusicXML xml = readFile(name+".xml"_);
    Sheet sheet {xml.signs, xml.divisions, 360};
    AudioFile mp3 = name+".mp3"_; // 48KHz AAC would be better
    MidiFile midi = readFile(name+".mid"_);
    buffer<uint> noteToBlit = sheet.synchronize(apply(filter(midi.notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;}));

    // Highlighting
    map<uint,uint> active; // Maps active keys to notes (indices)
    uint midiIndex = 0, noteIndex = 0;
    float targetPosition=0, speed=0, position=0; // X target/speed/position of sheet view

    // Encoding
    //bool contentChanged = true;
    bool encode = false;
    Encoder encoder {name /*, {&audioFile, &AudioFile::read}*/}; //TODO: remux without reencoding

    // Preview
    bool preview = true;
    Timer timer;
    Window window {this, int2(1280,360), "MusicXML"_};
    AudioOutput audio {{this,&Music::read}, audioThread};
    uint64 audioTime = 0, videoTime = 0;
    Image image {uint(sheet.measures.last()+4), uint(window.size.y)};
    Image blurred = image.size()-int2(4, 0);
    static constexpr uint blurWidth = 4;
    uint lastPosition = 0;

    uint noteIndexToMidiIndex(uint seekNoteIndex) {
        uint midiIndex=0;
        for(uint noteIndex=0; noteIndex<seekNoteIndex; midiIndex++) if(midi.notes[midiIndex].velocity) noteIndex++;
        return midiIndex;
    }

    Music() {
        renderBackground(image, White);
        sheet.render(image, Rect(image.size()));
        blurred.buffer.clear(0xFF);
        blurX<blurWidth>(blurred, image);

        if(sheet.synchronizationFailed) { window.background=White; return; }
        window.background = NoBackground; // Only clear on changes
        window.widget = this;
        //seek( midi.notes[noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[100+1]])].time );
        if(preview) { audio.start(mp3.rate, 1024); audioThread.spawn(); }
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
        position = targetPosition;
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
        //contentChanged = true;

        sheet.colors.clear();
        for(uint index: active.values) sheet.colors.insert(index, red);
        if(active) targetPosition = min(apply(active.values,[this](uint index){return sheet.blits[index].position.x;}));

        sheet.render(image, update);
        update.min.x -= 4;
        Rect update2 = update; update2.max.x += 4;
        blurX<blurWidth>(clip(blurred,update), clip(image,update2));
    }

    // -> image
    template<uint width> void blurX(const Image& target, const Image& source) {
        assert_(target.size()+int2(width, 0) == source.size());
        for(uint y: range(target.height)) {
            for(uint c: range(3)) { // FIXME: planar
                uint8 const* row = &source(0, y)[c];
                uint8* const targetRow = &target(0, y)[c];
                uint sum = 0;
                for(uint x: range(width)) sum += row[x*4];
                for(uint x: range(target.width)) {
                    uint8 const* source = row+x*4;
                    sum += source[width*4];
                    targetRow[x*4] = sum / (width+1);
                    sum -= source[0];
                }
            }
        }
    }

    // -> image
    void resampleX(const Image& target, const Image& source, float fract) {
        assert_(target.size()+int2(1, 0) == source.size());
        uint b8 = 256*fract, a8 = 256-b8;
        for(uint y: range(target.height)) {
            for(uint c: range(3)) { // FIXME: planar
                uint8 const* row = &source(0, y)[c];
                uint8* const targetRow = &target(0, y)[c];
                for(uint x: range(target.width)) {
#if 0
                    extern float sRGB_reverse[0x100];
                    extern uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
                    float a = sRGB_reverse[source(i+x,y)[c]];
                    float b = sRGB_reverse[source(i+x+1,y)[c]];
                    float linear = (1-u) * a + u * b;
                    target(x,y)[c] = sRGB_forward[int(round(0xFFF*linear))];
#elif 1
                    uint8 const* source = row+x*4;
                    targetRow[x*4] = (a8 * uint(source[0]) + b8 * uint(source[4])) / 256;
#endif
                }
            }
        }
    }

    // Render loop
    void render(const Image& target) override {
        uint nextTime = window.msc-window.firstMSC;
        if(videoTime+1 != nextTime) log("Dropped", nextTime-(videoTime+1));
        videoTime = nextTime;
        follow();
        // Smooth scroll animation (assumes constant time step)
        const float k=4./window.size.x, b=1./encoder.fps; // Stiffness and damping constants
        speed = b*speed + k*(targetPosition-position); // Euler integration of speed from forces of spring equation (capped)
        static float maxSpeed = 0; if(speed > maxSpeed) { maxSpeed = speed; log(maxSpeed); }
        //speed = min(2.f, speed);
        //speed = float((sheet.measures.last()-image.width)*midi.ticksPerSeconds)/float(midi.duration*encoder.fps);
        //float lastPosition = position;
        position = position + speed; // Euler integration of position from speed
        //position = target - (image.size().x-1)/2; // Snap to current note
        //position = sheet.measures[max(0, sheet.measureIndex(target)/3*3-1)]; // Snap several measures at a time
        //if(sheet.position != round(position)) sheet.position = round(position), contentChanged = true;
        //if(lastPosition != round(position)) lastPosition = round(position), contentChanged = true;
        //if(contentChanged) {
        Image source = clip(blurred, int2(floor(position), 0)+Rect(target.size()+int2(1,0)));
            //blurX(image, this->image, lastPosition, position+1); // TODO: resample (+motion blur)?
        resampleX(target, source, fract(position)); // TODO: resample
            //contentChanged=false;
        //}
        if(encode) encoder.writeVideoFrame(image);
        //videoTime++;
        window.render();
    }
} app;
