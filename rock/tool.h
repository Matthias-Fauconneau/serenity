#include "process.h"

/// Post-processing tool
struct Tool {
    /// Executes the tool computing data results using process
    virtual shared<Result> execute(Process& process) abstract;
    /// Virtual destructor
    virtual ~Tool() {}
};
