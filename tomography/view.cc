#include "view.h"
#include "text.h"
#include "operators.h"

Value SliceView::staticIndex = 0;

bool SliceView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button == WheelDown) { index.value = clip(0u, index.value-1, uint(this->size.z-1)); index.render(); return true; }
    if(button == WheelUp) { index.value = clip(0u, index.value+1, uint(this->size.z-1)); index.render(); return true; }
    if(button) { index.value = clip(0, int(cursor.x*(this->size.z-1)/(size.x-1)), int(this->size.z-1)); index.render(); return true; }
    return false;
}

int2 SliceView::sizeHint() { return upsampleFactor * this->size.xy(); }

void SliceView::render() {
    ImageF image = volume ? slice(*volume, index.value) : slice(*clVolume, index.value);
    while(image.size < this->target.size()) image = upsample(image);
    Image target = clip(this->target, (this->target.size()-image.size)/2+Rect(image.size));
    if(!target) return; // FIXME
    assert_(target.size() == image.size, target.size(), image.size);
    convert(target, image, 0);
    //Text(str(volume ? sum(*volume) : sum(*clVolume)),16,1).render(this->target, 0);
    putImage(target);
}

Value VolumeView::staticIndex = 0;

bool VolumeView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index.value = clip(0, int(cursor.x*(this->size.z-1)/(size.x-1)), int(this->size.z-1)); index.render(); return true; }
    return false;
}

int2 VolumeView::sizeHint() { return upsampleFactor * size.xy(); }

void VolumeView::render() {
    ImageF image = ImageF( size.xy() );
    project(image, volume, size.z, index.value);
    for(uint _unused i: range(log2(upsampleFactor))) image = upsample(image);
    Image target = clip(this->target, (this->target.size()-image.size)/2+Rect(image.size));
    if(!target) return; // FIXME
    assert_(target.size() == image.size, target.size(), image.size);
    convert(clip(target, (target.size()-image.size)/2+Rect(image.size)), image);
    //Text(str(sum(volume)),16,1).render(this->target, 0);
    putImage(target);
}
