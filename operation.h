#pragma once
#include "array.h"

static array<struct Operation*> operations;
/// Depends on named inputs to compute named outputs of a given sample size
struct Operation {
    Operation(const ref<byte>& name, uint sampleSize) : name(name) { outputs<<Output{name,sampleSize}; operations << this; }
    Operation(const ref<byte>& input, const ref<byte>& name, uint sampleSize) : Operation(name, sampleSize) { inputs<<input; }
    Operation(const ref<byte>& input, const ref<byte>& name, uint sampleSize, const ref<byte>& output2, uint sampleSize2) : Operation(input, name, sampleSize) {
        outputs<<Output{output2,sampleSize2}; }
    Operation(const ref<byte>& input1, const ref<byte>& input2, const ref<byte>& name, uint sampleSize) : Operation(input1, name, sampleSize) { inputs<<input2; }
    Operation(const ref<byte>& input1, const ref<byte>& input2, const ref<byte>& input3, const ref<byte>& name, uint sampleSize) : Operation(input1, input2, name, sampleSize) {
        inputs<<input3;
    }

    ref<byte> name;
    array<ref<byte>> inputs;
    struct Output { ref<byte> name; uint sampleSize; }; array<Output> outputs;
};
const ref<byte>& str(const Operation& operation) { return operation.name; }
bool operator==(const Operation& a, const Operation& b) { return a.name == b.name; }
bool operator==(const Operation::Output& a, const ref<byte>& b) { return a.name == b; }
const Operation* operationForOutput(const ref<byte>& name) { for(Operation* operation: operations) for(const Operation::Output& output: operation->outputs) if(output.name==name) return operation; return 0; }

