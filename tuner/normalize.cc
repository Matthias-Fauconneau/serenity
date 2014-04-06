#include "thread.h"
#include "sampler.h"
#include "math.h"
#include "time.h"
#include "layout.h"
#include "window.h"
#include "png.h"

float mean(const ref<float>& v) { return sum(v)/v.size; }
bool operator ==(range a, range b) { return a.start==b.start && a.stop==b.stop; }
bool operator <(range a, range b) { return a.start<b.start && a.stop<b.stop; }
inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }

/// Normalizes velocity layers to constant energy (after shaping by microphone response)
struct Normalize {
    Normalize() {
        Sampler sampler;
        const uint rate = 48000;
        sampler.open(rate, "Salamander.raw.sfz"_, Folder("Samples"_,root()));

        array<range> velocityLayers, releaseVelocityLayers; array<int> keys; uint N = -1;
        for(const Sample& sample: sampler.samples) {
            array<range>& layers = sample.trigger ? releaseVelocityLayers : velocityLayers;
            if(!layers.contains(range(sample.lovel,sample.hivel+1))) layers.insertSorted(range(sample.lovel,sample.hivel+1));
            if(!keys.contains(sample.pitch_keycenter)) keys << sample.pitch_keycenter;
            N = min(N, sample.flac.duration);
        }

        array<map<float,float>> energy; energy.grow(velocityLayers.size); // For each note (in MIDI key), energy relative to average
        for(int velocityLayer: range(velocityLayers.size)) {
            for(uint keyIndex: range(keys.size)) {
                const Sample& sample =
                        *({const Sample* sample = 0;
                           for(const Sample& s: sampler.samples) {
                               if(s.trigger!=0) continue;
                               if(range(s.lovel,s.hivel+1) != velocityLayers[velocityLayer]) continue;
                               if(s.pitch_keycenter != keys[keyIndex]) continue;
                               assert(!sample);
                               sample = &s;
                           }
                           assert(sample, velocityLayer, keyIndex, velocityLayers[velocityLayer].start, velocityLayers[velocityLayer].stop, keys[keyIndex]);
                           sample;
                          });

                assert(N<=sample.flac.duration, N, sample.flac.duration, velocityLayer, velocityLayers[velocityLayer].start, keyIndex, keys[keyIndex]);
                buffer<float2> stereo = decodeAudio(sample.data, N);
                double e=0; for(uint i: range(N)) e += sq(stereo[i][0])+sq(stereo[i][1]);
                energy[velocityLayer].insert(sample.pitch_keycenter, e);
            }
        }
        const uint maximumVelocity = velocityLayers.last().stop;
        const float maximumEnergy = mean(energy.last().values); // Computes mean energy of highest velocity layer

        // Flattens all samples to same level using SFZ "volume" attribute
        String sfz;
        sfz << "<group> ampeg_release=1\n"_; // Keys with dampers
        for(uint keyIndex: range(keys.size)) {
            if(keys[keyIndex]==90) sfz << "<group> ampeg_release=0\n"_; // Start of "Keys without dampers" section
            for(int velocityLayer: range(velocityLayers.size)) {
                const Sample& sample =
                        *({const Sample* sample = 0;
                           for(const Sample& s: sampler.samples) {
                               if(s.trigger!=0) continue;
                               if(range(s.lovel,s.hivel+1) != velocityLayers[velocityLayer]) continue;
                               if(s.pitch_keycenter != keys[keyIndex]) continue;
                               assert(!sample);
                               sample = &s;
                           }
                           assert(sample);
                           sample;
                          });

                float actual = energy[velocityLayer].at(sample.pitch_keycenter);
                uint velocity = velocityLayers[velocityLayer].stop;
                float ideal = sq((float)velocity/maximumVelocity)*maximumEnergy;
                sfz << "<region> sample="_+right(sample.name, 16)+" lokey="_+dec(sample.lokey,3)+" hikey="_+dec(sample.hikey,3)
                       +" lovel="_+dec(sample.lovel,3)+" hivel="_+dec(sample.hivel,3)+" pitch_keycenter="_+dec(sample.pitch_keycenter,3)+
                       " volume="_+ftoa(10*log10(ideal/actual),2,6)+"\n"_; // 10 not 20 as energy is already squared
            }
            if(keys[keyIndex]<=87) { // Release sample
                for(int velocityLayer: range(releaseVelocityLayers.size)) {
                    const Sample& sample =
                            *({const Sample* sample = 0;
                               for(const Sample& s: sampler.samples) {
                                   if(s.trigger!=1) continue;
                                   if(range(s.lovel,s.hivel+1) != releaseVelocityLayers[velocityLayer]) continue;
                                   if(s.pitch_keycenter != keys[keyIndex]) continue;
                                   assert(!sample);
                                   sample = &s;
                               }
                               assert(sample, keys[keyIndex], releaseVelocityLayers[velocityLayer].start, releaseVelocityLayers[velocityLayer].stop);
                               sample;
                              });
                    assert(sample.hikey<=88); // Keys without dampers
                    sfz << "<region> sample="_+right(sample.name, 16)+" lokey="_+dec(sample.lokey,3)+" hikey="_+dec(sample.hikey,3)
                           +" lovel="_+dec(sample.lovel,3)+" hivel="_+dec(sample.hivel,3)+" pitch_keycenter="_+dec(sample.pitch_keycenter,3)+
                           " trigger=release \n"_;
                }
            }
        }
        writeFile("Salamander.flat.sfz"_,sfz,Folder("Samples"_));
    }
} test;
