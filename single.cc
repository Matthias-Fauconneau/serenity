#include "view.h"

Dict parameters() {
 Dict parameters = parseDict("Speed: 0.04,PlateSpeed: 1e-4"_);
 for(string argument: arguments()) /*if(argument.contains('='))*/ parameters.append(parseDict(argument));
 return parameters;
}

File file {str(::parameters())+".working", currentWorkingDirectory(), Flags(WriteOnly|Create|Truncate)};
#if UI
SimulationView app {::parameters(), move(file)};
#else
SimulationRun app {::parameters(), move(file)};
#endif
