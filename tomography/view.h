#include "widget.h"
#include "volume.h"
#include "matrix.h"

struct View : Widget {
    const VolumeF* volume;
    bool renderVolume;

    View(VolumeF* volume, bool renderVolume) : volume(volume), renderVolume(renderVolume) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint();
    void render(const Image& target) override;
};

struct DiffView : Widget {
    const VolumeF* volume;
    const VolumeF* projections;

    DiffView(VolumeF* volume, VolumeF* projections) : volume(volume), projections(projections) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint();
    void render(const Image& target) override;
};
