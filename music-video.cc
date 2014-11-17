#include "MusicXML.h"
#include "midi.h"
#include "sheet.h"
#include "audio.h"
#include "window.h"
#include "interface.h"
#include "layout.h"
#include "keyboard.h"
#include "asound.h"
#include "render.h"
#include "video.h"
#include "time.h"
#include "sampler.h"
#include "encoder.h"
#include "parallel.h"
#include "fft.h"

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
MidiNotes notes(ref<Sign> signs, uint ticksPerQuarter) {
	MidiNotes notes;
	for(Sign sign: signs) {
		if(sign.type==Sign::Metronome) {
			/*assert_(!notes.ticksPerSeconds || notes.ticksPerSeconds == sign.metronome.perMinute*ticksPerQuarter,
					notes.ticksPerSeconds, sign.metronome.perMinute*ticksPerQuarter);
			notes.ticksPerSeconds = sign.metronome.perMinute*ticksPerQuarter;*/
			notes.ticksPerSeconds = max(notes.ticksPerSeconds, int64(sign.metronome.perMinute*ticksPerQuarter));
		}
		else if(sign.type == Sign::Note) {
			if(sign.note.tie == Note::NoTie || sign.note.tie == Note::TieStart)
				notes.insertSorted({sign.time*60, sign.note.key, 64/*FIXME: use dynamics*/});
			if(sign.note.tie == Note::NoTie || sign.note.tie == Note::TieStop)
				notes.insertSorted({(sign.time+sign.duration)*60, sign.note.key, 0});
		}
	}
	assert(notes.last().time >= 0);
	if(!notes.ticksPerSeconds) notes.ticksPerSeconds = 120*ticksPerQuarter; //TODO: default tempo from audio
	assert_(notes.ticksPerSeconds);
	return notes;
}

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
struct BeatSynchronizer : Widget {
	int64 rate;
	Sheet& sheet;
	MidiNotes& notes;
	buffer<Sign> signs = filter(sheet.midiToSign,[](Sign sign){return sign.type!=Sign::Note;});
	struct Bar { float x; bgr3f color; };
	array<Bar> bars;
	int64 currentTime = 0;
	BeatSynchronizer(Audio audio, Sheet& sheet, MidiNotes& notes) : rate(audio.rate), sheet(sheet), notes(notes) {
		// Converts MIDI time base to audio sample rate
		uint firstKey = 21+85, lastKey = 0;
		for(MidiNote& note: notes) {
			note.time = note.time*int64(rate)/notes.ticksPerSeconds;
			firstKey = min(firstKey, note.key);
			lastKey = max(lastKey, note.key);
			assert(note.time >= 0);
		}
		uint keyCount = lastKey+1-firstKey;
		notes.ticksPerSeconds = rate;

		size_t T = audio.size/audio.channels; // Total sample count
		size_t N = 2048; // Frame size (43ms @48KHz)
		size_t h = N; // Hop size (11ms @48KHz (75% overlap))
		FFT fft(N);
		size_t frameCount = (T-N)/h;
		buffer<float> keyOnsetFunctions(keyCount*frameCount); keyOnsetFunctions.clear(0);
		buffer<float> previous = buffer<float>(N/2); previous.clear(0);
		int firstK = floor(keyToPitch(firstKey)*N/rate);
		int lastK = ceil(keyToPitch(lastKey)*N/rate);
		for(size_t frameIndex: range(frameCount)) {
			ref<float> X = audio.slice(frameIndex*h*audio.channels, N*audio.channels);
			//if(audio.channels==1) for(size_t i: range(N)) fft.windowed[i] = fft.window[i] * X[frameIndex*h+i];
			if(audio.channels==2) for(size_t i: range(N)) fft.windowed[i] = fft.window[i] * (X[i*2+0]+X[i*2+1])/2;
			else error(audio.channels);
			fft.spectrum = buffer<float>(N/2);
			fft.transform();
			buffer<float> current = move(fft.spectrum);
			for(size_t k: range(firstK, lastK+1)) {
				float y = max(0.f, current[k] - previous[k]);
				for(uint key: range(max(firstKey, uint(round(pitchToKey(k*rate/N)))), round(pitchToKey((k+1)*rate/N))))
					keyOnsetFunctions[(key-firstKey)*frameCount+frameIndex] += y;
			}
			previous = move(current);
		}

		/// Normalizes onset function
		for(size_t key: range(keyCount)) {
			mref<float> f = keyOnsetFunctions.slice(key*frameCount, frameCount);
			float sum = 0;
			for(float v: f) sum += v;
			float mean = sum / frameCount;
			float SSQ = 0;
			for(float& v: f) { v -= mean; SSQ += sq(v); }
			float deviation = sqrt(SSQ / frameCount);
			for(float& v: f) v /= deviation;
		}

		/// Selects onset peaks
		struct Peak { int64 time; uint key; float value; };
		array<array<Peak>> P; // peaks
		for(size_t i: range(1, frameCount-1)) {
			array<Peak> chord;
			for(uint key: range(keyCount)) {
				ref<float> f = keyOnsetFunctions.slice(key*frameCount, frameCount);
				if(f[i-1] < f[i] && f[i] > f[i+1]) {
					chord.append(Peak{int64(i*h), firstKey+key, f[i]});
				}
			}
			if(chord) P.append(move(chord));
		}

		/// Collects MIDI chords
		array<array<Peak>> S; // score
		array<range> indices;
		for(size_t midiIndex = 0; midiIndex < notes.size;) {
			size_t first = midiIndex;
			while(midiIndex < notes.size && !notes[midiIndex].velocity) midiIndex++;
			if(midiIndex >= notes.size) break;
			int64 time = notes[midiIndex].time;
			array<Peak> chord;
			while(midiIndex < notes.size && notes[midiIndex].time == time) {
				if(notes[midiIndex].velocity) chord.append(Peak{time, notes[midiIndex].key, (float)notes[midiIndex].velocity});
				midiIndex++;
			}
			S.append(move(chord));
			indices.append(range(first, midiIndex));
		}
		indices.last().stop = notes.size;

		/// Synchronizes audio and score
		size_t m = S.size, n = P.size;

		// Global score matrix D
		struct Matrix {
			size_t m,n;
			buffer<float> elements;
			Matrix(size_t m, size_t n) : m(m), n(n), elements(m*n) { elements.clear(0); }
			float& operator()(size_t i, size_t j) { return elements[i*n+j]; }
		} D(m,n);

		for(size_t i: range(m)) for(size_t j: range(n)) {
			float d = 0;
			for(Peak s: S[i]) for(Peak p: P[j]) d += (s.key==p.key || s.key+12==p.key || s.key+12+7==p.key) * p.value;
			D(i,j) = max(max(j==0?0:D(i,j-1), i==0?0:D(i-1, j)), ((i==0||j==0)?0:D(i-1,j-1)) + d);
		};

		/// Best monotonous map
		buffer<int> map (m);
		map.clear(invalid);
		int i = m-1, j = n-1;
		while(i>=0 && j>=0) { //FIXME: reverse ?
			if(D(i,j) == (j==0?0:D(i,j-1))) j--; // Missing from peaks
			else if(D(i,j) == (i==0?0:D(i-1,j))) i--; // Missing from score
			else { map[i] = j; i--; j--; } // Match
		}

		int64 offset = 0;
		for(size_t i: range(m)) {
			if(map[i] != -1) offset = P[map[i]][0].time - S[i][0].time;
			for(size_t midiIndex: indices[i]) notes[midiIndex].time += offset;
		}

		assert_(sheet.ticksPerMinutes);
		{ // Synchronizes measure times with MIDI
			size_t midiIndex = 0, noteIndex = 0;
			for(size_t measureIndex: range(sheet.measureBars.size())) {
				while(noteIndex < sheet.midiToSign.size) {
					Sign sign = sheet.midiToSign[noteIndex];
					if(sign.type == Sign::Note && sign.note.measureIndex==measureIndex) break;
					midiIndex++;
					while(midiIndex < notes.size && !notes[midiIndex].velocity) midiIndex++;
					noteIndex++;
				}
				if(midiIndex < notes.size) {
					sheet.measureBars.keys[measureIndex] = notes[midiIndex].time*sheet.ticksPerMinutes/(60*notes.ticksPerSeconds);
				}
			}
		}

		// MIDI visualization (synchronized to sheet time)
		{size_t midiIndex = 0;
			auto onsets = apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.time;});
			for(int index: range(sheet.measureBars.size()-1)) {
				int64 t0 = (int64)notes.ticksPerSeconds*sheet.measureBars.keys[index]*60/sheet.ticksPerMinutes;
				float x0 = sheet.measureBars.values[index];
				int64 t1 = (int64)notes.ticksPerSeconds*sheet.measureBars.keys[index+1]*60/sheet.ticksPerMinutes;
				float x1 = sheet.measureBars.values[index+1];

				while(midiIndex<onsets.size && t0 <= onsets[midiIndex] && onsets[midiIndex] < t1) {
					assert_(t1>t0);
					float x = x0+(x1-x0)*(onsets[midiIndex]-t0)/(t1-t0);
					bars.append(Bar{x, blue});
					midiIndex++;
				}
			}
		}

		// Beat visualization (synchronized to sheet time)
		{size_t peakIndex = 0;
			for(int index: range(sheet.measureBars.size()-1)) {
				int64 t0 = (int64)rate*sheet.measureBars.keys[index]*60/sheet.ticksPerMinutes;
				float x0 = sheet.measureBars.values[index];
				int64 t1 = (int64)rate*sheet.measureBars.keys[index+1]*60/sheet.ticksPerMinutes;
				float x1 = sheet.measureBars.values[index+1];
				while(peakIndex<P.size && t0 <= P[peakIndex][0].time && P[peakIndex][0].time < t1) {
					assert_(t1>t0);
					float x = x0+(x1-x0)*(P[peakIndex][0].time-t0)/(t1-t0);
					bars.append(Bar{x, red});
					peakIndex++;
				}
			}
		}

		for(float x: sheet.measureBars.values) bars.append(Bar{x, green});
	}
	int2 sizeHint(int2 size) override { return int2(sheet.sizeHint(size).x, 32); }
	shared<Graphics> graphics(int2 size) override {
		assert_(size.x > 1050, size, sizeHint(size));
		shared<Graphics> graphics;
		for(Bar bar: bars) graphics->fills.append(vec2(bar.x, 0), vec2(2, size.y), bar.color, 1.f/2);
		for(int index: range(sheet.measureBars.size()-1)) {
			int64 t0 = (int64)rate*sheet.measureBars.keys[index]*60/sheet.ticksPerMinutes;
			float x0 = sheet.measureBars.values[index];
			int64 t1 = (int64)rate*sheet.measureBars.keys[index+1]*60/sheet.ticksPerMinutes;
			float x1 = sheet.measureBars.values[index+1];
			if(t0 <= currentTime && currentTime < t1) {
				assert_(t1>t0);
				float x = x0+(x1-x0)*(currentTime-t0)/(t1-t0);
				graphics->fills.append(vec2(x, 0), vec2(2, size.y));
			}
		}
		return graphics;
	}
};

struct Music : Widget {
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());

	// MusicXML file
    MusicXML xml = readFile(name+".xml"_);
	// MIDI file
	MidiNotes notes = existsFile(name+".mid"_) ? MidiFile(readFile(name+".mid"_)) : ::notes(xml.signs, xml.divisions);

	// Audio file
	buffer<String> audioFiles = filter(Folder(".").list(Files), [this](string path) { return !startsWith(path, name)
			|| (!endsWith(path, ".mp3") && !endsWith(path, ".m4a") && !endsWith(path, "performance.mp4") && !find(path, ".mkv")); });
	AudioFile audioFile = audioFiles ? AudioFile(audioFiles[0]) : AudioFile();

	// Video input
	buffer<String> videoFiles = filter(Folder(".").list(Files), [this](string path) {
			return !startsWith(path, name) ||  (!endsWith(path, "performance.mp4") && !find(path, ".mkv")); });
	Decoder video = videoFiles ? Decoder(videoFiles[0]) : Decoder();
	ImageView videoView = video ? video.size : Image();

	// Rendering
	Sheet sheet {xml.signs, xml.divisions, apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;})};
	BeatSynchronizer beatSynchronizer {decodeAudio(audioFiles[0]), sheet, notes};
	Scroll<VBox> scroll {{&sheet/*, &beatSynchronizer*/}};
	bool failed = sheet.firstSynchronizationFailureChordIndex != invalid;
	bool running = true; //!failed;
	Keyboard keyboard;
	VBox widget {{&scroll, &videoView, &keyboard}};

	// Highlighting
	map<uint, Sign> active; // Maps active keys to notes (indices)
	uint midiIndex = 0, noteIndex = 0;

	// Preview --
	Window window {this, int2(1280,720), [](){return "MusicXML"__;}};
	uint64 previousFrameCounterValue = 0;
	uint64 videoTime = 0;

	// Audio output
    Thread audioThread;
	AudioOutput audio = {{this, &Music::read32}, audioThread};
#if SAMPLER
	Thread decodeThread;
	Sampler sampler {48000, "/Samples/Salamander.sfz"_, {this, &Music::timeChanged}, decodeThread};
#endif
	size_t read32(mref<int2> output) {
		assert_(audioFile && audioFile.audioTime>=0);
		/**/  if(audioFile.channels == audio.channels) return audioFile.read32(mcast<int>(output));
		/*else if(audioFile.channels == 1) {
			int mono[output.size];
			size_t readSize = audioFile.read32(mref<int>(mono, output.size));
			for(size_t i: range(readSize)) output[i] = mono[i];
			return readSize;
		}*/
		else error(audioFile.channels);
	}

#if SAMPLER
	/// Adds new notes to be played (called in audio thread by sampler)
	uint samplerMidiIndex = 0;
	void timeChanged(uint64 time) {
		while(samplerMidiIndex < notes.size && notes[samplerMidiIndex].time*sampler.rate <= time*notes.ticksPerSeconds) {
			auto note = notes[samplerMidiIndex];
			sampler.noteEvent(note.key, note.velocity);
			samplerMidiIndex++;
		}
	}
#endif

	bool follow(int64 timeNum, int64 timeDen, int2 size) {
		if(timeNum < 0) return false; // FIXME
		bool contentChanged = false;
		for(;midiIndex < notes.size && notes[midiIndex].time*timeDen <= timeNum*notes.ticksPerSeconds; midiIndex++) {
			MidiNote note = notes[midiIndex];
			if(note.velocity) {
				Sign sign = sheet.midiToSign[noteIndex];
				if(sign.type == Sign::Note) {
					(sign.staff?keyboard.left:keyboard.right).append( sign.note.key );
					active.insertMulti(note.key, sign);
					sheet.measures.values[sign.note.measureIndex]->glyphs[sign.note.glyphIndex].color = (sign.staff?red:green);
					contentChanged = true;
				}
				noteIndex++;
			}
			else if(!note.velocity && active.contains(note.key)) {
				while(active.contains(note.key)) {
					Sign sign = active.take(note.key);
					(sign.staff?keyboard.left:keyboard.right).remove(sign.note.key);
					sheet.measures.values[sign.note.measureIndex]->glyphs[sign.note.glyphIndex].color = black;
					contentChanged = true;
				}
			}
		}
		uint64 t = timeNum*sheet.ticksPerMinutes;
		int previousOffset = scroll.offset.x;
		// Cardinal cubic B-Spline
		for(int index: range(sheet.measureBars.size()-1)) {
			uint64 t1 = sheet.measureBars.keys[index]*60*timeDen;
			uint64 t2 = sheet.measureBars.keys[index+1]*60*timeDen;
			if(t1 <= t && t < t2) {
				real f = real(t-t1)/real(t2-t1);
				real w[4] = { 1./6 * cb(1-f), 2./3 - 1./2 * sq(f)*(2-f), 2./3 - 1./2 * sq(1-f)*(2-(1-f)), 1./6 * cb(f) };
				auto X = [&](int index) { return clip(0.f, sheet.measureBars.values[clip<int>(0, index, sheet.measureBars.values.size)] - size.x/2,
							(float)abs(sheet.sizeHint(size).x)-size.x); };
				scroll.offset.x = -round( w[0]*X(index-1) + w[1]*X(index) + w[2]*X(index+1) + w[3]*X(index+2) );
				break;
			}
		}
		if(previousOffset != scroll.offset.x) contentChanged = true;
		beatSynchronizer.currentTime = timeNum*beatSynchronizer.rate/timeDen;
		if(video && video.videoTime*timeDen < timeNum*video.videoFrameRate) {
			if(video.read(videoView.image)) {
				rotate(videoView.image);
				contentChanged=true;
			}
		}
		return contentChanged;
	}

	void seek(uint64 time) {
		if(audioFile) {
			assert_(notes.ticksPerSeconds == audioFile.audioFrameRate);
			audioFile.seek(time); //FIXME: return actual frame time
		}
		if(video) {
			video.seek(time * video.videoFrameRate / notes.ticksPerSeconds);
		}
#if SAMPLER
		sampler.time = time*sampler.rate/notes.ticksPerSeconds;
		while(samplerMidiIndex < notes.size && notes[samplerMidiIndex].time*sampler.rate < time*notes.ticksPerSeconds) samplerMidiIndex++;
#endif
	}

	Music() {
		//TODO: measureBars.t *= 60 when using MusicXML (no MIDI)
		window.background = Window::White;
		scroll.horizontal=true, scroll.vertical=false, scroll.scrollbar = true;
		if(failed && !video) { // Seeks to first synchronization failure
			size_t measureIndex = 0;
			for(;measureIndex < sheet.measureToChord.size; measureIndex++)
				if(sheet.measureToChord[measureIndex]>=sheet.firstSynchronizationFailureChordIndex) break;
			scroll.offset.x = -sheet.measureBars.values[max<int>(0, measureIndex-3)];
		} else if(running) { // Starts one second before first onset
			if(notes[0].time > notes.ticksPerSeconds) seek(notes[0].time-notes.ticksPerSeconds);
		}
#if SAMPLER
		if(sampler) decodeThread.spawn(); // For sampler
#endif
		if(arguments().contains("encode")) { // Encode
			Encoder encoder {name};
			encoder.setVideo(int2(1280,720), 60);
			if(audioFile && audioFile.codec==AudioFile::AAC) encoder.setAudio(audioFile);
			else if(audioFile) encoder.setAudio(audioFile.channels, audioFile.audioFrameRate);
#if SAMPLER
			else encoder.setAudio(sampler.rate);
#else
			else error("audio");
#endif
			encoder.open();
			Time renderTime, encodeTime, totalTime;
			totalTime.start();
			for(int lastReport=0, done=0; !done;) {
				assert_(encoder.audioStream->time_base.num == 1);
				auto writeAudio = [&]{
					if(audioFile && audioFile.codec==AudioFile::AAC) {
						AVPacket packet;
						if(av_read_frame(audioFile.file, &packet) < 0) { done=true; return false; }
						if(audioFile.file->streams[packet.stream_index]==audioFile.audioStream) {
							assert_(encoder.audioStream->time_base.num == audioFile.audioStream->time_base.num);
							packet.pts=packet.dts=encoder.audioTime=
									int64(packet.pts)*encoder.audioStream->time_base.den/audioFile.audioStream->time_base.den;
							packet.duration = int64(packet.duration)*encoder.audioStream->time_base.den/audioFile.audioStream->time_base.den;
							packet.stream_index = encoder.audioStream->index;
							av_interleaved_write_frame(encoder.context, &packet);
						}
					} else if(audioFile) {
						buffer<int16> buffer(1024*audioFile.channels);
						buffer.size = audioFile.read16(buffer) * audioFile.channels;
						encoder.writeAudioFrame(buffer);
						done = buffer.size < buffer.capacity;
					}
#if SAMPLER
					else {
						buffer<int16> buffer(1024*sampler.channels);
						sampler.read16(buffer);
						encoder.writeAudioFrame(buffer);
						done = sampler.silence || encoder.audioTime*notes.ticksPerSeconds >= notes.last().time*encoder.audioFrameRate;
					}
#else
					else error("audio");
#endif
					return true;
				};
				if(encoder.videoFrameRate) { // Interleaved AV
					assert_(encoder.audioStream->time_base.num == 1 && encoder.audioStream->time_base.den == (int)encoder.audioFrameRate);
					// If both streams are at same PTS, starts with audio
					while(encoder.audioTime*encoder.videoFrameRate <= encoder.videoTime*encoder.audioFrameRate) {
						if(!writeAudio()) break;
					}
					while(encoder.videoTime*encoder.audioFrameRate < encoder.audioTime*encoder.videoFrameRate) {
						follow(encoder.videoTime, encoder.videoFrameRate, encoder.size);
						renderTime.start();
						Image target (encoder.size);
						fill(target, 0, target.size, 1, 1);
						::render(target, widget.graphics(target.size, Rect(target.size)));
						renderTime.stop();
						encodeTime.start();
						encoder.writeVideoFrame(target);
						encodeTime.stop();
					}
				} else { // Audio only
					if(!writeAudio()) break;
				}
				int percent = round(100.*encoder.audioTime/encoder.audioFrameRate/((float)notes.last().time/notes.ticksPerSeconds));
				if(percent!=lastReport) { log(str(percent,2)+"%", str(renderTime, totalTime), str(encodeTime, totalTime)); lastReport=percent; }
				{assert_(encoder.audioFrameRate == uint64(notes.ticksPerSeconds));
					auto onsets = apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.time;});
					if(encoder.audioTime > uint64(onsets.last() + notes.ticksPerSeconds)) {
						log("1s after last onset");
						break; // Cuts 1 second after last onset
					}
				}
			}
			requestTermination(0); // Window prevents automatic termination
		} else { // Preview
			window.show();
			if(playbackDeviceAvailable()) {
#if SAMPLER
				audio.start(audioFile.audioFrameRate ?: sampler.rate, sampler.periodSize, 32, 2);
				assert_(audio.rate == audioFile.audioFrameRate ?: sampler.rate);
#else
				assert_(audioFile);
				audio.start(audioFile.audioFrameRate, 1024, 32, 2);
#endif
				audioThread.spawn();
			} else running = false;
		}
    }

	int2 sizeHint(int2 size) override { return running ? widget.sizeHint(size) : scroll.ScrollArea::sizeHint(size); }
	shared<Graphics> graphics(int2 size) override {
		follow(audioFile.audioTime, audioFile.audioFrameRate, window.size);
		window.render();
		return running ? widget.graphics(size, Rect(size)) : scroll.ScrollArea::graphics(size);
	}
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
		return scroll.ScrollArea::mouseEvent(cursor, size, event, button, focus);
	}
	bool keyPress(Key key, Modifiers modifiers) override { return scroll.ScrollArea::keyPress(key, modifiers); }
} app;
