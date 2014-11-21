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

struct Peak { int time; uint key; float value; };
bool operator <(const Peak& a, const Peak& b) { return a.value > b.value; } // Decreasing order
String str(const Peak& o) { return str(o.key)/*+":"+str(int(round(o.value)))*/; }

typedef array<Peak> Bin;
String str(const Bin& o) { return /*str(o[0].time*8/48000)+*/str(o.slice(0, min(5ul, o.size))); }

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }

struct BeatSynchronizer : Widget {
	map<int, float>& measureBars;
	struct Bar { float x; bgr3f color; };
	array<Bar> bars;
	int currentTime = 0;
	BeatSynchronizer(Audio audio, MidiNotes& notes, ref<Sign> signs, map<int, float>& measureBars) : measureBars(measureBars) {
		// Converts MIDI time base to audio sample rate
		uint firstKey = 21+85, lastKey = 0;
		for(MidiNote& note: notes) {
			note.time = int64(note.time)*int64(audio.rate)/int64(notes.ticksPerSeconds);
			firstKey = min(firstKey, note.key);
			lastKey = max(lastKey, note.key);
			assert(note.time >= 0);
		}
		uint keyCount = lastKey+1-firstKey;
		notes.ticksPerSeconds = audio.rate;

		size_t T = audio.size/audio.channels; // Total sample count
		size_t N = 8192;  // Frame size: 48KHz * 2 (Nyquist) * 2 (window) / frameSize ~ 24Hz~A0
		size_t h = 2048; // Hop size: 48KHz * 60 s/min / 4 b/q / hopSize / 2 (Nyquist) ~ 175 bpm
		FFT fft(N);
		size_t frameCount = (T-N)/h;
		buffer<float> keyOnsetFunctions(keyCount*frameCount); keyOnsetFunctions.clear(0);
		buffer<float> previous = buffer<float>(N/2); previous.clear(0);
		int firstK = floor(keyToPitch(firstKey)*N/audio.rate);
		int lastK = ceil(keyToPitch(lastKey)*N/audio.rate);
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
				for(uint key: range(max(firstKey, uint(round(pitchToKey(k*audio.rate/N)))), round(pitchToKey((k+1)*audio.rate/N))))
					keyOnsetFunctions[(key-firstKey)*frameCount+frameIndex] += y;
			}
			previous = move(current);
		}

		/// Normalizes onset function
		for(size_t key: range(keyCount)) {
			mref<float> f = keyOnsetFunctions.slice(key*frameCount, frameCount);
			float SSQ = 0;
			for(float& v: f) SSQ += sq(v);
			float deviation = sqrt(SSQ / frameCount);
			if(deviation) for(float& v: f) v /= deviation;
		}

		/// Selects onset peaks
		array<Bin> P; // peaks
		int peakTimeOrigin = -1;
		const int framesPerBin = 2; // 50ms
		for(size_t binIndex: range(1, frameCount/framesPerBin-1)) {
			array<Peak> chord;
			for(size_t subIndex: range(framesPerBin)) {
				size_t i = binIndex * framesPerBin + subIndex;
				for(uint key: range(keyCount)) {
					ref<float> f = keyOnsetFunctions.slice(key*frameCount, frameCount);
					if(f[i] > 50./127 &&/*over global threshold*/ f[i-1] < f[i] && f[i] > f[i+1] /*Local maximum*/) {
						if(peakTimeOrigin == -1) peakTimeOrigin = i*h;
						chord.insertSorted(Peak{int(i*h)-peakTimeOrigin, firstKey+key, f[i]*127});
					}
				}
			}
			if(chord) P.append(move(chord));
		}
		log(P.slice(0,32));

		/// Collects MIDI chords following MusicXML at the same time to cluster (assumes performance is correctly ordered)
		array<Bin> S; // MIDI
		int scoreTimeOrigin = -1;
		buffer<MidiNote> onsets = filter(notes, [](MidiNote o){return o.velocity==0;});
		assert_(signs.size == onsets.size);
		for(size_t index = 0; index < signs.size;) {
			array<Peak> chord;
			while(index < signs.size && signs[index].type == Sign::Invalid) index++;
			assert_(signs[index].type == Sign::Note, index, signs.size, (int)signs[index].type);
			int64 time = signs[index].time;
			while(index < signs.size && signs[index].time == time) {
				if(scoreTimeOrigin == -1) scoreTimeOrigin = onsets[index].time;
				if(chord) assert_(onsets[index].time-scoreTimeOrigin-chord[0].time < 24000, onsets[index].time-chord[0].time);
				chord.append(Peak{int(onsets[index].time)-scoreTimeOrigin, onsets[index].key, (float)onsets[index].velocity});
				index++;
				while(index < signs.size && signs[index].type == Sign::Invalid) index++;
			}
			S.append(move(chord));
		}
		log(S.slice(0,32));

		/// Synchronizes audio and score using dynamic time warping
		size_t m = S.size, n = P.size;
		assert_(m < n);

		// Evaluates cumulative score matrix at each alignment point (i, j)
		struct Matrix {
			size_t m, n;
			buffer<float> elements;
			Matrix(size_t m, size_t n) : m(m), n(n), elements(m*n) { elements.clear(0); }
			float& operator()(size_t i, size_t j) { return elements[i*n+j]; }
		} D(m,n);
		// Reversed scan here to have the forward scan when walking back the best path
		for(size_t i: reverse_range(m)) for(size_t j: reverse_range(n)) {
			// Evaluates match (i,j)
			float d = 0;
			//TODO: evaluate peak harmonic energy
			for(Peak s: S[i]) for(Peak p: P[j]) d += (
						s.key==p.key-1 || s.key==p.key || s.key==p.key+1 ||
						s.key==p.key-12-1 || s.key==p.key-12 || s.key==p.key-12+1 ||
						s.key==p.key-12-7) * p.value;
			// Evaluates best cumulative score to an alignment point (i,j)
			D(i,j) = max((float[]){
							 j+1==n?0:D(i,j+1), // Ignores peak j
							 i+1==m?0:D(i+1, j), // Ignores chord i
							 ((i+1==m||j+1==n)?0:D(i+1,j+1)) + d // Matches chord i with peak j
						 });
		};

		// Evaluates _strictly_ monotonous map by walking back the best path on the cumulative score matrix
		int uniformOffset = peakTimeOrigin - scoreTimeOrigin;
		int globalOffset = 0;
		// Forward scan (chronologic for clearer midi time correlation logic)
		size_t i = 0, j = 0; // Starts from anchor alignment (0, 0)
		size_t midiIndex = 0;
		while(i<m && j<n) {
			//log(cumulativeOffset - (P[0][0].time - S[0][0].time));
			//float globalOffsetConstraint =  (P[0][0].time - S[0][0].time) + notes.ticksPerSeconds; // Allows up to 1 second global offset
			int localOffset = (P[j][0].time - S[i][0].time) - globalOffset; // >0: late, <0: early
			log(globalOffset/float(notes.ticksPerSeconds), localOffset/float(notes.ticksPerSeconds));
			//float instantanousOffsetConstraint = 0; // Constrains instantanous offset to obtain a reasonnable alignment even when peaks match badly
			if(i+1<m && D(i,j) == D(i+1,j) /*&& instantanousOffset < -offsetConstraint*/) { //(Skip chords when we are early). Step to (i+1, j)
				if(i+1<m && j+1<n) assert_(D(i+1, j) >= D(i, j+1) && D(i+1, j) >= D(i+1, j+1)); // Globally best step is to ignore chord i
				i++; // Ignores chord i
				int onsetChordTime = notes[midiIndex].time;
				for(;midiIndex < notes.size && (notes[midiIndex].velocity==0 || notes[midiIndex].time == onsetChordTime); midiIndex++)
					notes[midiIndex].time += uniformOffset + globalOffset; // Shifts all events until next chord, by the offset of last match
			} else // There are enough peaks to match instead (but need to be able to skip tremolos
			 //(Prevents skip when we are already late)
			/*else*/
			if(j+1<n &&
					((D(i,j) == D(i,j+1) && globalOffset < 8*notes.ticksPerSeconds && localOffset < notes.ticksPerSeconds/2)
					/*|| globalOffset < -notes.ticksPerSeconds*/ || localOffset < -notes.ticksPerSeconds/4)
					) {
				//if(i+1<m && j+1<n) assert_(D(i, j+1) >= D(i+1,j) && D(i+1,j) >= D(i+1,j+1)); // Globally best step is to ignore peak j
				j++; // Ignores peak j
			} else { // Match (step to (i+1, j+1))
				globalOffset = P[j][0].time - S[i][0].time;
				int onsetChordTime = notes[midiIndex].time;
				for(;midiIndex < notes.size && (notes[midiIndex].velocity==0 || notes[midiIndex].time == onsetChordTime); midiIndex++)
					notes[midiIndex].time += uniformOffset + globalOffset; // Shifts all events until next chord
				// Strict map (no multiple match)
				i++; j++;
			}
		}
		//assert_(i==m && j==n, i,j, m,n); // Ends with anchor aligment (m, n)
		log(m, n);

		{ // Set measure times to MIDI times
			buffer<MidiNote> onsets = filter(notes, [](MidiNote o){return o.velocity==0;});
			assert_(onsets.size == signs.size);
			size_t index = 0;
			for(size_t measureIndex: range(measureBars.size())) {
				while(index < signs.size) {
					if(signs[index].note.measureIndex==measureIndex) break;
					index++;
				}
				measureBars.keys[measureIndex] = onsets[index].time;
			}
		}

		// MIDI visualization (synchronized to sheet time)
		{size_t midiIndex = 0;
			auto onsets = apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.time;});
			for(int index: range(measureBars.size()-1)) {
				int t0 = measureBars.keys[index];
				float x0 = measureBars.values[index];
				int t1 = measureBars.keys[index+1];
				float x1 = measureBars.values[index+1];

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
			for(int index: range(measureBars.size()-1)) {
				int t0 = measureBars.keys[index];
				float x0 = measureBars.values[index];
				int t1 = measureBars.keys[index+1];
				float x1 = measureBars.values[index+1];
				while(peakIndex<P.size && t0 <= peakTimeOrigin+P[peakIndex][0].time && peakTimeOrigin+P[peakIndex][0].time < t1) {
					assert_(t1>t0);
					float x = x0+(x1-x0)*(peakTimeOrigin+P[peakIndex][0].time-t0)/(t1-t0);
					bars.append(Bar{x, red});
					peakIndex++;
				}
			}
		}

		for(float x: measureBars.values) bars.append(Bar{x, green});
	}
	int2 sizeHint(int2) override { return int2(measureBars.values.last(), 32); }
	shared<Graphics> graphics(int2 size) override {
		assert_(size.x > 1050, size, sizeHint(size));
		shared<Graphics> graphics;
		for(Bar bar: bars) graphics->fills.append(vec2(bar.x, 0), vec2(2, size.y), bar.color, 1.f/2);
		for(int index: range(measureBars.size()-1)) {
			int t0 = measureBars.keys[index];
			float x0 = measureBars.values[index];
			int t1 = measureBars.keys[index+1];
			float x1 = measureBars.values[index+1];
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
	// Name
	string name = arguments() ? arguments()[0] : (error("Expected name"), string());
	// Files
	buffer<String> audioFiles = filter(Folder(".").list(Files), [this](string path) { return !startsWith(path, name)
			|| (!endsWith(path, ".mp3") && !endsWith(path, ".m4a") && !endsWith(path, "performance.mp4") && !find(path, ".mkv")); });
	buffer<String> videoFiles = filter(Folder(".").list(Files), [this](string path) {
			return !startsWith(path, name) ||  (!endsWith(path, "performance.mp4") && !find(path, ".mkv")); });

	// MusicXML
    MusicXML xml = readFile(name+".xml"_);
	// MIDI
	MidiNotes notes = existsFile(name+".mid"_) ? MidiFile(readFile(name+".mid"_)) : ::notes(xml.signs, xml.divisions);
	// Audio
	AudioFile audioFile = audioFiles ? AudioFile(audioFiles[0]) : AudioFile();
	// Sheet
	Sheet sheet {xml.signs, xml.divisions, apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.key;})};
	BeatSynchronizer beatSynchronizer {decodeAudio(audioFiles[0]), notes, sheet.midiToSign, sheet.measureBars};
	// Video
	Decoder video = videoFiles ? Decoder(videoFiles[0]) : Decoder();

	// State
	bool failed = sheet.firstSynchronizationFailureChordIndex != invalid;
	bool running = true; //!failed;
	bool rotate = videoFiles ? endsWith(videoFiles[0], ".mkv") : false;

	// View
	Scroll<VBox> scroll {{&sheet, &beatSynchronizer}};
	ImageView videoView = video ? video.size : Image();
	Keyboard keyboard;
	VBox widget {{&scroll, &videoView, &keyboard}};

	// Highlighting
	map<uint, Sign> active; // Maps active keys to notes (indices)
	uint midiIndex = 0, noteIndex = 0;

	// Video preview
	Window window {this, int2(1280,720), [](){return "MusicXML"__;}};

	// Audio preview
    Thread audioThread;
	AudioOutput audio = {{this, &Music::read32}, audioThread};

	// Sampler
#if SAMPLER
	Thread decodeThread;
	Sampler sampler {48000, "/Samples/Salamander.sfz"_, {this, &Music::timeChanged}, decodeThread};
#endif

	// End
	buffer<int64> onsets = apply(filter(notes, [](MidiNote o){return o.velocity==0;}), [](MidiNote o){return o.time;});

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
					if(sign.note.measureIndex != invalid && sign.note.glyphIndex != invalid) {
						sheet.measures.values[sign.note.measureIndex]->glyphs[sign.note.glyphIndex].color = (sign.staff?red:green);
						contentChanged = true;
					}
				}
				noteIndex++;
			}
			else if(!note.velocity && active.contains(note.key)) {
				while(active.contains(note.key)) {
					Sign sign = active.take(note.key);
					(sign.staff?keyboard.left:keyboard.right).remove(sign.note.key);
					if(sign.note.measureIndex != invalid && sign.note.glyphIndex != invalid) {
						sheet.measures.values[sign.note.measureIndex]->glyphs[sign.note.glyphIndex].color = black;
					}
					contentChanged = true;
				}
			}
		}
		uint64 t = timeNum*notes.ticksPerSeconds;
		int previousOffset = scroll.offset.x;
		// Cardinal cubic B-Spline
		for(int index: range(sheet.measureBars.size()-1)) {
			uint64 t1 = sheet.measureBars.keys[index]*timeDen;
			uint64 t2 = sheet.measureBars.keys[index+1]*timeDen;
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
		beatSynchronizer.currentTime = timeNum*notes.ticksPerSeconds/timeDen;
		if(video && (!videoView.image || (video.videoTime*timeDen <= timeNum*video.videoFrameRate
				&& video.videoTime*notes.ticksPerSeconds < (onsets.last() + notes.ticksPerSeconds)*video.videoFrameRate))) {
			if(video.read(videoView.image)) {
				if(rotate) ::rotate(videoView.image);
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
		}
#if SAMPLER
		if(sampler) decodeThread.spawn(); // For sampler
#endif
		if(arguments().contains("encode")) { // Encode
			Encoder encoder {name};
			encoder.setVideo(int2(1280,720), 60);
			if(audioFile && audioFile.codec==AudioFile::AAC) encoder.setAudio(audioFile);
			else if(audioFile) encoder.setAAC(audioFile.channels, audioFile.audioFrameRate);
#if SAMPLER
			else encoder.setAudio(sampler.rate);
#else
			else error("audio");
#endif
			encoder.open();
			Time renderTime, encodeTime, totalTime;
			totalTime.start();
			int64 startTime = max(0ll, notes[0].time - notes.ticksPerSeconds);
			if(startTime > 0) seek(startTime);
			bool done = 0;
			for(int lastReport=0; !done;) {
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
						/*if(audioFile.channels == 2 && encoder.channels == 1) {
							for(size_t i: range(buffer.size)) buffer[i] = (int(buffer[i*2+0])+int(buffer[i*2+1]))/2;
							buffer.size /= 2;
						} else*/ assert_(audioFile.channels == encoder.channels);
						if(buffer.size) encoder.writeAudioFrame(buffer);
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
					return !done;
				};
				if(encoder.videoFrameRate) { // Interleaved AV
					assert_(encoder.audioStream->time_base.num == 1 && encoder.audioStream->time_base.den == (int)encoder.audioFrameRate);
					// If both streams are at same PTS, starts with audio
					while(encoder.audioTime*encoder.videoFrameRate <= encoder.videoTime*encoder.audioFrameRate) {
						if(!writeAudio()) break;
					}
					while(encoder.videoTime*encoder.audioFrameRate < encoder.audioTime*encoder.videoFrameRate) {
						follow(startTime+encoder.videoTime*notes.ticksPerSeconds/encoder.videoFrameRate, notes.ticksPerSeconds, encoder.size);
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
				if(startTime+encoder.audioTime >= uint64(onsets.last() + notes.ticksPerSeconds)) break; // Cuts 1 second after last onset
			}
			requestTermination(0); // Window prevents automatic termination
		} else { // Preview
			if(running) { // Starts one second before first onset
				if(notes[0].time > notes.ticksPerSeconds) seek(notes[0].time-notes.ticksPerSeconds);
			}
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
