#include "view.h"

Dict parameters() {
 Dict parameters;
 for(string argument: arguments()) /*if(argument.contains('='))*/ parameters.append(parseDict(argument));
 return parameters;
}

File file {str(::parameters())+".working", currentWorkingDirectory(), Flags(WriteOnly|Create|Truncate)};
#if UI
SimulationView app {::parameters(), move(file)};
#else
SimulationRun app {::parameters(), move(file)};
#endif
