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
#include "sampler.h"

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

/// Converts signs to notes
MidiNotes notes(ref<Sign> signs, uint divisions) {
	MidiNotes notes;
	for(Sign sign: signs) {
		if(sign.type==Sign::Metronome) {
			assert_(!notes.ticksPerSeconds);
			notes.ticksPerSeconds = sign.metronome.perMinute*divisions;
		}
		else if(sign.type == Sign::Note) {
			notes.insertSorted({uint(sign.time*60), sign.note.key, 64/*FIXME: use dynamics*/});
			notes.insertSorted({uint((sign.time+sign.duration)*60), sign.note.key, 0});
		}
	}
	assert_(notes.ticksPerSeconds);
	return notes;
}

struct Music : Widget {
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());

	// MusicXML file
    MusicXML xml = readFile(name+".xml"_);
	// MIDI file
	MidiNotes notes = existsFile(name+".mid"_) ? MidiFile(readFile(name+".mid"_)) : ::notes(xml.signs, xml.divisions);

	// Audio file
	buffer<String> audioFiles = filter(Folder(".").list(Files),
							   [this](string path) { return !startsWith(path, name) || (!endsWith(path, ".mp3") && !endsWith(path, ".m4a")); });
	AudioFile audioFile = audioFiles ? AudioFile(audioFiles[0]) : AudioFile();

	// Rendering
	Scroll<Sheet> sheet {xml.signs, xml.divisions, apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;})};
	bool failed = sheet.firstSynchronizationFailureChordIndex != invalid;
	bool running = !failed;
	Keyboard keyboard;
	VBox widget {{&sheet, &keyboard}};

	// Highlighting
	map<uint, Sign> active; // Maps active keys to notes (indices)
	uint midiIndex = 0, noteIndex = 0;

	// Preview --
	Window window {this, int2(1280,720), [](){return "MusicXML"__;}};
	uint64 previousFrameCounterValue = 0;
	uint64 videoTime = 0;
	// Audio output
    Thread audioThread;
	AudioOutput audio16 = {{this,&Music::read16}, audioThread};
	AudioOutput audio32 = {{this,&Music::read32}, audioThread};
	AudioOutput& audio = audioFile ? audio16 : audio32; // FIXME
	Thread decodeThread;
	Sampler sampler {48000, "/Samples/Salamander.sfz"_, {this, &Music::timeChanged}, decodeThread};
    uint64 audioTime = 0;

	/// Adds new notes to be played (called in audio thread by sampler)
	uint samplerMidiIndex = 0;
	void timeChanged(uint64 time) {
		while(samplerMidiIndex < notes.size && notes[samplerMidiIndex].time*sampler.rate <= time*notes.ticksPerSeconds) {
			auto note = notes[samplerMidiIndex];
			sampler.noteEvent(note.key, note.velocity);
			samplerMidiIndex++;
		}
	}

	size_t read16(mref<short2> output) {
		size_t readSize = audioFile.read(output);
		audioTime += readSize;
		return readSize;
	}

	size_t read32(mref<int2> output) {
		size_t readSize = sampler.read(output);
		audioTime += readSize;
		return readSize;
	}

    uint noteIndexToMidiIndex(uint seekNoteIndex) {
        uint midiIndex=0;
		for(uint noteIndex=0; noteIndex<seekNoteIndex; midiIndex++) {
			assert_(midiIndex < notes.size);
			if(notes[midiIndex].velocity) noteIndex++;
		}
        return midiIndex;
    }

	bool follow(uint64 timeNum, uint64 timeDen, int2 size) {
		bool contentChanged = false;
		for(;midiIndex < notes.size && notes[midiIndex].time*timeDen <= timeNum*notes.ticksPerSeconds; midiIndex++) {
			MidiNote note = notes[midiIndex];
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
		for(size_t index: range(sheet.measures.size()-1)) {
			uint64 t0 = sheet.measures.keys[index], t1 = sheet.measures.keys[index+1];
			assert_(t0<t1);
			if(t0*timeDen <= timeNum*xml.divisions && timeNum*xml.divisions <= t1*timeDen) {
				float x0 = sheet.measures.values[index], x1 = sheet.measures.values[index+1];
				assert_(x0<x1);
				float x = x0+(x1-x0)*(timeNum*xml.divisions-t0*timeDen)/((t1-t0)*timeDen);
				sheet.offset.x = -clip(0.f, x-size.x/2, float(abs(sheet.widget().sizeHint(size).x)-size.x));
				break;
			}
		}
		return contentChanged;
	}

	/*void seek(uint64 midiTime) {
		if(audio) {
			audioTime = midiTime * audioFile.rate / notes.ticksPerSeconds;
			audioFile.seek(audioTime); //FIXME: return actual frame time
			videoTime = audioTime * window.framesPerSecond / audioFile.rate; // FIXME: remainder
		} else {
			assert_(notes.ticksPerSeconds);
			videoTime = midiTime * window.framesPerSecond / notes.ticksPerSeconds;
			previousFrameCounterValue=window.currentFrameCounterValue;
			follow(videoTime, window.framesPerSecond);
		}
	}*/

    Music() {
		window.background = Window::White;
		sheet.horizontal=true, sheet.vertical=false, sheet.scrollbar = true;
		if(failed) { // Seeks to first synchronization failure
			size_t measureIndex = 0;
			for(;measureIndex < sheet.measureToChord.size; measureIndex++)
				if(sheet.measureToChord[measureIndex]>=sheet.firstSynchronizationFailureChordIndex) break;
			sheet.offset.x = -sheet.measures.values[max<int>(0, measureIndex-3)];
		} /*else if(running) { // Seeks to first note
			assert_(0 < sheet.measureToChord.size);
			assert_(sheet.measureToChord[0] < sheet.chordToNote.size);
			assert_(noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[0]])<notes.size);
			seek( notes[noteIndexToMidiIndex(sheet.chordToNote[sheet.measureToChord[0]])].time );
		}*/
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
					follow(encoder.videoTime, encoder.videoFrameRate, target.size);
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
			if(!audioFile) decodeThread.spawn(); // for sampler
			window.show();
			if(playbackDeviceAvailable()) {
				audio.start(audioFile.rate ?: sampler.rate, sampler.periodSize, audioFile ? 16 : 32);
				assert_(audio.rate == audioFile.rate ?: sampler.rate);
				audioThread.spawn();
			}
		}
    }

	int2 sizeHint(int2 size) override { return failed ? sheet.ScrollArea::sizeHint(size) : widget.sizeHint(size); }
	shared<Graphics> graphics(int2 size) override {
		if(!running) return sheet.ScrollArea::graphics(size);
		if(!previousFrameCounterValue) previousFrameCounterValue=window.currentFrameCounterValue;
		//int64 elapsedFrameCount = int64(window.currentFrameCounterValue) - int64(previousFrameCounterValue);
		//if(elapsedFrameCount>1) log("Skipped", elapsedFrameCount, "frames"); // PROFILE
#if 0
		if(audio) {
			videoTime = (audioTime - audio.periodSize/*latency*/) * window.framesPerSecond / audio.rate;
		}/*else {
			videoTime += elapsedFrameCount;
			if(!audio && !audioFile) while(audioTime * window.framesPerSecond < videoTime * sampler.rate) {
				log("Simulation");
				int buffer[2*sampler.periodSize]; read32(mref<int2>((int2*)buffer, sampler.periodSize));
			}
		}*/
		follow(videoTime, window.framesPerSecond, size);
#endif
		follow(sampler.time, sampler.rate, size);
		previousFrameCounterValue = window.currentFrameCounterValue;
		window.render();
		return widget.graphics(size, Rect(size));
    }
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
		return sheet.ScrollArea::mouseEvent(cursor, size, event, button, focus);
	}
	bool keyPress(Key key, Modifiers modifiers) override { return sheet.ScrollArea::keyPress(key, modifiers); }
} app;
