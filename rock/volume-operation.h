#pragma once
#include "operation.h"
#include "volume.h"

// Converts an abstract operation Result to a volume
inline Volume toVolume(const Result& result) {
    Volume volume;
    if( !parseVolumeFormat(volume, result.metadata) ) return Volume();
    volume.data = buffer<byte>(ref<byte>(result.data));
    volume.sampleSize = volume.data.size / volume.size();
    assert(volume.sampleSize >= align(8, nextPowerOfTwo(log2(nextPowerOfTwo((volume.maximum+1))))) / 8, volume.sampleSize, volume.data.size); // Minimum sample size to encode maximum value (in 2ⁿ bytes)
    return volume;
}

/// Convenience class to help define volume operations
struct VolumeOperation : virtual Operation {
    /// Overriden by implementation to return required output sample size (or 0 for non-volume output)
    virtual uint outputSampleSize(uint index unused) { return 0; } // No volume output by default
    uint64 outputSize(const Dict&, const ref<Result*>& inputs, uint index) override {
        assert_(inputs);
        assert_(toVolume(*inputs[0]), inputs[0]->name);
        return toVolume(*inputs[0]).size() * outputSampleSize(index);
    }
    /// Actual operation (overriden by implementation)
    virtual void execute(const Dict& args unused, const mref<Volume>& outputs unused, const ref<Volume>& inputs unused) { error("None of the execute methods were overriden by implementation"_); }
    /// Actual operation (overriden by implementation) with additional non-volume outputs
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const mref<Result*>&) { this->execute(args, outputs, inputs); }
    /// Actual operation (overriden by implementation) with additional non-volume inputs
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const ref<Result*>&) { this->execute(args, outputs, inputs); }
    /// Actual operation (overriden by implementation) with additional non-volume outputs and inputs
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs, const mref<Result*>& otherOutputs, const ref<Result*>& otherInputs) {
        if(!otherOutputs && !otherInputs) return this->execute(args, outputs, inputs);
        if(otherOutputs && !otherInputs) return this->execute(args, outputs, inputs, otherOutputs);
        if(otherInputs && !otherOutputs) return this->execute(args, outputs, inputs, otherInputs);
        error("Implementation ignores all non-volume inputs and non-volume outputs", outputs, inputs, otherOutputs, otherInputs);
    }
    void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override {
        array<Volume> inputVolumes; array<Result*> otherInputs;
        for(Result* input: inputs) {
            Volume volume = toVolume(*input);
            if(volume) inputVolumes << move(volume);
            else otherInputs << input;
        }
        array<Volume> outputVolumes; array<Result*> otherOutputs;
        for(uint index: range(outputs.size)) {
            uint sampleSize = this->outputSampleSize(index);
            if(sampleSize) {
                Volume volume;
                volume.sampleSize = sampleSize;
                volume.data = unsafeReference(outputs[index]->data);
                if(inputVolumes) { // Inherits initial metadata from previous operation
                    const Volume& source = inputVolumes.first();
                    volume.sampleCount=source.sampleCount; volume.copyMetadata(source);
                    assert(volume.sampleSize * volume.size() == volume.data.size, volume.sampleSize, volume.size(), volume.data.size, index, outputs);
                    if(source.tiled()) interleavedLookup(volume);
                }
                outputVolumes << move( volume );
            } else {
                otherOutputs << outputs[index];
            }
        }
        execute(args, outputVolumes, inputVolumes, otherOutputs, otherInputs);
        uint outputVolumesIndex=0; for(uint index: range(outputs.size)) {
            uint sampleSize = this->outputSampleSize(index);
            if(sampleSize) {
                Volume& output = outputVolumes[outputVolumesIndex++];
                if(output.sampleSize==2) assert(maximum((const Volume16&)output)==output.maximum, outputs[index]->name, output, maximum((const Volume16&)output), output.maximum);
                if(output.sampleSize==4) assert(maximum((const Volume32&)output)==output.maximum, outputs[index]->name, output, maximum((const Volume32&)output), output.maximum);
                outputs[index]->metadata = volumeFormat(output);
                outputs[index]->data.size = output.data.size;
            }
        }
    }
};

/// Convenience class to define a single input, single output volume operation
template<Type O> struct VolumePass : virtual VolumeOperation {
    uint outputSampleSize(uint) override { return sizeof(O); }
    virtual void execute(const Dict& args, VolumeT<O>& target, const Volume& source) abstract;
    virtual void execute(const Dict& args, const mref<Volume>& outputs, const ref<Volume>& inputs) override { execute(args, outputs[0], inputs[0]); }
};
#define defineVolumePass(name, type, function) \
    class(name, Operation), virtual VolumePass<type> { void execute(const Dict&, VolumeT<type>& target, const Volume& source) override { function(target, source); } }
