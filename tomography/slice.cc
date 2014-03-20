/// \file slice.cc Displays volume as slices (or 3D visualization)
#include "slice.h"
#include "data.h"
#include "graphics.h"

float SliceView::sliceZ = 1./2;


bool SliceView::view(Volume&& volume, const string& name) {
    if(volume.sampleSize<1 || volume.sampleSize>6) return false;
    names << String(name);
    volumes << move(volume);
    //renderVolume = volumes.size>=2 && volumes[0].tiled() && volumes[0].sampleSize==1 && volumes[1].tiled() && volumes[1].sampleSize==1;
    return true;
}

bool SliceView::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    if(button==WheelDown||button==WheelUp) {
        int nextIndex = clip<int>(0,currentIndex+(button==WheelUp?1:-1),volumes.size-1);
        if(nextIndex == currentIndex) return false;
        currentIndex = nextIndex;
        if(currentIndex>=2) renderVolume=false;
        return true;
    }
    if(!button) return false;
    if(event==Press && button==RightButton) renderVolume=!renderVolume;
    assert(!renderVolume);
    Image image = slice(volumes[currentIndex], sliceZ, true, true, true);
    while(2*image.size()<=size) image=upsample(image);
    int2 centered = (size-image.size())/2;
    float z = clip(0.f, float(cursor.x-centered.x)/(image.size().x-1), 1.f);
    if(sliceZ != z) sliceZ = z;
    return true;
}

int2 SliceView::sizeHint() {
    assert_(volumes);
    const Volume& volume = volumes[currentIndex];
    int2 size = volume.sampleCount.xy(), margin = 2*volume.margin.xy();
    while(size<int2(1024)) size*=2, margin *= 2;
    return renderVolume ? 1024 : int2(align(4,size.x-margin.x),align(4,size.y-margin.y));
}

void SliceView::render(const Image& target) {
    assert(!renderVolume);
    Image image = slice(volumes[currentIndex], sliceZ, true, true, true);
    const Volume& volume = volumes[currentIndex];
    int2 imageSize = volume.sampleCount.xy();
    while(2*imageSize<=target.size()) image=upsample(image), imageSize*=2;
    int2 offset = (target.size()-image.size())/2;
    blit(target, offset, image);
}

#if INTERFACE
#include "volume-operation.h"
bool SliceView::view(const string& metadata, const string& name, const buffer<byte>& data) {
    return view(toVolume(metadata, data), name);
}
string SliceView::name() { return names[currentIndex]; }
#include "interface.h"
template struct Interface<View>::Factory<SliceView>;
#endif
