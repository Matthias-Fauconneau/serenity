#include "widget.h"
#include "volume.h"
#include "matrix.h"
#include "project.h"
struct GLTexture;

struct SliceView : Widget {
    const VolumeF* volume;
    const int upsampleFactor;
    uint& index;

    static uint staticIndex;
    SliceView(const VolumeF* volume, const int upsampleFactor, uint& index=staticIndex) : volume(volume), upsampleFactor(upsampleFactor), index(index) {}
    bool mouseEvent(const Image& target, int2 cursor, int2 size, Event event, Button button) override;
    int2 sizeHint() override;
    void render(const Image& target) override;
};

struct VolumeView : Widget {
    const VolumeF* volume;
    const ref<Projection>& projections;
    const int2 size;
    const int upsampleFactor;
    uint& index;

    static uint staticIndex;
    VolumeView(const VolumeF* volume, const ref<Projection>& projections, const int2 size, const int upsampleFactor, uint& index=staticIndex) : volume(volume), projections(projections), size(size), upsampleFactor(upsampleFactor), index(index) {}
    bool mouseEvent(const Image& target, int2 cursor, int2 size, Event event, Button button) override;
    int2 sizeHint() override;
    void render(const Image& target) override;
};
