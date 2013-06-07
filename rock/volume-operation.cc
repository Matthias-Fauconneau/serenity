#include "volume-operation.h"

class(Downsample, Operation), virtual VolumePass<uint16> {
    ref<byte> parameters() const override { return "downsample"_; }
    void execute(const Dict& args, VolumeT<uint16>& target, const Volume& source) override {
        downsample(target, source);
        for(uint times unused: range(((int)args.at("downsample"_)?:1)-1)) downsample(target, target);
    }
};

defineVolumePass(Tile, uint16, tile);

defineVolumePass(SquareRoot, uint8, squareRoot);
