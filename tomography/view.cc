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
    //for(uint y: range(image.size.y)) for(uint x: range(image.size.x)) assert_(isNumber(image(x,y)));
    while(image.size < this->target.size()) image = upsample(image);
    Image target = clip(this->target, (this->target.size()-image.size)/2+Rect(image.size));
    if(!target) return; // FIXME
    ImageF source = clip(image, (image.size-target.size())/2+Rect(target.size()));
    assert_(target.size() == source.size, target.size(), source.size, image.size, (this->target.size()-image.size)/2+Rect(image.size));
    float max = convert(target, source);
    float min = ::min(image.data);
    Text((volume?""_:clVolume->name)+"\n"_+str(min)+"\n"_+str(volume ? mean(*volume) : mean(*clVolume))+"\n"_+str(max),16,green).render(this->target, 0);
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
    float max = convert(clip(target, (target.size()-image.size)/2+Rect(image.size)), image);
    float min = ::min(image.data);
    Text(volume.name+"\n"_+str(min)+"\n"_+str(mean(volume))+"\n"_+str(max),16,green).render(this->target, 0);
    putImage(target);
}
