#include "process.h"

/// Post-processing tool
struct Tool {
    /// Executes the tool computing data results using process
    virtual buffer<byte> execute(Process& process) abstract;
    /// Virtual destructor
    virtual ~Tool() {}
};
