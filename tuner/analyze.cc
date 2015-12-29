#include "thread.h"
//include "pitch.h"
#include "sampler.h"
#include "math.h"
#include "time.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "png.h"

bool operator ==(range a, range b) { return a.start==b.start && a.stop==b.stop; }

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }
inline float loudnessWeight(float f) {
 const float a = sq(20.6), b=sq(107.7), c=sq(737.9), d=sq(12200);
 float f2 = f*f;
 return d*f2*f2 / ((f2 + a) * sqrt((f2+b)*(f2+c)) * (f2+d));
}

struct Sampler::Sample {
 String name;
 Map data; FLAC flac; array<float> envelope; //Sample data
 int trigger=0; uint lovel=0; uint hivel=127; uint lokey=0; uint hikey=127; uint locc64=0, hicc64=127; uint random=0; //Input controls
 uint pitch_keycenter=-1;/*60;*/ float releaseTime=1; float amp_veltrack=1; float volume=1; //Performance parameters
 int rt_decay=0;
 float startLevel = 1; // Sound level of first 2K samples
 uint decayTime = 0; // Time (in samples) where level decays below
};

struct Analysis {
 VList<Plot> plots;
 unique<Window> window = nullptr;
 Analysis() {
  Sampler sampler ("Piano/Piano.sfz"_);
  uint minDuration = -1;
  array<range> velocityLayers; array<uint> keys;
  for(const Sampler::Sample& sample: sampler.samples) {
   assert_(sample.trigger==0); //if(sample.trigger!=0) continue;
   //if(sample.pitch_keycenter == 108) continue;
   if(!velocityLayers.contains(range(sample.lovel,sample.hivel+1))) velocityLayers.append( range(sample.lovel,sample.hivel+1) );
   if(!keys.contains(sample.pitch_keycenter)) keys.append( sample.pitch_keycenter );
   minDuration = min(minDuration, sample.flac.duration);
  }
  log(minDuration, minDuration/sampler.rate);
  //const uint N = 8192; // Analysis window size (A-1 (27Hz~2K)) ~ 0.2s

  buffer</*velocity*/ map<float /*key*/,float>> energies (velocityLayers.size, 0);  // For each note (in MIDI key), energy relative to mean of loudest layer
  for(int velocityLayer: range(velocityLayers.size)) {
   energies.append();
   for(uint keyIndex: range(keys.size)) {
    for(const Sampler::Sample& sample: sampler.samples) {
     assert_(sample.trigger==0); //if(sample.trigger!=0) continue;
     if(range(sample.lovel, sample.hivel+1) != velocityLayers[velocityLayer]) continue;
     if(sample.pitch_keycenter != keys[keyIndex]) continue;
     buffer<float2> stereo = decodeAudio(sample.data, minDuration);//, N);
     //assert(stereo.size == N, stereo.size, sample.name, sample.data.size);
     float energy = 0;
     for(float2 Y: stereo/*N*/) {
      float y = (Y[0]+Y[1]);// * 0x1p-25f;
      energy += sq(y);
     }
     energies[velocityLayer].insertMulti(keys[keyIndex], energy);
    }
   }
   log(velocityLayer+1, velocityLayers.size);
  }
  const float e0 = mean(energies.last().values); // Computes mean energy of highest velocity layer
  for(auto& layer: energies) for(float& e: layer.values) e /= e0; // Normalizes all energy values
  /*{// Flattens all samples to same level using SFZ "volume" attribute
            String sfz;
            // Keys with dampers
            sfz << "<group> ampeg_release=1\n"_;
            float maxGain = 0;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger!=0 || sample.hikey>88) continue;
                int velocityLayer = velocityLayers.indexOf(range(sample.lovel,sample.hivel+1));
                float e = energy[velocityLayer].at(sample.pitch_keycenter);
                maxGain = max(maxGain, sqrt(1/e));
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                       +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+
                       " volume="_+str(10*log10(1/e))+"\n"_; // 10 not 20 as energy is already squared
            }
            // Keys without dampers
            sfz << "<group> ampeg_release=0\n"_;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger!=0 || sample.hikey<=88) continue;
                int velocityLayer = velocityLayers.indexOf(range(sample.lovel,sample.hivel+1));
                float e = energy[velocityLayer].at(sample.pitch_keycenter);
                maxGain = max(maxGain, sqrt(1/e));
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                       +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+
                       " volume="_+str(10*log10(1/e))+"\n"_; // 10 not 20 as energy is already squared
            }
            // Release samples
            sfz << "<group> trigger=release\n"_;
            for(const Sample& sample: sampler.samples) {
                if(sample.trigger==0) continue;
                assert(sample.hikey<=88); // Keys without dampers
                sfz << "<region> sample="_+sample.name+" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                       +" lovel="_+str(sample.lovel)+" hivel="_+str(sample.hivel)+" pitch_keycenter="_+str(sample.pitch_keycenter)+"\n"_;
            }
            writeFile("Salamander."_+str(N)+".sfz"_,sfz,Folder("Samples"_));
        }*/

  if(1) {
   for(auto& e: energies) for(float& k: e.keys) k -= 21; // A0 -> 0
   for(auto& e: energies) for(float& y: e.values) y = 10*log10(y); // Decibels
   {Plot plot("Energy"_, false);
    plot.xlabel = "Key"__, plot.ylabel = "Decibels"__;
    plot.dataSets = {apply(velocityLayers, [](range velocity){return str(velocity.stop);}), ::move(energies)};
    plots.append(move(plot));
   }
  }
  if(plots) window = ::window(&plots);
 }
} analyze;
