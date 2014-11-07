#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "ffmpeg.h"
#include "window.h"
#include "interface.h"
#include "layout.h"
#include "keyboard.h"
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

struct Music : Widget {
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());

	// MusicXML file
    MusicXML xml = readFile(name+".xml"_);
	// MIDI file
    MidiFile midi = readFile(name+".mid"_);
	// Audio file
	buffer<String> audioFiles = filter(Folder(".").list(Files),
							   [this](string path) { return !startsWith(path, name) || (!endsWith(path, ".mp3") && !endsWith(path, ".m4a")); });
	AudioFile audioFile = audioFiles ? AudioFile(audioFiles[0]) : AudioFile();

	// Rendering
	Scroll<Sheet> sheet {xml.signs, xml.divisions, apply(filter(midi.notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;})};
	bool failed = sheet.firstSynchronizationFailureChordIndex != invalid;
	Keyboard keyboard;
	VBox widget {{&sheet, &keyboard}};

	// Highlighting
	map<uint, Sign> active; // Maps active keys to notes (indices)
	uint midiIndex = 0, noteIndex = 0;
	float targetPosition=0, speed=0, position=0; // X target/speed/position of sheet view

	// Preview --
	Window window {this, int2(1280,720), [](){return "MusicXML"__;}};
	uint64 previousFrameCounterValue = 0;
	uint64 videoTime = 0;
	// Audio output
    Thread audioThread;
    AudioOutput audio {{this,&Music::read}, audioThread};
    uint64 audioTime = 0;

	uint read(const mref<short2>& output) {
		size_t readSize = audioFile.read(output);
		audioTime += readSize;
		return readSize;
	}

    uint noteIndexToMidiIndex(uint seekNoteIndex) {
        uint midiIndex=0;
        for(uint noteIndex=0; noteIndex<seekNoteIndex; midiIndex++) if(midi.notes[midiIndex].velocity) noteIndex++;
        return midiIndex;
    }

	void follow(uint timeNum, uint timeDen) {
		bool contentChanged = false;
		for(;midiIndex < midi.notes.size && midi.notes[midiIndex].time*timeDen <= timeNum*midi.ticksPerSeconds; midiIndex++) {
			MidiNote note = midi.notes[midiIndex];
			if(note.velocity) {
				Sign sign = sheet.midiToSign[noteIndex];
				if(sign.type == Sign::Note) {
					(sign.staff?keyboard.left:keyboard.right).append( sign.note.key );
					sheet.notation->glyphs[active.insertMulti(note.key, sign).note.glyphIndex].color = (sign.staff?red:green);
					contentChanged = true;
				}
				noteIndex++;
			}
			else if(!note.velocity && active.contains(note.key)) {
				while(active.contains(note.key)) {
					Sign sign = active.take(note.key);
					(sign.staff?keyboard.left:keyboard.right).remove(sign.note.key);
					sheet.notation->glyphs[sign.note.glyphIndex].color = black;
					contentChanged = true;
				}
			}
		}
		if(!contentChanged) return;
		if(active) targetPosition = min(apply(active.values, [this](Sign sign){return sheet.notation->glyphs[sign.note.glyphIndex].origin.x;}));
	}

	void seek(uint64 unused midiTime) {
		if(audio) {
			audioTime = midiTime * audioFile.rate / midi.ticksPerSeconds;
			audioFile.seek(audioTime); //FIXME: return actual frame time
			videoTime = audioTime * window.framesPerSecond / audioFile.rate; // FIXME: remainder
		} else {
			videoTime = midiTime * window.framesPerSecond / midi.ticksPerSeconds;
			previousFrameCounterValue=window.currentFrameCounterValue;
			follow(videoTime, window.framesPerSecond);
		}
	}

	// Double Euler integration of position from spring equation: dtt x = -k δx
	void step(const float dt, int2 size) {
		const float b = -1, k = 1./4; // damping [1/T] and stiffness [1/TT] constants
		speed += dt * (b*speed - k*(position-targetPosition)); // Euler time integration of acceleration [px/s²] from spring equation to speed [px/s]
		position += dt * speed; // Euler time integration of speed [px/s] to position [px]
		sheet.offset.x = -clip(0.f, position, float(abs(sheet.widget().sizeHint(size).x)-size.x));
	}

    Music() {
		window.background = Window::White;
		sheet.horizontal=true, sheet.vertical=false, sheet.scrollbar = true;
		if(failed) { // Seeks to first synchronization failure
			size_t measureIndex = 0;
			for(;measureIndex < sheet.measureToChord.size; measureIndex++)
				if(sheet.measureToChord[measureIndex]>=sheet.firstSynchronizationFailureChordIndex) break;
			sheet.offset.x = -sheet.measures[max<int>(0, measureIndex-3)];
		} else { // Seeks to first note
			seek( midi.notes[noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[0]])].time );
		}
		if(arguments().contains("encode")) { // Encode
			Encoder encoder {name, int2(1280,720), 60, audioFile};
			Image target(encoder.size);
			Time renderTime, encodeTime, totalTime;
			totalTime.start();
            for(int lastReport=0, done=0;!done;) {
				assert_(encoder.audioStream->time_base.num == 1);
				while(encoder.audioTime*encoder.videoFrameRate <= encoder.videoTime*encoder.audioStream->time_base.den) {
					AVPacket packet;
					if(av_read_frame(audioFile.file, &packet) < 0) { done=1; break; }
					packet.pts=packet.dts=encoder.audioTime=
							int64(packet.pts)*encoder.audioStream->codec->time_base.den/audioFile.audioStream->codec->time_base.den;
					packet.duration = int64(packet.duration)*encoder.audioStream->time_base.den/audioFile.audioStream->time_base.den;
					packet.stream_index = encoder.audioStream->index;
					av_interleaved_write_frame(encoder.context, &packet);
                }
				while(encoder.audioTime*encoder.videoFrameRate > encoder.videoTime*encoder.audioStream->time_base.den) {
					follow(encoder.videoTime, encoder.videoFrameRate);
					step(1./encoder.videoFrameRate, target.size);
					renderTime.start();
					fill(target, 0, target.size, 1, 1);
					::render(target, widget.graphics(target.size, Rect(target.size)));
					renderTime.stop();
					encodeTime.start();
                    encoder.writeVideoFrame(target);
					encodeTime.stop();
                }
				int percent = round(100.*encoder.audioTime/audioFile.duration);
				if(percent!=lastReport) { log(str(percent,2)+"%", str(renderTime, totalTime), str(encodeTime, totalTime)); lastReport=percent; }
				//if(percent==5) break; // DEBUG
            }
			requestTermination(0); // window prevents automatic termination
		} else { // Preview
			window.show();
			if(playbackDeviceAvailable()) audio.start(audioFile.rate, 1024); audioThread.spawn();
		}
    }

	int2 sizeHint(int2 size) override { return failed ? sheet.ScrollArea::sizeHint(size) : widget.sizeHint(size); }
	shared<Graphics> graphics(int2 size) override {
		if(failed) return sheet.ScrollArea::graphics(size);
		if(!previousFrameCounterValue) previousFrameCounterValue=window.currentFrameCounterValue;
		int64 elapsedFrameCount = int64(window.currentFrameCounterValue) - int64(previousFrameCounterValue);
		if(elapsedFrameCount>1) log("Skipped", elapsedFrameCount, "frames"); // PROFILE
		if(audio) {
			videoTime = audioTime * window.framesPerSecond / audioFile.rate + elapsedFrameCount /*Compensates renderer latency*/;
		} else {
			videoTime += elapsedFrameCount;
		}
		follow(videoTime, window.framesPerSecond);
		step(float(elapsedFrameCount)/float(window.framesPerSecond), size);
		previousFrameCounterValue = window.currentFrameCounterValue;
		window.render();
		return widget.graphics(size, Rect(size));
    }
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
		return sheet.ScrollArea::mouseEvent(cursor, size, event, button, focus);
	}
	bool keyPress(Key key, Modifiers modifiers) override { return sheet.ScrollArea::keyPress(key, modifiers); }
} app;
