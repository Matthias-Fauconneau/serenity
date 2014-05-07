#include "widget.h"
#include "volume.h"
#include "matrix.h"

struct View : Widget {
    const VolumeF* volume;
    int2 lastPos = 0;
    static vec2 rotation; static float sliceZ; // Shared between all views
    bool renderVolume = false;

    View(VolumeF* volume) : volume(volume) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint() { return int2(512); }
    void render(const Image& target) override;
};
