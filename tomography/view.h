#include "widget.h"
#include "volume.h"
#include "matrix.h"
#include "project.h"
struct GLTexture;

struct Value {
    uint value;
    array<Widget*> widgets;
    Value(uint value) : value(value) {}
    Value& registerWidget(Widget* widget) { widgets << widget; return *this; }
    void render() { for(Widget* widget: widgets) widget->render(); }
};

struct SliceView : Widget {
    int3 size;
    const VolumeF* volume = 0;
    const CLVolume* clVolume = 0;
    const int upsampleFactor;
    Value& index;
    string name;

    static Value staticIndex;
    SliceView(const VolumeF& volume, const int upsampleFactor, Value& index=staticIndex, string name=""_) : size(volume.size), volume(&volume), upsampleFactor(upsampleFactor), index(index.registerWidget(this)), name(name) {}
    SliceView(const CLVolume& clVolume, const int upsampleFactor, Value& index=staticIndex, string name=""_) : size(clVolume.size), clVolume(&clVolume), upsampleFactor(upsampleFactor), index(index.registerWidget(this)), name(name) {}
    default_move(SliceView);
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    int2 sizeHint() override;
    void render() override;
};

struct VolumeView : Widget {
    const CLVolume& volume;
    const int3 size;
    const int upsampleFactor;
    Value& index;
    string name;

    static Value staticIndex;
    VolumeView(const CLVolume& volume, const int3 projectionSize, const int upsampleFactor, Value& index=staticIndex, string name=""_) : volume(volume), size(projectionSize), upsampleFactor(upsampleFactor), index(index.registerWidget(this)), name(name) {}
    default_move(VolumeView);
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    int2 sizeHint() override;
    void render() override;
};
