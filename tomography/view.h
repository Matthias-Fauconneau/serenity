#include "widget.h"
#include "function.h"
#include "matrix.h"

struct View : Widget {
    typedef function<void(const ImageF& target, const mat4& projection)> Projection;
    Projection project;
    int2 lastPos = 0;
    static vec2 rotation; // Shared between all views

    View(Projection project) : project(project) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button);
    int2 sizeHint() { return int2(512); }
    void render(const Image& target) override;
};
