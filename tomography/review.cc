#include "thread.h"
#include "variant.h"

struct Application {
    Application() {
        Folder results = "Results"_;
        map<String, array<Variant>> values;
        for(string name: results.list()) {
            if(name.contains('.')) continue;
            ref<string> parameters = {"Volume Size"_,"Grain Radius"_,"Projection Size"_,"Trajectory"_,"Rotations"_,"Photon Count"_,"Subsets size"_};
            array<string> arguments = split(name);
            for(uint i: range(parameters.size)) if(!values[parameters[i]].contains(arguments[i])) values[parameters[i]].insertSorted(Variant(copy(String(arguments[i]))));
        }
        log(values);
    }
} app;
