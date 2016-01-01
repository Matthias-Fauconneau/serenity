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

static const float threshold = 0.0009;

float energy(ref<byte> data, uint& start, uint& end) {
 buffer<float2> Y = decodeAudio(data, 8192);
 float sum = 0;
 for(float2 y: Y) sum += (y[0]+y[1]) * 0x1p-25f;
 float mean = sum / Y.size;
 float energy = 0;
 float max = 0; uint maxT = 0; start = ~0;
 for(size_t t : range(Y.size)) {
  float y = (Y[t][0]+Y[t][1]) * 0x1p-25f - mean;
  if(abs(y) > max) { max=abs(y); maxT=t; }
  if(start==uint(~0)) if(abs(y) >= threshold) start=t;
 }
 end = 0;
 for(size_t t : reverse_range(Y.size)) {
  float y = (Y[t][0]+Y[t][1]) * 0x1p-25f - mean;
  if(abs(y) >= threshold) { end=t+1; break; }
 }
 if(max < threshold) {
  assert_(start == uint(~0) && maxT < start, start, max, threshold, maxT, start);
  log(max);
  start = 0; //maxT;
  assert_(max < 0.000236);
 }
 //thresholdT = maxT;
 //assert_(thresholdT+N <= Y.size, thresholdT);
 end = ::max(end, start+4096);
 for(size_t t : range(start, end)) {
  float y = (Y[t][0]+Y[t][1]) * 0x1p-25f - mean;
  energy += sq(y); // Measures energy only from maxT to maxT+N
 }
 energy /= (end-start);
 return energy;
}
float energy(ref<byte> data) { uint start,end; return energy(data, start, end); }

#if 1
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
  /*uint minmaxT = -1, maxmaxT = 0; float minmax = inf;
  uint minThresholdT = -1, maxThresholdT = 0;
  uint minThresholdTend = -1, maxThresholdTend = 0;
  uint minN = -1, maxN = 0;*/
  for(int velocityLayer: range(velocityLayers.size)) {
   energies.append();
   correctedEnergies.append();
   for(uint keyIndex: range(keys.size)) {
    for(size_t sampleIndex: range(sampler.samples.size)) {
     const Sampler::Sample& sample = sampler.samples[sampleIndex];
     assert_(sample.trigger==0); //if(sample.trigger!=0) continue;
     if(range(sample.lovel, sample.hivel+1) != velocityLayers[velocityLayer]) continue;
     if(sample.pitch_keycenter != keys[keyIndex]) continue;
#if 0
     buffer<float2> Y = decodeAudio(sample.data, 65536);
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
     maxN = ::max(maxN, thresholdTend-thresholdT);
     minN = ::min(minN, thresholdTend-thresholdT);
     maxmaxT = ::max(maxmaxT, maxT);
     minmaxT = ::min(minmaxT, maxT);
     minmax = ::min(minmax, max);
     minThresholdT = ::min(minThresholdT, thresholdT);
     maxThresholdT = ::max(maxThresholdT, thresholdT);
     minThresholdTend = ::min(minThresholdTend, thresholdTend);
     maxThresholdTend = ::max(maxThresholdTend, thresholdTend);
#else
     uint start, end;
     float energy = ::energy(sample.data, start, end);
#endif
     energies[velocityLayer].insertMulti(keys[keyIndex], energy);
     correctedEnergies[velocityLayer].insertMulti(keys[keyIndex], energy*sq(sample.volume));
     sampleEnergies[sampleIndex] = energy;
     //if(sample.pitch_keycenter==57) { log(sample.pitch_keycenter, thresholdT, thresholdTend); assert_(thresholdT < 2115, thresholdT); }
     if(sample.pitch_keycenter==26) { assert_(start <= 644 || start>=2001, start, sample.pitch_keycenter); log(sample.pitch_keycenter, start); }
     if(sample.pitch_keycenter==33) assert_(start <= 595 || start>=2801, start, sample.pitch_keycenter);// log(sample.pitch_keycenter, thresholdT, thresholdT >= 2509);
     sampleStarts[sampleIndex] = start;
     sampleEnds[sampleIndex] = end;
     minE = ::min(minE, energy);
     maxE = ::max(maxE, energy);
    }
   }
   log(velocityLayer+1, "/", velocityLayers.size);
  }
  //log("minmax", minmax, "minmaxT", minmaxT, "maxmaxT", maxmaxT);//, minE, maxE);
  //log("minThresholdT", minThresholdT, "maxThresholdT", maxThresholdT);
  //log("minThresholdTend", minThresholdTend, "maxThresholdTend", maxThresholdTend);
  //log("minN", minN, "maxN", maxN);
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
#if 1 // ln to use more low velocity layers
     float lowE = under>minE ? (ln(under)+ln(e))/2 : ln(minE);
     float highE = over<maxE ? (ln(e)+ln(over))/2 : ln(maxE);
     assert_(lowE < highE, lowE, highE);
     int lovel = 1+floor((lowE-ln(minE))/(ln(maxE)-ln(minE))*64);
     int hivel = min(127, int(0+ceil((highE-ln(minE))/(ln(maxE)-ln(minE))*64)));
#else // sqrt to use more high velocity layers
     float lowE = under>minE ? (sqrt(under)+sqrt(e))/2 : sqrt(minE);
     float highE = over<maxE ? (sqrt(e)+sqrt(over))/2 : sqrt(maxE);
     assert_(lowE < highE, lowE, highE);
     int lovel = 1+floor((lowE-sqrt(minE))/(sqrt(maxE)-sqrt(minE))*128);
     int hivel = min(127, int(0+ceil((highE-sqrt(minE))/(sqrt(maxE)-sqrt(minE))*128)));
#endif
     if(over >= maxE) hivel = 127;     //int lovel = sqrt((lowE-minE)/(maxE-minE))*127;
     //int hivel = sqrt((highE-minE)/(maxE-minE))*127;
     // TODO: plot velocity layers
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

#else

#include "asound.h"
#include "text.h"

struct Loudness {
  Sampler sampler {"Piano/Piano.sfz"_};

  AudioOutput audio {{this, &Loudness::read32}};

  FLAC current, other;
  string currentName = "A"_, otherName = "B"_;
  size_t currentIndex, otherIndex;
  size_t nextA, nextB;
  size_t a = invalid, b = invalid;

  Random random;

  Text text {"A"_};
  unique<Window> window = ::window(&text);

  buffer<float> energy;
  buffer<uint> start, end;
  struct pair { int a, b; bool operator<(const pair& o) const { return a < o.a || (a==o.a && b < o.b); } };
  array<pair> greaterThan;
  struct EnergyDifference { float d; int a, b; bool operator<(const EnergyDifference& o) const { return d < o.d; } };
  array<EnergyDifference> energyDifference;

  size_t read32(mref<int2> out) {
   buffer<float2> buffer(out.size);
   size_t read = current.read(buffer);
   if(a!=invalid && b==invalid && current.position >= sampler.rate/4) b = nextB; // Already allow to decide while still playing B for the first time
   if(read<out.size || current.position >= end[currentIndex]) {
    swap(current, other);
    swap(currentName, otherName);
    swap(currentIndex, otherIndex);
    text = currentName; window->render();
    /**/  if(a==invalid) a = nextA;
    else if(b==invalid) b = nextB;
    current = ::copy(sampler.samples[currentIndex].flac); // Resets decoder
    //current.skip(start[currentIndex]); // Already skipped in Sampler
    size_t read2 = current.read(buffer.slice(read)); // Complete frame
    assert_(read+read2 == out.size); // Assumes samples are longer than a period
   }
   for(size_t i: range(out.size*2)) {
    float u = buffer[i/2][i%2];
    constexpr float minValue = -64577408, maxValue = 76015472;
    float v = (u-minValue) / (maxValue-minValue); // Normalizes range to [0-1] //((u-minValue) / (maxValue-minValue)) * 2 - 1; // Normalizes range to [-1-1]
    int w = v*0x1p32 - 0x1p31; // Converts floating point to two-complement signed 32 bit integer
    out[i/2][i%2] = w;
   }
   return out.size;
  }

  size_t index = -1;
  void next() {
   a = invalid, b = invalid; index++;
   parse();
#if RANDOM
   nextA = random%sampler.samples.size;
   nextB = random%sampler.samples.size;
#elif 1 // Confirm energy inversions
   if(index >= energyDifference.size) { log("Review completed. Next pass", energyDifference.size); index=0; }
   assert_(energyDifference.size, "All user references agrees with automatic evaluation");
   nextA = energyDifference[index].a;
   nextB = energyDifference[index].b;
   log(energyDifference[index].d);
   log("A", start[nextA], end[nextA], energy[nextA]);
   log("B", start[nextB], end[nextB], energy[nextB]);
#elif 0 // TODO: Confirm velocity layer inversion
#endif
   currentIndex = nextA; current = ::copy(sampler.samples[nextA].flac); currentName = "A"_; // current.skip(start[currentIndex]); Already skipped in Sampler
   otherIndex = nextB; other = ::copy(sampler.samples[nextB].flac); otherName = "B"_; // other.skip(start[otherIndex]); Already skipped in Sampler
   log(nextA, nextB);
   text = "A"_; window->render();
  }

  void parse() {
   greaterThan.clear();
   energyDifference.clear();
   array<string> sampleNames = apply(sampler.samples, [](const Sampler::Sample& sample)->string{ return sample.name; });
   if(existsFile("loudness",".config"_)) {
    String loudness = readFile("loudness",".config"_);
    array<int> testedSamples; size_t inequalCount = 0;
    for(string line: split(loudness,"\n")) {
     TextData s (line);
     string A = s.until(' ');
     int a = sampleNames.indexOf(A);
     string cmp = s.until(' ');
     string B = s.until('\n');
     int b = sampleNames.indexOf(B);

     if(testedSamples.contains(a) && testedSamples.contains(b)) { // Filters duplicates
      greaterThan.filter([=](const pair& o) {
       if(o.a == b && o.b == a) return true; // Inverse entries cancel each other, ~ cancels >
       if(o.a == a && o.b == b) return true; // Duplicate entries can be removed, ~ cancels >
       return false;
      });
     }

     if(cmp == ">") {
      inequalCount++;
      testedSamples.addSorted(a);
      testedSamples.addSorted(b);
      greaterThan.insertSorted({a, b});
     } else assert_(cmp == "~");
    }
    log(inequalCount, "=>", testedSamples.size, "/", sampler.samples.size);
   }
   for(pair entry: greaterThan) {
    float a = energy[entry.a];
    float b = energy[entry.b];
    float difference = a-b;
    if(difference < 0) energyDifference.insertSorted({difference, entry.a, entry.b});
   }
   log(energyDifference.size, "inversions of automatic estimation versus user reference");
  }

  Loudness() {
   if(!existsFile("energy",".cache"_) || !existsFile("start",".cache"_) || !existsFile("end",".cache"_) || 1) {
    buffer<float> energy(sampler.samples.size);
    buffer<uint> start(sampler.samples.size);
    buffer<uint> end(sampler.samples.size);
    for(size_t i: range(sampler.samples.size)) {
     const Sampler::Sample& sample = sampler.samples[i];
     log(sample.name);
     energy[i] = ::energy(sample.data, start[i], end[i]);
    }
    writeFile("energy",cast<byte>(energy), ".cache"_, true);
    writeFile("start",cast<byte>(start), ".cache"_, true);
    writeFile("end",cast<byte>(end), ".cache"_, true);
   }
   energy = cast<float>(readFile("energy", ".cache"_));
   start = cast<uint>(readFile("start", ".cache"_));
   end = cast<uint>(readFile("end", ".cache"_));
   parse();

   window->actions[Key('a')] = [this]{
    if(a == invalid || b == invalid) return;
    const auto& A = sampler.samples[a];
    const auto& B = sampler.samples[b];
    log("A > B");
    File("loudness",".config"_, Flags(WriteOnly|Create|Append)).write(str(A.name, ">", B.name)+'\n');
    next();
   };
   window->actions[Key('b')] = [this]{
    if(a == invalid || b == invalid) return;
    const auto& A = sampler.samples[a];
    const auto& B = sampler.samples[b];
    log("B > A");
    File("loudness",".config"_, Flags(WriteOnly|Create|Append)).write(str(B.name, ">", A.name)+'\n');
    next();
   };
   window->actions[Space] = [this]{
    if(a == invalid || b == invalid) return;
    const auto& A = sampler.samples[a];
    const auto& B = sampler.samples[b];
    log("A ~ B");
    File("loudness",".config"_, Flags(WriteOnly|Create|Append)).write(str(A.name, "~", B.name)+'\n');
    next();
   };

   random.seed();
   next();
   audio.start(sampler.rate, 4096, 32, 2); // Keeps device open to avoid clicks
  }
} loudness;
#endif
