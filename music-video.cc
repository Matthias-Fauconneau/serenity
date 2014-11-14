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
#include "decoder.h"
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
	if(!notes.ticksPerSeconds) notes.ticksPerSeconds = 120*ticksPerQuarter;
	assert_(notes.ticksPerSeconds);
	return notes;
}

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
struct BeatSynchronizer : Widget {
	buffer<float> signal;
	int64 rate;
	Sheet& sheet;
	MidiNotes& notes;
	buffer<Sign> signs = filter(sheet.midiToSign,[](Sign sign){return sign.type!=Sign::Note;});
	struct Bar { float x; bgr3f color; };
	array<Bar> bars;
	size_t time = 0;
	BeatSynchronizer(ref<float2> stereo, int64 rate, Sheet& sheet, MidiNotes& notes) : signal(stereo.size), rate(rate), sheet(sheet), notes(notes) {
		// Converts MIDI time base to audio sample rate
		for(MidiNote& note: notes) note.time = note.time*rate/notes.ticksPerSeconds;
		notes.ticksPerSeconds = rate;

		// Setups signal spectral processing
		assert_(stereo);
		for(size_t index: range(signal.size)) {
			signal[index] = (stereo[index][0]+stereo[index][1])/2;
			assert_(signal[index]>=-1 && signal[index]<=1, signal[index]);
		}
		size_t T = signal.size; // Total sample count
		size_t N = 2048; // Frame size (43ms @48KHz)
		size_t h = N/4; // Hop size (11ms @48KHz (75% overlap))
		FFT fft(N);
		assert_(T>N, T);
		size_t frameCount = (T-N)/h;
#if 0
		buffer<float> previous;
		// Synchronizes MIDI with audio by matching expected fundamentals onsets (TODO: use pitch estimator candidates, offsets)
		map<uint, size_t> expected;
		size_t firstMidiIndex = 0, nextMidiIndex = 0;
		int64 midiTime = 0; uint64 firstMatchAudioTimeShift = 0;
		for(size_t frameIndex: range(frameCount)) {
			while(nextMidiIndex < notes.size && !expected) {
				// Shifts next notes using last match
				if(firstMatchAudioTimeShift) {
					for(MidiNote& note: notes.slice(firstMidiIndex)) note.time += firstMatchAudioTimeShift;
					firstMatchAudioTimeShift = 0;
				}
				// Prepares next expectation
				firstMidiIndex = nextMidiIndex;
				midiTime = notes[nextMidiIndex].time;
				while(nextMidiIndex < notes.size && notes[nextMidiIndex].time == midiTime) {
					if(notes[nextMidiIndex].velocity) expected.insert(notes[nextMidiIndex].key, nextMidiIndex);
					nextMidiIndex++;
				}
			}

			size_t audioTime = frameIndex * h;
			for(size_t i: range(N)) {
				fft.windowed[i] = fft.window[i] * signal[audioTime+i];
			}
			fft.spectrum = buffer<float>(N/2);
			fft.transform();
			buffer<float> current = move(fft.spectrum);
			if(previous) {
				expected.filter([&](uint key, size_t index) {
					uint f0 = round(keyToPitch(key)*N/rate);
					assert_(f0 < N/2);
					int64 expectedTime = notes[index].time;
					float distance = (expectedTime-int64(audioTime))/float(rate);
					if(current[f0] > previous[f0]/max(1.f,abs(distance)) && current[f0] > fft.periodEnergy/(10*max(1.f,abs(distance)))) {
						log(expectedTime/float(rate), audioTime/float(rate), distance, "got", key, current[f0]/previous[f0], current[f0]/fft.periodEnergy);
						if(!firstMatchAudioTimeShift) firstMatchAudioTimeShift = audioTime - notes[index].time;
						return true;
					}
					return false;
				});
			}
			previous = move(current);
		}
#else
		buffer<double> f(frameCount); // Spectral flux onset function
		buffer<float> previous = buffer<float>(N/2); previous.clear(0);
		double sum = 0;
		for(size_t frameIndex: range(frameCount)) {
			size_t t0 = frameIndex * h;
			for(size_t i: range(N)) {
				fft.windowed[i] = fft.window[i] * signal[t0+i];
			}
			fft.spectrum = buffer<float>(N/2);
			fft.transform();
			buffer<float> current = move(fft.spectrum);
			double spectralFlux = 0;
			for(size_t k: range(N/2)) {
				float x = current[k] - previous[k]; // or L1: sqrt(current[k]) - sqrt(previous[k])
				if(x<0) x=0; // "Half-wave rectifier"
				spectralFlux += x;
			}
			f[frameIndex] = spectralFlux;
			sum += spectralFlux;
			previous = move(current);
		}

		/// Normalizes onset function
		double mean = sum/frameCount;
		double SSQ = 0;
		for(double& v: f) { v -= mean; SSQ += sq(v); }
		double deviation = sqrt(SSQ);
		for(double& v: f) v /= deviation;

		/// Selects onset peaks (beats)
		array<int64> beats;
		const int w = 3, m = 3;
		for(size_t n: range(m*w, frameCount-w)) {
			bool localMaximum = true;
			for(size_t k: range(n-w, n+w+1)) {
				if(k==n) continue;
				if(!(f[n] > f[k])) localMaximum=false;
			}
			if(localMaximum) {
				double sum = 0;
				for(size_t k: range(n-m*w, n+w+1)) sum += f[k];
				double mean = sum / (m*w+w+1);
				const double threshold = beats ? 1./1024 : 1./2048;
				if(f[n] > mean + threshold) beats.append( n * h );
			}
		}

		// Synchronizes MIDI with audio by moving each onset to nearest onset
		{// Inconditionnaly sync first note with first beat
			MidiNote note = notes[0];
			int64 current = note.time;
			int64 offset = beats[0] - current;
			for(MidiNote& note: notes) note.time += offset;
		}
#endif

		assert_(sheet.ticksPerMinutes);
		{ // Synchronizes measure times with MIDI
			size_t midiIndex = 0, noteIndex = 0;
			for(size_t measureIndex: range(sheet.measureBars.size())) {
				while(noteIndex < sheet.midiToSign.size) {
					Sign sign = sheet.midiToSign[noteIndex];
					if(sign.type == Sign::Note && sign.note.measureIndex==measureIndex) break;
					midiIndex++;
					while(!notes[midiIndex].velocity) midiIndex++;
					noteIndex++;
				}
				if(midiIndex < notes.size) {
					sheet.measureBars.keys[measureIndex] = notes[midiIndex].time*sheet.ticksPerMinutes/(60*notes.ticksPerSeconds);
				}
			}
		}

		// MIDI visualization (synchronized to sheet time)
		{
			size_t midiIndex = 0;
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

		/*// Beat visualization (synchronized to sheet time)
		{
			size_t beatIndex = 0;
			for(int index: range(sheet.measureBars.size()-1)) {
				int64 t0 = (int64)rate*sheet.measureBars.keys[index]*60/sheet.ticksPerMinutes;
				float x0 = sheet.measureBars.values[index];
				int64 t1 = (int64)rate*sheet.measureBars.keys[index+1]*60/sheet.ticksPerMinutes;
				float x1 = sheet.measureBars.values[index+1];
				while(beatIndex<beats.size && t0 <= beats[beatIndex] && beats[beatIndex] < t1) {
					assert_(t1>t0);
					float x = x0+(x1-x0)*(beats[beatIndex]-t0)/(t1-t0);
					bars.append(Bar{x, red});
					beatIndex++;
				}
			}
		}*/

		for(float x: sheet.measureBars.values) bars.append(Bar{x, green});
	}
	int2 sizeHint(int2 size) override { return int2(sheet.sizeHint(size).x, 32); }
	shared<Graphics> graphics(int2 size) override {
		assert_(size.x > 1050, size, sizeHint(size));
		shared<Graphics> graphics;
		for(Bar bar: bars) graphics->fills.append(vec2(bar.x, 0), vec2(2, size.y), bar.color, 1.f/2);
		for(int index: range(sheet.measureBars.size()-1)) {
			uint64 t0 = (int64)rate*sheet.measureBars.keys[index]*60/sheet.ticksPerMinutes;
			float x0 = sheet.measureBars.values[index];
			uint64 t1 = (int64)rate*sheet.measureBars.keys[index+1]*60/sheet.ticksPerMinutes;
			float x1 = sheet.measureBars.values[index+1];
			if(t0 <= time && time < t1) {
				assert_(t1>t0);
				float x = x0+(x1-x0)*(time-t0)/(t1-t0);
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
	buffer<String> audioFiles = filter(Folder(".").list(Files), [this](string path) {
			return !startsWith(path, name) || (!endsWith(path, ".mp3") && !endsWith(path, ".m4a") && !endsWith(path, "performance.mp4") && !endsWith(path, ".cut.mp4")); });
	AudioFile audioFile = audioFiles ? AudioFile(audioFiles[0]) : AudioFile();

	// Audio data
	Audio audioData = decodeAudio(audioFiles[0]); // Decodes full file for analysis (beat detection and synchronization)

	// Video input
	buffer<String> videoFiles = filter(Folder(".").list(Files), [this](string path) {
			return !startsWith(path, name) ||  (!endsWith(path, "performance.mp4") && !endsWith(path, ".cut.mp4")); });
	Decoder video = videoFiles ? Decoder(videoFiles[0]) : Decoder();
	ImageView videoView = video ? video.size : Image();

	// Rendering
	Sheet sheet {xml.signs, xml.divisions, apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;})};
	BeatSynchronizer beatSynchronizer {audioData, audioData.rate, sheet, notes};
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
	Thread decodeThread;
	Sampler sampler {48000, "/Samples/Salamander.sfz"_, {this, &Music::timeChanged}, decodeThread};

	size_t read32(mref<int2> output) {
		assert_(audioFile && audioFile.audioTime>=0);
		if(follow(audioFile.audioTime, audioFile.audioFrameRate, window.size)) window.render();
		return audioFile.read32(output);
	}

	/// Adds new notes to be played (called in audio thread by sampler)
	uint samplerMidiIndex = 0;
	void timeChanged(uint64 time) {
		while(samplerMidiIndex < notes.size && notes[samplerMidiIndex].time*sampler.rate <= time*notes.ticksPerSeconds) {
			auto note = notes[samplerMidiIndex];
			sampler.noteEvent(note.key, note.velocity);
			samplerMidiIndex++;
		}
	}

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
				//log(index, t1/float(sheet.ticksPerMinutes*timeDen), t/float(sheet.ticksPerMinutes*timeDen), t2/float(sheet.ticksPerMinutes*timeDen));
				real f = real(t-t1)/real(t2-t1);
				real w[4] = { 1./6 * cb(1-f), 2./3 - 1./2 * sq(f)*(2-f), 2./3 - 1./2 * sq(1-f)*(2-(1-f)), 1./6 * cb(f) };
				auto X = [&](int index) { return clip(0.f, sheet.measureBars.values[clip<int>(0, index, sheet.measureBars.values.size)] - size.x/2, (float)abs(sheet.sizeHint(size).x)-size.x); };
				//log(X(index), w[0]*X(index-1) + w[1]*X(index) + w[2]*X(index+1) + w[3]*X(index+2), X(index+1));
				scroll.offset.x = -round( w[0]*X(index-1) + w[1]*X(index) + w[2]*X(index+1) + w[3]*X(index+2) );
				break;
			}
		}
		if(previousOffset != scroll.offset.x) contentChanged = true;
		beatSynchronizer.time = timeNum*beatSynchronizer.rate/timeDen;
		if(video && video.videoTime*timeDen < timeNum*video.videoFrameRate) {
			video.read(videoView.image);
			rotate(videoView.image);
			contentChanged=true;
		}
		return contentChanged;
	}

	void seek(uint64 time) {
		/*if(audio) {
			audioTime = midiTime * audioFile.rate / notes.ticksPerSeconds;
			audioFile.seek(audioTime); //FIXME: return actual frame time
			videoTime = audioTime * window.framesPerSecond / audioFile.rate; // FIXME: remainder
		} else {
			assert_(notes.ticksPerSeconds);
			videoTime = midiTime * window.framesPerSecond / notes.ticksPerSeconds;
			previousFrameCounterValue=window.currentFrameCounterValue;
			follow(videoTime, window.framesPerSecond);
		}*/
		sampler.time = time*sampler.rate/notes.ticksPerSeconds;
		while(samplerMidiIndex < notes.size && notes[samplerMidiIndex].time*sampler.rate < time*notes.ticksPerSeconds) samplerMidiIndex++;
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
		} else if(running) { // Seeks to first note
			seek( sheet.midiToSign[0].time );
		}
		if(!audioFile) decodeThread.spawn(); // For sampler
		if(arguments().contains("encode")) { // Encode
			Encoder encoder {name};
			encoder.setVideo(int2(1280,720), 60);
			if(audioFile) encoder.setAudio(audioFile);
			else encoder.setAudio(sampler.rate);
			encoder.open();
			Time renderTime, encodeTime, totalTime;
			totalTime.start();
			for(int lastReport=0, done=0; !done;) {
				assert_(encoder.audioStream->time_base.num == 1);
				auto writeAudio = [&]{
					if(audioFile) {
						AVPacket packet;
						if(av_read_frame(audioFile.file, &packet) < 0) { done=true; return false; }
						if(audioFile.file->streams[packet.stream_index]==audioFile.audioStream) {
							assert_(encoder.audioStream->codec->time_base.den == audioFile.audioStream->codec->time_base.den);
							packet.pts=packet.dts=encoder.audioTime= int64(packet.pts); //*encoder.audioStream->codec->time_base.den/audioFile.audioStream->codec->time_base.den;
							packet.duration = int64(packet.duration)*encoder.audioStream->time_base.den/audioFile.audioStream->time_base.den;
							packet.stream_index = encoder.audioStream->index;
							av_interleaved_write_frame(encoder.context, &packet);
						}
					} else {
						byte buffer_[sampler.periodSize*sizeof(short2)];
						mref<short2> buffer((short2*)buffer_, sampler.periodSize);
						sampler.read16(buffer);
						encoder.writeAudioFrame(buffer);
						done = sampler.silence || encoder.audioTime*notes.ticksPerSeconds >= notes.last().time*encoder.audioFrameRate;
					}
					return true;
				};
				if(encoder.videoFrameRate) { // Interleaved AV
					assert_(encoder.audioStream->time_base.den == (int)encoder.audioFrameRate);
					while(encoder.audioTime*encoder.videoFrameRate <= encoder.videoTime*encoder.audioFrameRate) { // If both streams are at same PTS, starts with audio
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
				//if(percent==5) break; // DEBUG
			}
			requestTermination(0); // Window prevents automatic termination
		} else { // Preview
			window.show();
			if(playbackDeviceAvailable()) {
				audio.start(audioFile.audioFrameRate ?: sampler.rate, sampler.periodSize, /*audioFile ? 16 :*/ 32);
				assert_(audio.rate == audioFile.audioFrameRate ?: sampler.rate);
				audioThread.spawn();
			} else running = false;
		}
    }

	int2 sizeHint(int2 size) override { return running ? widget.sizeHint(size) : scroll.ScrollArea::sizeHint(size); }
	shared<Graphics> graphics(int2 size) override { return running ? widget.graphics(size, Rect(size)) : scroll.ScrollArea::graphics(size); }
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) {
		return scroll.ScrollArea::mouseEvent(cursor, size, event, button, focus);
	}
	bool keyPress(Key key, Modifiers modifiers) override { return scroll.ScrollArea::keyPress(key, modifiers); }
} app;
