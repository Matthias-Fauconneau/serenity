#include "view.h"
#include "text.h"
#include "graphics.h"

Value SliceView::staticIndex (0);

bool SliceView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button == WheelDown) { index.value = clip(0u, index.value-1, uint(this->size.z-1)); index.render(); return true; }
    if(button == WheelUp) { index.value = clip(0u, index.value+1, uint(this->size.z-1)); index.render(); return true; }
    if(button) { index.value = clip(0, int((cursor.x*(this->size.z-1)+(size.x-1)/2)/(size.x-1)), int(this->size.z-1)); index.render(); return true; }
    return false;
}

int2 SliceView::sizeHint() { return ::max(int2(64), upsampleFactor * this->size.xy()); }

void SliceView::render() {
    ImageF image = volume ? slice(*volume, index.value) : slice(*clVolume, index.value);
    for(uint _unused i: range(log2(upsampleFactor))) image = upsample(image);
    Image target = clip(this->target, (this->target.size()-image.size)/2+Rect(image.size));
    if(!target) return; // FIXME
    ImageF source = clip(image, (image.size-target.size())/2+Rect(target.size()));
    assert_(target.size() == source.size, target.size(), source.size, image.size, (this->target.size()-image.size)/2+Rect(image.size));
    fill(this->target, Rect(this->target.size()), white);
    convert(target, source, this->max);
    string name = volume ? volume->name : clVolume->name;
    Text(name,16,green).render(this->target, 0);
    putImage(this->target);
}

Value VolumeView::staticIndex (0);

bool VolumeView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { index.value = clip(0, int((cursor.x*(A.count-1)+(size.x-1)/2)/(size.x-1)), int(A.count-1)); index.render(); return true; }
    return false;
}

int2 VolumeView::sizeHint() { return ::max(int2(64), upsampleFactor * A.projectionSize.xy()); }

void VolumeView::render() {
    ImageF image = ImageF(A.projectionSize.xy());
    project(image, A, x, index.value);
    for(uint _unused i: range(log2(upsampleFactor))) image = upsample(image);
    Image target = clip(this->target, (this->target.size()-image.size)/2+Rect(image.size));
    if(!target) { log("Empty clip"); return; } // FIXME
    assert_(target.size() == image.size, target.size(), image.size);
    fill(this->target, Rect(this->target.size()), white);
    convert(clip(target, (target.size()-image.size)/2+Rect(image.size)), image, this->max);
    Text(x.name,16,green).render(this->target, 0);
    putImage(this->target);
}
