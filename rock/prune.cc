#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Computes the unconnected and connected pore space volume versus pruning radius and the largest pruning radius keeping both Z faces connected
class(Prune, Tool) {
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, ResultManager& results) override {
        shared<Result> inputResult = results.getResult("maximum"_, arguments); // Keep this reference to prevent this input to be evicted from cache
        Volume input = toVolume(inputResult);
        int3 size=input.sampleCount-input.margin; int maximumRadius=min(min(size.x, size.y), size.z)/2;
        real totalVolume = parseScalar(results.getResult("volume-total"_, arguments)->data);
        NonUniformSample unconnectedVolume, connectedVolume;
        for(int r2: range(sq(maximumRadius))) { real r = sqrt((real)r2);
        //for(int r: range(maximumRadius)) { int r2=sq(r); // Faster testing
            Dict args = copy(arguments);
            args.insert(String("minimalSqRadius"_), r2);
            {args.at("floodfill"_) = 0;
                shared<Result> result = results.getResult("volume"_, args); // Pore space volume (in voxels)
                real relativeVolume = parseScalar(result->data) / totalVolume;
                log("unconnected\t", r, ftoa(relativeVolume,3));
                unconnectedVolume.insert(r, relativeVolume);
            }
            {args.at("floodfill"_) = 1;
                shared<Result> result = results.getResult("volume"_, args); // Pore space volume (in voxels)
                real relativeVolume = parseScalar(result->data) / totalVolume;
                log("connected\t",r, ftoa(relativeVolume,3));
                connectedVolume.insert(r, relativeVolume);
                if(!relativeVolume) break;
            }
        }
        outputs[0]->metadata = String("V(λ).tsv"_);
        outputs[0]->data = "#Pore space volume versus pruning radius\n"_ + toASCII(unconnectedVolume);
        outputs[1]->metadata = String("V(λ).tsv"_);
        outputs[1]->data = "#Pore space volume versus pruning radius\n"_ + toASCII(connectedVolume);
    }
};
