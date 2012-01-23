#include "string.h"
#include "file.h"
#include "music.h"
#include <sys/mman.h>
#include <unistd.h>
#include "math.h"

void Sampler::open(const string& path) {
	// parse sfz and mmap samples
	Sample group;
	int start=::time();
	string folder = section(path,'/',0,-2); folder.size++;
	TextStream s = mapFile(path);
	Sample* sample=0;
	for(;;) {
		while(*s==' '||*s=='\n'||*s=='\r') s++;
		if(*s == 0) break;
		if(s.match(_("<group>"))) { group=Sample(); sample = &group; }
		else if(s.match(_("<region>"))) { samples<<group; sample = &samples.last();  }
		else if(s.match(_("//"))) {
			while(*s && *s!='\n' && *s!='\r') s++;
			while(*s=='\n' || *s=='\r') s++;
		}
		else {
			string key = section(s,'='); s+=key.size; s++;/*=*/
			const char* v=s; while(*s!=' '&&*s!='\n'&&*s!='\r') s++; string value(v,s-v);
			if(key==_("sample")) {
				if(::time()-start > 1000) { start=::time(); log("Loading...",samples.size); }
				string path = folder+value.replace('\\','/')+string("\0",1);
				auto file = mapFile(path).slice(44);
				sample->data = (uint8*)file.data; sample->size=file.size;
				mlock(sample->data, min(64*1024,sample->size)); //TODO: monolithic file for sequential load
			}
			else if(key==_("trigger")) sample->trigger = value==_("release");
			else if(key==_("lovel")) sample->lovel=toInteger(value);
			else if(key==_("hivel")) sample->hivel=toInteger(value);
			else if(key==_("lokey")) sample->lokey=toInteger(value);
			else if(key==_("hikey")) sample->hikey=toInteger(value);
			else if(key==_("pitch_keycenter")) sample->pitch_keycenter=toInteger(value);
			else if(key==_("ampeg_release")) sample->releaseTime=(48000*toInteger(value))/1024*1024;
			else if(key==_("amp_veltrack")) sample->amp_veltrack=toInteger(value);
			else if(key==_("rt_decay")) sample->rt_decay=toInteger(value);
			else if(key==_("volume")) sample->volume=exp10(toInteger(value)/20.0);
            else error("unknown opcode",key);
		}
	}

	// setup pitch shifting
	buffer = new float[period*2];
	for(int i=0;i<3;i++) { Layer& l=layers[i];
		l.size = round(period*exp2((i-1)/12.0));
		l.buffer = new float[l.size*2];
		if(l.size!=period) new (&l.resampler) Resampler(2, l.size, period);
	}
}

void Sampler::event(int key, int vel) {
	Note* released=0;
	int trigger=0;
	if(vel==0) {
		for(Note& note : active) {
			if(note.key==key) {
				note.end = note.begin + 6*note.sample->releaseTime;
				trigger=1; released=&note; vel = note.vel; //for release sample
			}
		}
	}
	for(const Sample& s : samples) {
		if(trigger == s.trigger && key >= s.lokey && key <= s.hikey && vel >= s.lovel && vel <= s.hivel) {
			int shift = 1+key-s.pitch_keycenter;
			assert(shift>=0 && shift<3,"TODO: pitch shift > 1");
			float level = float(vel*vel)/(127*127);
			level = 1-(s.amp_veltrack/100.0*(1-level));
			level *= s.volume;
			if(released) {
				float noteLength = float(released->end - released->sample->data)/6/48000;
				float attenuation = exp10(-s.rt_decay * noteLength / 20);
				level *= attenuation;
			}
			Note note = { &s, s.data, s.data + s.size + 6*s.releaseTime, key, vel, shift, level };
			active << note;
			break; //assume only one match
		}
	}
}

void Sampler::setup(const AudioFormat&) {}

void Sampler::read(int16 *output, int period) {
	assert(period==layers[1].size,"period != 1024");
	timeChanged.emit(time);
	for(Layer& layer : layers) clear(layer.buffer,layer.size*2);
	for(int i=0;i<active.size;i++) { Note& n = active[i];
		const int frame = layers[n.shift].size;
		float* d = layers[n.shift].buffer;
		const uint8* &s = n.begin;
#define L float(*(int*)(s-1)>>8) //TODO: fast 24bit to float
#define R float(*(int*)(s+2)>>8)
		float level = n.level;
		int release = (n.end-s)/6;
		int sustain = release - n.sample->releaseTime;
		int length = ((n.sample->data+n.sample->size)-s)/6;
		if( sustain >= frame && length >= frame ) {
			sustain = min(sustain,length);
			for(const float* end=d+frame*2;d<end;d+=2,s+=6) {
				d[0] += level*L;
				d[1] += level*R;
			}
		} else if( release >= frame && length >= frame ) {
			float reciprocal = level/n.sample->releaseTime;
			const float* end=d+frame*2;
			for(int i=release;d<end;d+=2,s+=6,i--) {
				d[0] += (reciprocal * i) * L;
				d[1] += (reciprocal * i) * R;
			}
		} else { active.removeAt(i); i--; }
	}
	for(Layer& layer : layers) { if(!layer.resampler) continue;
		int in = layer.size, out = period;
		layer.resampler.filter(layer.buffer,&in,buffer,&out);
		for(int i=0;i<period*2;i++) layers[1].buffer[i] += buffer[i];
	}
	for(int i=0;i<period*2;i++) output[i] = clip(-32768,int(layers[1].buffer[i])>>10,32767);
	if(record) write(record,output,period*2*sizeof(int16));
	time+=period;
}

void Sampler::recordWAV(const string& path) {
	record = createFile(path);
	struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
		int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=48000; int32 bps=48000*4;
		int16 stride=4; int16 bitdepth=16; char data[4]={'d','a','t','a'}; /*size?*/ } __attribute__ ((packed)) header;
	write(record,header);
}

void Sampler::sync() {
	if(!record) return;
	lseek(record,4,SEEK_SET); write<int32>(record,36+time); lseek(record,0,SEEK_END);
}
