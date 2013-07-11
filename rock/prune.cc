#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Computes the unconnected and connected pore space volume versus pruning radius and the largest pruning radius keeping both Z faces connected
class(Prune, Tool) {
    string parameters() const override { return "path"_; }
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, Process& process) override {
        real resolution = parseScalar(process.getResult("resolution"_, arguments)->data);
        shared<Result> inputResult = process.getResult("crop"_, arguments); // Keep this reference to prevent this input to be evicted from cache
        Volume input = toVolume(inputResult);
        int3 size=input.sampleCount-input.margin; int maximumRadius=min(min(size.x, size.y), size.z)/2;
        real totalVolume = parseScalar(process.getResult("volume-total"_, arguments)->data);
        NonUniformSample unconnectedVolume, connectedVolume;
        real criticalRadius = 0;
        //for(int r2: range(sq(maximumRadius))) { real r = sqrt((real)r2);
        for(int r: range(maximumRadius)) { int r2=sq(r); // Faster testing
            Dict args = copy(arguments);
            args["minimalSqRadius"_]=r2;
            {args.at("floodfill"_) = 0;
                shared<Result> result = process.getResult("volume"_, args); // Pore space volume (in voxels)
                real relativeVolume = parseScalar(result->data) / totalVolume;
                log("unconnected\t", r, ftoa(relativeVolume,4));
                unconnectedVolume.insert(resolution*r, relativeVolume);
            }
            {args.at("floodfill"_) = 1;
                shared<Result> result = process.getResult("volume"_, args); // Pore space volume (in voxels)
                real relativeVolume = parseScalar(result->data) / totalVolume;
                log("connected\t",r, ftoa(relativeVolume,4));
                connectedVolume.insert(resolution*r, relativeVolume);
                if(!relativeVolume) break;
                criticalRadius = r;
            }
        }
        output(outputs, "unconnected(λ)"_, "V(λ [μm]).tsv"_, [&]{return "#Pore space volume versus pruning radius\n"_ + toASCII(unconnectedVolume);});
        output(outputs, "connected(λ)"_, "V(λ [μm]).tsv"_, [&]{return "#Pore space volume versus pruning radius\n"_ + toASCII(connectedVolume);});
        output(outputs, "critical-radius"_, "scalar"_, [&]{return toASCII(resolution*criticalRadius);});
    }
};
