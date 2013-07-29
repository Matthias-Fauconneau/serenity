#include "process.h"
#include "volume-operation.h"
#include "sample.h"

/// Computes the unconnected and connected pore space volume versus pruning radius and the largest pruning radius keeping both Z faces connected
class(Prune, Tool) {
    string parameters() const override { return "path connect-pore"_; } //FIXME: get parameters from target (recursive dependency)
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, Process& process) override {
        real resolution = parseScalar(process.getResult("resolution"_, arguments)->data);
        shared<Result> inputResult = process.getResult("skeleton-tiled"_, arguments); // Keep this reference to prevent this input to be evicted from cache
        Volume input = toVolume(inputResult);
        int3 size=input.sampleCount-input.margin; int maximumRadius=min(16, min(min(size.x, size.y), size.z)/2);
        real totalVolume = parseScalar(process.getResult("volume-total"_, arguments)->data);
        NonUniformSample unconnectedVolume, connectedVolume;
        real criticalRadius = 0;
        //for(int r2: range(sq(maximumRadius))) { real r = sqrt((real)r2);
        const real precision = 2; int last=-1; for(int i: range(maximumRadius)) { int r2=round(sq(i/precision)); if(r2==last) continue; last=r2; real r=sqrt(real(r2));
            Dict args = copy(arguments);
            args.at("minimalSqRadius"_)=r2;
            {args.at("connect-pore"_) = 0;
                shared<Result> result = process.getResult("volume"_, args); // Pore space volume (in voxels)
                real relativeVolume = parseScalar(result->data) / totalVolume;
                log(args.at("connect-pore"_),"\t", r,"vx", resolution*r,"μm", ftoa(relativeVolume,4));
                unconnectedVolume.insert(resolution*r, relativeVolume);
            }
            {args.at("connect-pore"_) = 1;
                if(arguments.contains("connect-pore"_)) args.at("connect-pore"_) = copy(arguments.at("connect-pore"_));
                shared<Result> result = process.getResult("volume"_, args); // Pore space volume (in voxels)
                real relativeVolume = parseScalar(result->data) / totalVolume;
                log(args.at("connect-pore"_),"\t", r,"vx", resolution*r,"μm", ftoa(relativeVolume,4));
                connectedVolume.insert(resolution*r, relativeVolume);
                if(!relativeVolume) break;
                criticalRadius = r;
            }
        }
        output(outputs, "unconnected(λ)"_, "V(λ [μm]).tsv"_, [&]{return "#Pore space volume versus pruning radius\n"_ + toASCII(unconnectedVolume);});
        output(outputs, "connected(λ)"_, "V(λ [μm]).tsv"_, [&]{return "#Pore space volume versus pruning radius\n"_ + toASCII(connectedVolume);});
        output(outputs, "critical-radius"_, "scalar"_, [&]{return toASCII(criticalRadius);});
        output(outputs, "P(Sw)"_, "P [Pa](Sw).tsv"_, [&]{
            NonUniformSample P_Sw;
            const real gamma = 0.025 * 1e6; // Surface tension between oil and water (N/μm)
            for(auto point: connectedVolume) P_Sw.insertMulti((connectedVolume[0]-point.value)/connectedVolume[0], gamma/(2*max(1.,point.key)));
            P_Sw.insertMulti(1,0);
            return toASCII(P_Sw);
        });
    }
};
