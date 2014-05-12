#include "widget.h"
#include "volume.h"
#include "matrix.h"
#include "project.h"

struct ProjectionView : Widget {
    const ref<ImageF>& projections;
    const int upsampleFactor;

    ProjectionView(const ref<ImageF>& projections, const int upsampleFactor) : projections(projections), upsampleFactor(upsampleFactor) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint();
    void render(const Image& target) override;
};

struct VolumeView : Widget {
    const VolumeF& volume;
    const ref<Projection>& projections;
    const int upsampleFactor;

    VolumeView(const VolumeF& volume, const ref<Projection>& projections, const int upsampleFactor) : volume(volume), projections(projections), upsampleFactor(upsampleFactor) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint();
    void render(const Image& target) override;
};

struct SliceView : Widget {
    const VolumeF& volume;
    const int upsampleFactor;

    SliceView(const VolumeF& volume, const int upsampleFactor) : volume(volume), upsampleFactor(upsampleFactor) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint();
    void render(const Image& target) override;
};

/*struct DiffView : Widget {
    const VolumeF& volume;
    const VolumeF& projections;

    DiffView(const VolumeF& volume, const VolumeF& projections) : volume(volume), projections(projections) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint();
    void render(const Image& target) override;
};*/
