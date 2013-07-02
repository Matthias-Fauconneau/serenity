#pragma once
/// \file operation.h Abstract interface for operations run by a process manager
#include "result.h"

 /// Executes an operation using inputs to compute outputs (of given sample sizes)
struct Operation {
    /// Returns which parameters affects this operation output
    virtual string parameters() const { return ""_; }
    /// Returns the desired intermediate data size in bytes for each outputs
    virtual size_t outputSize(const Dict& args unused, const ref<Result*>& inputs unused, uint index unused) { return 0; } // Unknown sizes by default
    /// Executes the operation using inputs to compute outputs
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) abstract;
    /// Virtual destructor
    virtual ~Operation() {}
};

/// Convenient helper method to implement outputs
template<Type F> bool output(const ref<Result*>& outputs, const string& name, const string& metadata, F data) {
    for(Result* output: outputs) if(output->name == name) {
        assert_(!output->metadata);
        output->metadata = String(metadata);
        assert_(!output->data);
        output->data = data();
        return true;
    }
    return false;
}
/// Convenient helper method to implement outputs
template<Type F> bool outputElements(const ref<Result*>& outputs, const string& name, const string& metadata, F data) {
    for(Result* output: outputs) if(output->name == name) {
        assert_(!output->metadata);
        output->metadata = String(metadata);
        assert_(!output->data);
        output->elements = data();
        return true;
    }
    return false;
}
/// Convenience class to define a single input, single output operation
struct Pass : virtual Operation {
    virtual void execute(const Dict& args, Result& output, const Result& source) abstract;
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs) override { execute(args, *outputs[0], *inputs[0]); }
};
