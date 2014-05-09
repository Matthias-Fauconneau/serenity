#include "widget.h"
#include "volume.h"
#include "matrix.h"

struct View : Widget {
    const VolumeF* volume;
    int2 lastPos = 0;
    // Shared between all views
    static bool renderVolume;
    static float index; // Z slice index (2D) or projection index along trajectory (3D)
    //static vec2 rotation;

    View(VolumeF* volume) : volume(volume) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint();
    void render(const Image& target) override;
};
