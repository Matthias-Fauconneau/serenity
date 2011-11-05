#include "music.h"
#include "process.h"
#include "file.h"
#include "interface.h"
#include "document.h"

struct Music : Application {
	AudioOutput audio;
	Sampler sampler;
	Sequencer seq;
	MidiFile midi;
	PDF sheet;
	//Score score;
	map<int, int> notes; //[midiIndex] = note, indexOf(midiIndex) = scoreIndex
	~Music() { sampler.sync(); seq.sync(); }

	Window window = Window(int2(640,640), sheet);

	void start(array<string> &&arguments) {
		sheet.needUpdate.connect(this,&Music::update);

		for(auto&& path : arguments) {
			if(path.endsWith(_(".sfz")) && exists(path)) {
				sampler.open(path);
				seq.noteEvent.connect(&sampler,&Sampler::event);
				audio.setInput(&sampler);
			} else if(path.endsWith(_(".mid"))) {
				if(exists(path)) {
					midi.open(path);
					sampler.timeChanged.connect(&midi,&MidiFile::update);
					midi.noteEvent.connect(&sampler,&Sampler::event);
				} else {
					seq.recordMID(path);
				}
			} else if(path.endsWith(_(".pdf")) && exists(path)) {
				//sheet.onGlyph.connect(&score,&Score::onGlyph);
				//sheet.onPath.connect(&score,&Score::onPath);
				sheet.open(path);
				window.rename(section(section(path,'/',-2,-1),'.',0,-2));
				//score->synchronize(midi->notes);
				/*map<int, map<int, int> > sort; //[chronologic][bass to treble order] = index
				for(int i=0;i<events.count();i++) {
					MidiEvent e = events[i];
					if(e.type==NoteOn) sort[e.tick].insertMulti(e.note,i);
				}
				foreach(int tick,sort.keys()) foreach(int note,sort[tick].keys()) {
					toScore[sort[tick][note]]=notes.count(); notes << events[sort[tick][note]].note;
				}*/
			} /*else if(path.endsWith(_(".wav")) && !exists(path) && sampler) {
				sampler->recordWAV(path);
			}*/
		}
		//if(!sampler && !sheet) fail("Usage: music [instrument.sfz] [music.mid] [sheet.pdf] [output.wav]");
		//sheet.scroll=1600;
		update();
		if(audio.input) audio.start();
	}
	void update() {
		window.render();
	}
} music;
