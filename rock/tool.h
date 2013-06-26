#include "process.h"

/// Post-processing tool
struct Tool {
   /// Executes the tool computing data results using process
   virtual void execute(Process& process) abstract;
};
