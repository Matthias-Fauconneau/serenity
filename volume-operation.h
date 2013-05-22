#pragma once
#include "operation.h"
#include "volume.h"

// Converts an abstract operation Result to a volume
inline Volume toVolume(const Result& result) {
    Volume volume;
    parseVolumeFormat(volume, result.metadata);
    volume.data = buffer<byte>(result.data);
    volume.sampleSize = volume.data.size / volume.size();
    assert(volume.sampleSize >= align(8, nextPowerOfTwo(log2(nextPowerOfTwo((volume.maximum+1))))) / 8); // Minimum sample size to encode maximum value (in 2ⁿ bytes)
    return volume;
}

/// Convenience class to help define volume operations
struct VolumeOperation : virtual Operation {
    virtual uint outputSampleSize(uint index) abstract;
    uint64 outputSize(map<ref<byte>, Variant>&, const ref<shared<Result>>& inputs, uint index) override {
        uint64 outputSize = toVolume(inputs[0]).size() * outputSampleSize(index);
        assert_(outputSize<=(1ll<<32), toVolume(inputs[0]), toVolume(inputs[0]).size(), outputSampleSize(index));
        return outputSize; //toVolume(inputs[0]).size() * outputSampleSize(index);
    }
    virtual void execute(map<ref<byte>, Variant>& args, array<Volume>& outputs, const ref<Volume>& inputs) abstract;
    void execute(map<ref<byte>, Variant>& args, array<shared<Result>>& outputs, const ref<shared<Result>>& inputs) override {
        array<Volume> inputVolumes = apply<Volume>(inputs, toVolume);
        array<Volume> outputVolumes;
        for(uint index: range(outputs.size)) {
            Volume volume;
            volume.sampleSize = outputSampleSize(index);
            volume.data = buffer<byte>(outputs[index]->data);
            if(inputVolumes) { // Inherits initial metadata from previous operation
                const Volume& source = inputVolumes.first();
                volume.x=source.x, volume.y=source.y, volume.z=source.z, volume.copyMetadata(source);
                assert(volume.sampleSize * volume.size() == volume.data.size);
            }
            outputVolumes << move( volume );
        }
        execute(args, outputVolumes, inputVolumes);
        for(uint index: range(outputs.size)) {
            Volume& output = outputVolumes[index];
            if(output.sampleSize==2) assert(maximum((const Volume16&)output)<=output.maximum, output, maximum((const Volume16&)output), output.maximum);
            if(output.sampleSize==4) assert(maximum((const Volume32&)output)<=output.maximum, output, maximum((const Volume32&)output), output.maximum);
            outputs[index]->metadata = volumeFormat(output);
        }
    }
};

/// Convenience class to define a single input, single output volume operation
template<Type I, Type O> struct VolumePass : virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(O); }
    virtual void execute(map<ref<byte>, Variant>& args, VolumeT<O>& target, const VolumeT<I>& source) abstract;
    virtual void execute(map<ref<byte>, Variant>& args, array<Volume>& outputs, const ref<Volume>& inputs) override { execute(args, outputs[0], inputs[0]); }
};
