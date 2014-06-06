#include "view.h"

Value SliceView::staticIndex;

bool SliceView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index.value = clip(0, int(cursor.x*(volume->size.z-1)/(size.x-1)), int(volume->size.z)); index.render(); return true; }
    return false;
}

int2 SliceView::sizeHint() { return upsampleFactor * volume->size.xy(); }

void SliceView::render() {
    ImageF image = slice(*volume, index.value);
    while(image.size < target.size()) image = upsample(image);
    Image t = clip(target, (target.size()-image.size)/2+Rect(image.size));
    if(t.size() == image.size) convert(t, image, 0);
}

Value VolumeView::staticIndex;

bool VolumeView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index.value = clip(0, int(cursor.x*(projections.size-1)/(size.x-1)), int(projections.size)); index.render(); return true; }
    return false;
}

int2 VolumeView::sizeHint() { return upsampleFactor * size; }

void VolumeView::render() {
    ImageF image = ImageF( size );
    project(image, *volume, projections[index.value]);
    for(uint _unused i: range(log2(upsampleFactor))) image = upsample(image);
    convert(clip(target, (target.size()-image.size)/2+Rect(image.size)), image, 0);
}
