#include "view.h"

bool SliceView::mouseEvent(const Image& target, int2 cursor, int2 size, Event, Button button) {
    if(button) { index = clip(0, int(cursor.x*(volume->size.z-1)/(size.x-1)), int(volume->size.z)); render(target); return true; }
    return false;
}

int2 SliceView::sizeHint() { return upsampleFactor * volume->size.xy(); }

void SliceView::render(const Image& target) {
    ImageF image = slice(*volume, index);
    while(image.size < target.size()) image = upsample(image);
    convert(clip(target, (target.size()-image.size)/2+Rect(image.size)), image, 0);
}


bool VolumeView::mouseEvent(const Image& target, int2 cursor, int2 size, Event, Button button) {
    if(button) { index = clip(0, int(cursor.x*(projections.size-1)/(size.x-1)), int(projections.size)); render(target); return true; }
    return false;
}

int2 VolumeView::sizeHint() { return upsampleFactor * size; }

void VolumeView::render(const Image& target) {
    ImageF image = ImageF( size );
    project(image, *volume, projections[index]);
    for(uint _unused i: range(log2(upsampleFactor))) image = upsample(image);
    convert(clip(target, (target.size()-image.size)/2+Rect(image.size)), image, 0);
}
