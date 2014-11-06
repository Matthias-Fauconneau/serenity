#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "window.h"
#include "interface.h"
#include "audio.h"
#include "render.h"
#include "encoder.h"
#include "time.h"

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

#define ENCODE 1
#define PREVIEW !ENCODE
#define AUDIO 0
#define MIDI 1

struct Music
#if PREVIEW
        : Widget
#endif
{
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());
    MusicXML xml = readFile(name+".xml"_);
	Scroll<Sheet> sheet {xml.signs, xml.divisions};
#if AUDIO || ENCODE
	AudioFile mp3 {name+".mp3"_}; // 48KHz AAC would be better
#endif
#if MIDI
    MidiFile midi = readFile(name+".mid"_);
	buffer<size_t> noteToGlyph =
			sheet.synchronize(apply(filter(midi.notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;}));
#endif

    // Highlighting
    map<uint,uint> active; // Maps active keys to notes (indices)
    uint midiIndex = 0, noteIndex = 0;
    float targetPosition=0, speed=0, position=0; // X target/speed/position of sheet view

    // Encode
#if ENCODE
    Encoder encoder {name, 1280, 720, 60, mp3};
	int2 size = encoder.size;
#endif
    // Preview
#if PREVIEW
    bool preview = PREVIEW;
	Window window {this, int2(1280, 720), [](){return "MusicXML"__;}};
	int2 size = window.size;
	uint64 videoTime = 0;
	uint64 previousFrameCounterValue = 0;
#if AUDIO
    Thread audioThread;
    AudioOutput audio {{this,&Music::read}, audioThread};
    uint64 audioTime = 0;
#endif
	bool running = true;
#endif
#if MIDI
    uint noteIndexToMidiIndex(uint seekNoteIndex) {
        uint midiIndex=0;
        for(uint noteIndex=0; noteIndex<seekNoteIndex; midiIndex++) if(midi.notes[midiIndex].velocity) noteIndex++;
        return midiIndex;
    }
#endif

    Music() {
#if PREVIEW
		window.background = Window::White;
		sheet.horizontal=true, sheet.vertical=false, sheet.scrollbar = true;
		if(sheet.firstSynchronizationFailureChordIndex != invalid) {
			size_t measureIndex = 0;
			for(;measureIndex < sheet.measureToChord.size; measureIndex++)
				if(sheet.measureToChord[measureIndex]>=sheet.firstSynchronizationFailureChordIndex) break;
			sheet.offset.x = -sheet.measures[max<int>(0, measureIndex-3)];
		}
#if MIDI
		if(sheet.firstSynchronizationFailureChordIndex == invalid) {
			seek( midi.notes[noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[0/*122*/]])].time );
		}
#endif
        if(preview) {
            window.show();
#if AUDIO
            audio.start(mp3.rate, 1024); audioThread.spawn();
#endif
        }
#else
        /*else*/ {
			Image target(encoder.size);
			Time renderTime, encodeTime, totalTime;
			totalTime.start();
            for(int lastReport=0, done=0;!done;) {
                while(encoder.audioTime*encoder.fps <= encoder.videoTime*encoder.rate) {
                    AVPacket packet;
                    if(av_read_frame(mp3.file, &packet) < 0) { done=1; break; }
                    assert_(mp3.file->streams[packet.stream_index]==mp3.audioStream);
                    assert_(mp3.audioStream->time_base.num==1);
					assert_(packet.pts*encoder.audioStream->time_base.den%mp3.audioStream->time_base.den==0);
                    encoder.audioTime = packet.pts*encoder.audioStream->time_base.den/mp3.audioStream->time_base.den;
                    assert_(encoder.audioStream->time_base.num==1 && uint(encoder.audioStream->time_base.den)==encoder.rate);
					assert_(packet.dts == packet.pts);
                    packet.pts = packet.dts = encoder.audioTime;
					assert_((int64)packet.duration*encoder.audioStream->time_base.den%mp3.audioStream->time_base.den==0);
                    packet.duration = (int64)packet.duration*encoder.audioStream->time_base.den/mp3.audioStream->time_base.den;
                    packet.stream_index = encoder.audioStream->index;
                    av_interleaved_write_frame(encoder.context, &packet);
                }
                while(encoder.audioTime*encoder.fps > encoder.videoTime*encoder.rate) {
                    follow(encoder.videoTime, encoder.fps);
                    step(1./encoder.fps);
					renderTime.start();
					fill(target, 0, target.size, 1, 1);
					::render(target, sheet.ScrollArea::graphics(target.size));
					renderTime.stop();
					encodeTime.start();
                    encoder.writeVideoFrame(target);
					encodeTime.stop();
                }
                int percent = round(100.*encoder.audioTime/mp3.duration);
				if(percent!=lastReport) { log(str(percent,2)+"%", str(renderTime, totalTime), str(encodeTime, totalTime)); lastReport=percent; }
				if(percent==25) break; // DEBUG
            }
        }
#endif
    }

#if MIDI
	void follow(uint timeNum, uint timeDen) {
        bool contentChanged = false;
		for(;midiIndex < midi.notes.size && midi.notes[midiIndex].time*timeDen <= timeNum*midi.ticksPerSeconds; midiIndex++) {
            MidiNote note = midi.notes[midiIndex];
			if(note.velocity) {
				size_t glyphIndex = noteToGlyph[noteIndex];
				if(glyphIndex!=invalid) {
					sheet.notation->glyphs[active.insertMulti(note.key, glyphIndex)].color = red;
					contentChanged = true;
				}
                noteIndex++;
            }
            else if(!note.velocity && active.contains(note.key)) {
				while(active.contains(note.key)) {
					sheet.notation->glyphs[active.take(note.key)].color = black;
					contentChanged = true;
				}
            }
        }
        if(!contentChanged) return;
		if(active) targetPosition = min(apply(active.values,[this](uint index){return sheet.notation->glyphs[index].origin.x;}));
    }

    void step(const float dt) {
        const float b=-1, k=1; // damping [1] and stiffness [1/T2] constants
        speed += dt * (b*speed + k*(targetPosition-position)); // Euler integration of speed (px/s) from acceleration by spring equation (px/s2)
        position += dt * speed; // Euler integration of position (in pixels) from speed (in pixels/s)
		sheet.offset.x = -clip(0, int(round(position)), sheet.widget().sizeHint(size).x-size.x);
    }
#endif
#if PREVIEW
#if AUDIO
    uint read(const mref<short2>& output) {
        size_t readSize = mp3.read(output);
        audioTime += readSize;
        return readSize;
    }
#endif

	void seek(uint64 unused midiTime) {
#if AUDIO
        audioTime = midiTime * mp3.rate / midi.ticksPerSeconds;
        mp3.seek(audioTime); //FIXME: return actual frame time
		videoTime = audioTime * window.framesPerSecond / mp3.rate; // FIXME: remainder
#elif MIDI
		videoTime = midiTime * window.framesPerSecond / midi.ticksPerSeconds;
		previousFrameCounterValue=window.currentFrameCounterValue;
		follow(videoTime, window.framesPerSecond);
#endif
    }

	int2 sizeHint(int2 size) override { return sheet.ScrollArea::sizeHint(size); }
	shared<Graphics> graphics(int2 size) override {
		if(!previousFrameCounterValue) previousFrameCounterValue=window.currentFrameCounterValue;
		int64 elapsedFrameCount = int64(window.currentFrameCounterValue) - int64(previousFrameCounterValue);
		if(elapsedFrameCount>1) log("Skipped", elapsedFrameCount, "frames"); // PROFILE
#if AUDIO
		videoTime = audioTime * window.framesPerSecond / mp3.rate + elapsedFrameCount /*Compensates renderer latency*/;
#else
		videoTime += elapsedFrameCount;
#endif
#if MIDI
		follow(videoTime, window.framesPerSecond);
		step(float(elapsedFrameCount)/float(window.framesPerSecond));
		previousFrameCounterValue = window.currentFrameCounterValue;
        if(running) window.render();
#endif
		return sheet.ScrollArea::graphics(size);
    }
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
		return sheet.ScrollArea::mouseEvent(cursor, size, event, button, focus);
	}
	bool keyPress(Key key, Modifiers modifiers) override { return sheet.ScrollArea::keyPress(key, modifiers); }
#endif
} app;
