/// \file slice.cc Displays volume as slices
#include "slice.h"
#include "volume-operation.h"
#include "display.h"

float SliceView::sliceZ = 1./2;

bool SliceView::view(const string& metadata, const string& name, const buffer<byte>& data) {
    //if(volumes) return false;
    Volume volume = toVolume(metadata, data);
    if(!inRange(1u,volume.sampleSize,4u)) return false;
    names << String(name);
    volumes << move(volume);
    return true;
}

string SliceView::name() { return names[currentIndex]; }

bool SliceView::mouseEvent(int2 cursor, int2 size, Event unused event, Button button) {
    if(button==WheelDown||button==WheelUp) {
        int nextIndex = clip<int>(0,currentIndex+(button==WheelUp?1:-1),volumes.size-1);
        if(nextIndex == currentIndex) return false;
        currentIndex = nextIndex; //contentChanged();
        return true;
    }
    //if(event==Press) lastPos=cursor, lastZ = sliceZ;
    if(!button) return false;
    //float z = clip(0.f, lastZ+float(cursor.x-lastPos.x)/(size.x-1), 1.f);
    float z = clip(0.f, float(cursor.x)/(size.x-1), 1.f);
    if(sliceZ != z) { sliceZ = z; /*contentChanged();*/ }
    return true;
}

int2 SliceView::sizeHint() {
    assert_(volumes);
    const Volume& volume = volumes[currentIndex];
    return (volume.sampleCount-2*volume.margin).xy();
}

void SliceView::render(int2 position, int2 size) {
    const Volume& volume = volumes[currentIndex];
    Image image = slice(volume, sliceZ, true, true, true);
    //while(2*image.size()<=size) image=upsample(image);
    int2 centered = position+(size-image.size())/2;
    blit(centered, image);
}
