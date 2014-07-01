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

struct Music /*: Widget*/ {
    string name = arguments()[0];
    MusicXML xml = readFile(name+".xml"_);
    Sheet sheet {xml.signs, xml.divisions, 720};
    AudioFile mp3 = name+".mp3"_; // 48KHz AAC would be better
    MidiFile midi = readFile(name+".mid"_);
    buffer<uint> noteToBlit = sheet.synchronize(apply(filter(midi.notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;}));

    // Highlighting
    map<uint,uint> active; // Maps active keys to notes (indices)
    uint midiIndex = 0, noteIndex = 0;
    float targetPosition=0, speed=0, position=0; // X target/speed/position of sheet view

    // Encoding
#define ENCODE 1
#if ENCODE
    Encoder encoder {name, 1280, 720, 60, mp3};
#else
    // Preview
    bool preview = !encode;
    Timer timer;
    Window window {this, int2(1280,720), "MusicXML"_};
    Thread audioThread;
    AudioOutput audio {{this,&Music::read}, audioThread};
    uint64 audioTime = 0, videoTime = 0;
#endif
    uint noteIndexToMidiIndex(uint seekNoteIndex) {
        uint midiIndex=0;
        for(uint noteIndex=0; noteIndex<seekNoteIndex; midiIndex++) if(midi.notes[midiIndex].velocity) noteIndex++;
        return midiIndex;
    }

    Music() {
#if PREVIEW
        window.background = White;
        //seek( midi.notes[noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[100+1]])].time );
        if(preview) { window.show(); audio.start(mp3.rate, 1024); audioThread.spawn(); }
#else
        /*else*/ {
            Image target(encoder.size());
            for(int lastReport=0;;) {
                while(encoder.audioTime*encoder.fps <= encoder.videoTime*encoder.rate) {
                    AVPacket packet;
                    if(av_read_frame(mp3.file, &packet) < 0) break;
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
                    step();
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

    void step() {
        const float dt=1./encoder.fps, k=dt, b=-1; // Time step, stiffness (1/frame) and damping (1) constants
        speed += dt * (b*speed + k*(targetPosition-position)); // Euler integration of speed (px/frame) from forces of spring equation
        position += speed; // Euler integration of position (in pixels) from speed (in pixels per frame)
    }

#if PREVIEW
    uint read(const mref<short2>& output) {
        size_t readSize = mp3.read(output);
        audioTime += readSize;
        return readSize;
    }

    void seek(uint64 midiTime) {
        audioTime = midiTime * mp3.rate / midi.ticksPerSeconds;
        mp3.seek(audioTime); //FIXME: return actual frame time
        videoTime = audioTime*encoder.fps/mp3.rate; // FIXME: remainder
        follow(videoTime, 60);
        position = targetPosition;
    }

    // Render loop
    void render(const Image& target) override {
        uint nextTime = window.msc-window.firstMSC;
        if(videoTime+1 != nextTime) { static int dropCount=0; int drop=nextTime-(videoTime+1); if(drop>0) { dropCount+=drop; log("Dropped",drop,dropCount); } }
        while(videoTime < nextTime) { follow(videoTime, 60); step(); videoTime++; }
        position = min(float(sheet.measures.last()-target.size().x), position);
        sheet.render(target, int2(floor(-position), 0), target.size());
        window.render();
    }
#endif
} app;
