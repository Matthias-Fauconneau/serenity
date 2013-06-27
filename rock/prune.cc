#include "process.h"
#include "volume-operation.h"
#include "analysis.h"

/// Computes the connected (and unconnected) pore space volume versus pruning radius and the largest pruning radius keeping both Z faces connected
class(Prune, Tool) {
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, ResultManager& results) override {
        shared<Result> inputResult = results.getResult("maximum"_, arguments); // Keep this reference to prevent this input to be evicted from cache
        Volume input = toVolume(inputResult);
        int3 size=input.sampleCount-input.margin; int maximumRadius=min(min(size.x, size.y), size.z)/2;
        real totalVolume = parseScalar(results.getResult("volume-total"_, arguments)->data);
        NonUniformSample connectedVolume;
        for(int r2: range(sq(maximumRadius))) {
        //for(int r: range(maximumRadius)) { int r2=sq(r);
            Dict args = copy(arguments);
            args.at("minimalSqRadius"_) = r2;
            shared<Result> result = results.getResult("volume"_, args); // Pore space volume (in voxels)
            real volume = parseScalar(result->data);
            log(sqrt((real)r2), ftoa(volume/totalVolume,3));
            connectedVolume.insert(sqrt((real)r2), volume/totalVolume);
            if(!volume) break;
        }
        outputs[0]->metadata = String("V(λ).tsv"_);
        outputs[0]->data = "#Connected pore space volume versus pruning radius\n"_ + toASCII(connectedVolume);
    }
};
