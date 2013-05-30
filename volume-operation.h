#pragma once
#include "operation.h"
#include "volume.h"

// Converts an abstract operation Result to a volume
inline Volume toVolume(const Result& result) {
    Volume volume;
    parseVolumeFormat(volume, result.metadata);
    volume.data = buffer<byte>(ref<byte>(result.data));
    volume.sampleSize = volume.data.size / volume.size();
    assert(volume.sampleSize >= align(8, nextPowerOfTwo(log2(nextPowerOfTwo((volume.maximum+1))))) / 8, volume.sampleSize, volume.data.size); // Minimum sample size to encode maximum value (in 2ⁿ bytes)
    return volume;
}

/// Convenience class to help define volume operations
struct VolumeOperation : virtual Operation {
    virtual uint outputSampleSize(uint index) abstract;
    uint64 outputSize(const Dict&, const ref<shared<Result>>& inputs, uint index) override {  return toVolume(inputs[0]).size() * outputSampleSize(index); }
    virtual void execute(const Dict& args, array<Volume>& outputs, const ref<Volume>& inputs) abstract;
    void execute(const Dict& args, array<shared<Result>>& outputs, const ref<shared<Result>>& inputs) override {
        array<Volume> inputVolumes = apply<Volume>(inputs, toVolume);
#if 1
        for(uint index: range(inputs.size)) {
            Volume& input = inputVolumes[index];
            if(input.sampleSize==2) assert(maximum((const Volume16&)input)<=input.maximum, inputs[index]->name, input, hex(maximum((const Volume16&)input)), hex(input.maximum));
            if(input.sampleSize==4) assert(maximum((const Volume32&)input)<=input.maximum, inputs[index]->name, input, hex(maximum((const Volume32&)input)), hex(input.maximum));
        }
#endif
        array<Volume> outputVolumes;
        for(uint index: range(outputs.size)) {
            Volume volume;
            volume.sampleSize = outputSampleSize(index);
            volume.data = buffer<byte>(ref<byte>(outputs[index]->data));
            if(inputVolumes) { // Inherits initial metadata from previous operation
                const Volume& source = inputVolumes.first();
                volume.sampleCount=source.sampleCount; volume.copyMetadata(source);
                assert(volume.sampleSize * volume.size() == volume.data.size);
            }
            outputVolumes << move( volume );
        }
        execute(args, outputVolumes, inputVolumes);
        for(uint index: range(outputs.size)) {
            Volume& output = outputVolumes[index];
            if(output.sampleSize==2) assert(maximum((const Volume16&)output)<=output.maximum, outputs[index]->name, output, maximum((const Volume16&)output), output.maximum);
            if(output.sampleSize==4) assert(maximum((const Volume32&)output)<=output.maximum, outputs[index]->name, output, maximum((const Volume32&)output), output.maximum);
            outputs[index]->metadata = volumeFormat(output);
            outputs[index]->data.size = output.data.size;
        }
    }
};

/// Convenience class to define a single input, single output volume operation
template<Type O> struct VolumePass : virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(O); }
    virtual void execute(const Dict& args, VolumeT<O>& target, const Volume& source) abstract;
    virtual void execute(const Dict& args, array<Volume>& outputs, const ref<Volume>& inputs) override { execute(args, outputs[0], inputs[0]); }
};
#define PASS(name, type, function) \
    class(name, Operation), virtual VolumePass<type> { void execute(const Dict&, VolumeT<type>& target, const Volume& source) override { function(target, source); } }

/// Convenience class to define a single input, no output volume operation
struct VolumeInput : virtual Operation {
    uint64 outputSize(const Dict&, const ref<shared<Result>>&, uint) override { return 0; }
    virtual void execute(const Dict& args, const ref<byte>& name, const Volume& source) abstract;
    virtual void execute(const Dict& args, array<shared<Result>>&, const ref<shared<Result>>& inputs) override { execute(args, inputs[0]->name+"."_+inputs[0]->arguments, toVolume(inputs[0])); }
};
