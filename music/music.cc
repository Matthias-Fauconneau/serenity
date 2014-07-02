#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "encoder.h"
#include "window.h"
#include "audio.h"

// -> ffmpeg.h/cc
extern "C" {
#define _MATH_H // Prevent system <math.h> inclusion which conflicts with local "math.h"
#define _STDLIB_H // Prevent system <stdlib.h> inclusion which conflicts with local "thread.h"
#define __STDC_CONSTANT_MACROS
#include <stdint.h>
#include <libavformat/avformat.h> //avformat
#include <libavcodec/avcodec.h> //avcodec
#include <libavutil/avutil.h> //avutil
}

#define ENCODE 0
#define PREVIEW !ENCODE
#define AUDIO 0

struct Music
#if PREVIEW
        : Widget
#endif
{
    string name = arguments()[0];
    MusicXML xml = readFile(name+".xml"_);
    Sheet sheet {xml.signs, xml.divisions, 720};
#if AUDIO || ENCODE
    AudioFile mp3 = name+".mp3"_; // 48KHz AAC would be better
#endif
    MidiFile midi = readFile(name+".mid"_);
    buffer<uint> noteToBlit = sheet.synchronize(apply(filter(midi.notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;}));

    // Highlighting
    map<uint,uint> active; // Maps active keys to notes (indices)
    uint midiIndex = 0, noteIndex = 0;
    float targetPosition=0, speed=0, position=0; // X target/speed/position of sheet view

    // Encode
#if ENCODE
    bool encode = ENCODE
    Encoder encoder {name, 1280, 720, 60, mp3};
#endif
    // Preview
#if PREVIEW
    bool preview = PREVIEW;
    Window window {this, int2(1280,720), "MusicXML"_};
    const uint fps = 60;
    uint64 videoTime = 0;
#if AUDIO
    Thread audioThread;
    AudioOutput audio {{this,&Music::read}, audioThread};
    uint64 audioTime = 0;
#endif
    bool running = false;
#endif
    uint noteIndexToMidiIndex(uint seekNoteIndex) {
        uint midiIndex=0;
        for(uint noteIndex=0; noteIndex<seekNoteIndex; midiIndex++) if(midi.notes[midiIndex].velocity) noteIndex++;
        return midiIndex;
    }

    Music() {
#if PREVIEW
        window.background = White;
        seek( midi.notes[noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[122]])].time );
        if(preview) {
            window.show();
#if AUDIO
            audio.start(mp3.rate, 1024); audioThread.spawn();
#endif
        }
#else
        /*else*/ {
            Image target(encoder.size());
            for(int lastReport=0, done=0;!done;) {
                while(encoder.audioTime*encoder.fps <= encoder.videoTime*encoder.rate) {
                    AVPacket packet;
                    if(av_read_frame(mp3.file, &packet) < 0) { done=1; break; }
                    assert_(mp3.file->streams[packet.stream_index]==mp3.audioStream);
                    assert_(mp3.audioStream->time_base.num==1);
                    assert_(packet.pts*encoder.audioStream->time_base.den%mp3.audioStream->time_base.den==0, packet.pts, packet.pts/float(mp3.audioStream->time_base.den));
                    encoder.audioTime = packet.pts*encoder.audioStream->time_base.den/mp3.audioStream->time_base.den;
                    assert_(encoder.audioStream->time_base.num==1 && uint(encoder.audioStream->time_base.den)==encoder.rate);
                    assert_(packet.dts == packet.pts, packet.dts, packet.pts);
                    packet.pts = packet.dts = encoder.audioTime;
                    assert_((int64)packet.duration*encoder.audioStream->time_base.den%mp3.audioStream->time_base.den==0, packet.duration, packet.duration/float(mp3.audioStream->time_base.den));
                    packet.duration = (int64)packet.duration*encoder.audioStream->time_base.den/mp3.audioStream->time_base.den;
                    packet.stream_index = encoder.audioStream->index;
                    av_interleaved_write_frame(encoder.context, &packet);
                }
                while(encoder.audioTime*encoder.fps > encoder.videoTime*encoder.rate) {
                    follow(encoder.videoTime, encoder.fps);
                    step(1./encoder.fps);
                    renderBackground(target, White);
                    position = min(float(sheet.measures.last()-target.size().x), position);
                    sheet.render(target, int2(floor(-position), 0), target.size());
                    encoder.writeVideoFrame(target);
                }
                int percent = round(100.*encoder.audioTime/mp3.duration);
                if(percent!=lastReport) { log(percent); lastReport=percent; }
            }
        }
#endif
    }

    void follow(uint timeNum, uint timeDen) {
        bool contentChanged = false;
        for(;midiIndex < midi.notes.size && midi.notes[midiIndex].time*timeDen <= timeNum*midi.ticksPerSeconds; midiIndex++) {
            MidiNote note = midi.notes[midiIndex];
            if(note.velocity) {
                uint blitIndex = noteToBlit[noteIndex];
                if(blitIndex!=uint(-1)) { active.insertMulti(note.key, blitIndex); contentChanged = true; }
                noteIndex++;
            }
            else if(!note.velocity && active.contains(note.key)) {
                while(active.contains(note.key)) { active.remove(note.key); contentChanged = true; }
            }
        }
        if(!contentChanged) return;

        sheet.colors.clear();
        for(uint index: active.values) sheet.colors.insert(index, red);
        if(active) targetPosition = min(apply(active.values,[this](uint index){return sheet.blits[index].position.x;}));
    }

    void step(const float dt) {
        const float b=-1, k=1; // damping [1] and stiffness [1/T2] constants
        speed += dt * (b*speed + k*(targetPosition-position)); // Euler integration of speed (px/s) from acceleration by spring equation (px/s2)
        position += dt * speed; // Euler integration of position (in pixels) from speed (in pixels/s)
    }

#if PREVIEW
#if AUDIO
    uint read(const mref<short2>& output) {
        size_t readSize = mp3.read(output);
        audioTime += readSize;
        return readSize;
    }
#endif

    void seek(uint64 midiTime) {
#if AUDIO
        audioTime = midiTime * mp3.rate / midi.ticksPerSeconds;
        mp3.seek(audioTime); //FIXME: return actual frame time
        videoTime = audioTime*fps/mp3.rate; // FIXME: remainder
#else
        videoTime = midiTime * fps / midi.ticksPerSeconds;
#endif
        follow(videoTime, fps);
        position = targetPosition;
    }

    // Render loop
    void render(const Image& target) override {
        follow(videoTime, fps);
        step(1./fps);
        videoTime++;
        position = min(float(sheet.measures.last()-target.size().x), position);
        sheet.render(target, int2(floor(-position), 0), target.size());
        if(running) window.render();
    }
#endif
} app;
