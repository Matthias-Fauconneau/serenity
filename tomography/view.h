#include "widget.h"
#include "volume.h"
#include "matrix.h"
#include "project.h"
struct GLTexture;

struct Value {
    uint value;
    array<Widget*> widgets;
    explicit Value(uint value) : value(value) {}
    Value& registerWidget(Widget* widget) { widgets << widget; return *this; }
    void render() { for(Widget* widget: widgets) widget->render(); }
};

struct SliceView : Widget {
    int3 size;
    const VolumeF* volume = 0;
    const CLVolume* clVolume = 0;
    const int upsampleFactor;
    Value& index;
    float max = 0;

    static Value staticIndex;
    SliceView(const VolumeF* volume, const int upsampleFactor, Value& index=staticIndex) : size(volume->size), volume(volume), upsampleFactor(upsampleFactor), index(index.registerWidget(this)) {}
    SliceView(const VolumeF& volume, const int upsampleFactor, Value& index=staticIndex) : SliceView(&volume, upsampleFactor, index) {}
    SliceView(const CLVolume* clVolume, const int upsampleFactor, Value& index=staticIndex, float max=0) : size(clVolume->size), clVolume(clVolume), upsampleFactor(upsampleFactor), index(index.registerWidget(this)), max(max) {}
    SliceView(const CLVolume& clVolume, const int upsampleFactor, Value& index=staticIndex) : SliceView(&clVolume, upsampleFactor, index) {}
    default_move(SliceView);
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    int2 sizeHint() override;
    void render() override;
};

struct VolumeView : Widget {
    const CLVolume& x;
    const Projection A;
    const int upsampleFactor;
    Value& index;
    float max = 0;

    static Value staticIndex;
    VolumeView(const CLVolume* x, const Projection& A, const int upsampleFactor, Value& index=staticIndex) : x(*x), A(A), upsampleFactor(upsampleFactor), index(index.registerWidget(this)) {}
    VolumeView(const CLVolume& x, const Projection& A, const int upsampleFactor, Value& index=staticIndex) : VolumeView(&x, A, upsampleFactor, index) {}

    default_move(VolumeView);
    bool mouseEvent(int2 cursor, int2 size, Event event, Button button) override;
    int2 sizeHint() override;
    void render() override;
};
