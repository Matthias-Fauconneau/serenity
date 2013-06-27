#pragma once
#include "result.h"

struct ResultManager {
    /// Returns a valid cached Result for \a target with \a arguments or generates it if necessary
    virtual shared<Result> getResult(const string& target, const Dict& arguments) abstract;
};

/// High level operation with direct access to query new results from process
/// \note as inputs are unknown, results are not regenerated on input changes
struct Tool {
    /// Executes the tool computing data results using process
    virtual void execute(const Dict& args, const ref<Result*>& outputs, const ref<Result*>& inputs, ResultManager& results) abstract;
    /// Virtual destructor
    virtual ~Tool() {}
};
