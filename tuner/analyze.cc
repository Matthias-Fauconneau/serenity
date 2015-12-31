#include "thread.h"
//include "pitch.h"
#include "sampler.h"
#include "math.h"
#include "time.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "png.h"

String str(range r) { return "["+str(r.start, r.stop)+"]"; }
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
#if 0
  Plot plot("Attacks"_, true);
  plot.xlabel = "Time"__, plot.ylabel = "Level"__;
  for(const Sampler::Sample& sample: sampler.samples) {
   const size_t N = 4096;
   buffer<float2> stereo = decodeAudio(sample.data, N);;
   map<float, float> attack;
   attack.keys.reserve(N);
   for(size_t i: range(N)) {
    float y = (stereo[i][0]+stereo[i][1]);// * 0x1p-25f;
    attack.insert((float)i/sampler.rate, y);
   }
   plot.dataSets.insert(copyRef(sample.name), ::move(attack));
  }
  plots.append(move(plot));
#else
  uint minDuration = -1;
  array<range> velocityLayers; array<uint> keys;
  for(const Sampler::Sample& sample: sampler.samples) {
   assert_(sample.trigger==0); //if(sample.trigger!=0) continue;
   //if(sample.pitch_keycenter == 108) continue;
   if(!velocityLayers.contains(range(sample.lovel,sample.hivel+1))) velocityLayers.append( range(sample.lovel,sample.hivel+1) );
   if(!keys.contains(sample.pitch_keycenter)) keys.append( sample.pitch_keycenter );
   minDuration = min(minDuration, sample.flac.duration);
  }
  //log(velocityLayers);
  log(minDuration, minDuration/sampler.rate);
  //assert_(minDuration >= sampler.rate/2);
  //minDuration = 8192; // Attack energy (0.4s)
  //const uint maxThresholdT0 = 10685;//742;
  //const uint maxMaxT0 = 16059;
  //const uint unused D = 402058;
  //const uint unused maxMaxT0 = 8141;
  //const uint maxMaxT0 = 24717;
  //const uint N = 4096; // Analysis window size ~ 0.2s
  //assert_(maxThresholdT0+N <= minDuration);

  buffer</*velocity*/ map<float /*key*/,float>> energies (velocityLayers.size, 0);  // For each note (in MIDI key), energy relative to mean of loudest layer
  buffer</*velocity*/ map<float /*key*/,float>> correctedEnergies (velocityLayers.size, 0);  // For each note (in MIDI key), energy relative to mean of loudest layer
  buffer<float> sampleEnergies (sampler.samples.size);
  buffer<float> sampleStarts (sampler.samples.size);
  buffer<float> sampleEnds (sampler.samples.size);
  float minE = inf, maxE = 0;
  uint minmaxT = -1, maxmaxT = 0; float minmax = inf;
  float threshold = 0.0005;
  uint minThresholdT = -1, maxThresholdT = 0;
  uint minThresholdTend = -1, maxThresholdTend = 0;
  uint minN = -1, maxN = 0;
  for(int velocityLayer: range(velocityLayers.size)) {
   energies.append();
   correctedEnergies.append();
   for(uint keyIndex: range(keys.size)) {
    for(size_t sampleIndex: range(sampler.samples.size)) {
     const Sampler::Sample& sample = sampler.samples[sampleIndex];
     assert_(sample.trigger==0); //if(sample.trigger!=0) continue;
     if(range(sample.lovel, sample.hivel+1) != velocityLayers[velocityLayer]) continue;
     if(sample.pitch_keycenter != keys[keyIndex]) continue;
     buffer<float2> Y = decodeAudio(sample.data, 65536);
     //assert(stereo.size == N, stereo.size, sample.name, sample.data.size);
     float sum = 0;
     for(float2 y: Y) sum += (y[0]+y[1]) * 0x1p-25f;
     float mean = sum / Y.size;
     float energy = 0;
     float max = 0; uint maxT = 0; uint thresholdT = ~0;
     for(size_t t : range(Y.size)) {
      float y = (Y[t][0]+Y[t][1]) * 0x1p-25f - mean;
      if(abs(y) > max) { max=abs(y); maxT=t; }
      if(thresholdT==uint(~0))  if(abs(y) >= threshold) thresholdT=t;
     }
     uint thresholdTend = 0;
     for(size_t t : reverse_range(Y.size)) {
      float y = (Y[t][0]+Y[t][1]) * 0x1p-25f - mean;
      if(abs(y) >= threshold) { thresholdTend=t; break; }
     }
     if(max < threshold) {
      assert_(thresholdT == uint(~0) && maxT < thresholdT, thresholdT, max, threshold, maxT, thresholdT);
      error(max);
      thresholdT = maxT;
     }
     //thresholdT = maxT;
     //assert_(thresholdT+N <= Y.size, thresholdT);
     thresholdTend = ::max(thresholdTend, thresholdT+4096);
     for(size_t t : range(thresholdT, thresholdTend)) {
      float y = (Y[t][0]+Y[t][1]) * 0x1p-25f - mean;
      energy += sq(y); // Measures energy only from maxT to maxT+N
     }
     energy /= (thresholdTend-thresholdT);
     //assert_(threshold <= max, threshold, max, maxT, sample.flac.duration, sample.name);
    // assert_(thresholdT!=uint(~0));
     //log(max, maxT, threshold, thresholdT);
     maxN = ::max(maxN, thresholdTend-thresholdT);
     minN = ::min(minN, thresholdTend-thresholdT);

     maxmaxT = ::max(maxmaxT, maxT);
     minmaxT = ::min(minmaxT, maxT);
     minmax = ::min(minmax, max);
     minThresholdT = ::min(minThresholdT, thresholdT);
     maxThresholdT = ::max(maxThresholdT, thresholdT);
     minThresholdTend = ::min(minThresholdTend, thresholdTend);
     maxThresholdTend = ::max(maxThresholdTend, thresholdTend);
     energies[velocityLayer].insertMulti(keys[keyIndex], energy);
     correctedEnergies[velocityLayer].insertMulti(keys[keyIndex], energy*sq(sample.volume));
     sampleEnergies[sampleIndex] = energy;
     if(sample.pitch_keycenter==57) { log(sample.pitch_keycenter, thresholdT, thresholdTend); assert_(thresholdT < 2115, thresholdT); }
     if(sample.pitch_keycenter==26) assert_(thresholdT <= 450 || thresholdT>=2503, thresholdT);// log(sample.pitch_keycenter, thresholdT, thresholdT >= 2509);
     sampleStarts[sampleIndex] = thresholdT;
     sampleEnds[sampleIndex] = thresholdTend;
     minE = ::min(minE, energy);
     maxE = ::max(maxE, energy);
    }
   }
   log(velocityLayer+1, "/", velocityLayers.size);
  }
  log("minmax", minmax, "minmaxT", minmaxT, "maxmaxT", maxmaxT);//, minE, maxE);
  log("minThresholdT", minThresholdT, "maxThresholdT", maxThresholdT);
  log("minThresholdTend", minThresholdTend, "maxThresholdTend", maxThresholdTend);
  log("minN", minN, "maxN", maxN);
  {// Flattens all samples to same level using SFZ "volume" attribute
   array<char> sfz;
   int maxlovel = 0;
   auto process = [&](bool with) {
    for(size_t sampleIndex: range(sampler.samples.size)) {
     Sampler::Sample& sample = sampler.samples[sampleIndex];
     if(sample.trigger!=0 || (with ^ !(sample.pitch_keycenter>88))) continue;
     float e = sampleEnergies[sampleIndex];
#if 1 // Overrides velocity layers
     float under = minE, over = maxE;
     for(size_t otherIndex: range(sampler.samples.size)) {
      const Sampler::Sample& other = sampler.samples[otherIndex];
      if(other.pitch_keycenter != sample.pitch_keycenter) continue;
      if(other.random != sample.random) continue;
      if(other.locc64 != sample.locc64 || other.hicc64 != sample.hicc64) continue;
      if(sampleIndex == otherIndex) continue;
      float otherE = sampleEnergies[otherIndex];
      if(otherE < e && otherE > under) under = otherE;
      if(otherE > e && otherE < over) over = otherE;
     }
     assert_(under <= e && e <= over, minE, under, e, over, maxE);
     float lowE = under>minE ? (ln(under)+ln(e))/2 : ln(minE);
     float highE = over<maxE ? (ln(e)+ln(over))/2 : ln(maxE);
     assert_(lowE < highE, lowE, highE);
     int lovel = 1+floor((lowE-ln(minE))/(ln(maxE)-ln(minE))*127);
     int hivel = min(127, int(0+ceil((highE-ln(minE))/(ln(maxE)-ln(minE))*127)));
     //int lovel = sqrt((lowE-minE)/(maxE-minE))*127;
     //int hivel = sqrt((highE-minE)/(maxE-minE))*127;
#else
     int lovel = sample.lovel;
     int hivel = sample.hivel;
#endif
     maxlovel = ::max(maxlovel, lovel);
     assert_(0 <= lovel && lovel <= hivel && hivel  <= 127, lovel, hivel, minE, under, lowE, e, highE, over, maxE); //FIXME: round robin too similar samples
     sfz.append("<region> sample="_+sample.name
                +" lokey="_+str(sample.lokey)+" hikey="_+str(sample.hikey)
                +" lovel="_+str(lovel)+" hivel="_+str(hivel)
                +" locc64="_+str(sample.locc64)+" hicc64="_+str(sample.hicc64)
                +" lorand="_+(sample.random?"0.5"_:"0"_)+" hirand="_+(sample.random?"1"_:"0.5"_)
                +" pitch_keycenter="_+str(sample.pitch_keycenter)
                +" volume="_+str(10*log10(maxE/e)) // 10 not 20 as energy is already squared
                +" offset="_+str(sampleStarts[sampleIndex])
                +" end="_+str(sampleEnds[sampleIndex])
                +"\n"_);
     sample.lovel = lovel; sample.hivel = hivel; // DEBUG
    }
   };
   log("maxlovel", maxlovel);
   // Keys with dampers
   sfz.append("<group> ampeg_release=1\n"_);
   process(true);
   // Keys without dampers
   sfz.append("<group> ampeg_release=0\n"_);
   process(false);
   //log(sfz);
   for(size_t key: range(21, 21+88)) {
    for(int cc64: range(2)) {
     for(uint random: range(2)) {
     // log(key, cc64, random);
      for(size_t sampleIndex: range(sampler.samples.size)) {
       const Sampler::Sample& sample = sampler.samples[sampleIndex];
       if(sample.pitch_keycenter == key && ((sample.locc64==0) == (cc64==0)) && sample.random == random) {}//log(sample.lovel, sample.hivel);
      }
     }
    }
   }
   writeFile("Piano.autocorrected.sfz"_, sfz, "Piano"_, true);
  }

  if(1) {
   const float e0 = mean(energies.last().values); // Computes mean energy of highest velocity layer
   for(auto& layer: energies) for(float& energy: layer.values) energy /= e0; // Normalizes all energy values
   for(auto& e: energies) for(float& k: e.keys) k -= 21; // A0 -> 0
   for(auto& e: energies) for(float& y: e.values) y = 10*log10(y); // Decibels
   for(auto& e: correctedEnergies) for(float& k: e.keys) k -= 21; // A0 -> 0
   for(auto& e: correctedEnergies) for(float& y: e.values) y = 10*log10(y); // Decibels
   {Plot plot("Energy"_, false);
    plot.xlabel = "Key"__, plot.ylabel = "Decibels"__;
    /*plot.dataSets = {
     apply(velocityLayers, [](range velocity){return str(velocity.stop);})+apply(velocityLayers, [](range velocity)->String{return str(velocity.stop)+"*"_;}),
     ::move(energies)+::move(correctedEnergies)
    };*/
    //plot.dataSets = {apply(velocityLayers, [](range velocity){return str(velocity.stop);}),::move(energies)};
    plot.dataSets = {apply(velocityLayers, [](range velocity)->String{return str(velocity.stop)+"*"_;}),::move(correctedEnergies)};
    plots.append(move(plot));
   }
  }
#endif
  if(plots) window = ::window(&plots);
 }
} analyze;
