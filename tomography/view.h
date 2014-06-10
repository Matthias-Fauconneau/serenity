#include "widget.h"
#include "volume.h"
#include "matrix.h"
#include "project.h"
struct GLTexture;

struct Value {
    uint value = 0;
    array<Widget*> widgets;
    Value& registerWidget(Widget* widget) { widgets << widget; return *this; }
    void render() { for(Widget* widget: widgets) widget->render(); }
};

struct SliceView : Widget {
    const VolumeF* volume;
    const int upsampleFactor;
    Value& index;

    static Value staticIndex;
    SliceView(const VolumeF* volume, const int upsampleFactor, Value& index=staticIndex) : volume(volume), upsampleFactor(upsampleFactor), index(index.registerWidget(this)) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    int2 sizeHint() override;
    void render() override;
};

struct VolumeView : Widget {
    const VolumeF* volume;
    const int3 size;
    const int upsampleFactor;
    Value& index;

    static Value staticIndex;
    VolumeView(const VolumeF* volume, const int3 projectionSize, const int upsampleFactor, Value& index=staticIndex) : volume(volume), size(projectionSize), upsampleFactor(upsampleFactor), index(index.registerWidget(this)) {}
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    int2 sizeHint() override;
    void render() override;
};
