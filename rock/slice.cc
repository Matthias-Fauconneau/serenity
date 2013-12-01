/// \file slice.cc Displays volume as slices (or 3D visualization)
#include "slice.h"
#include "volume-operation.h"
#include "display.h"
#include "render.h"

float SliceView::sliceZ = 1./2;

bool SliceView::view(const string& metadata, const string& name, const buffer<byte>& data) {
    Volume volume = toVolume(metadata, data);
    if(volume.sampleSize<1 || volume.sampleSize>6) return false;
    names << String(name);
    volumes << move(volume);
    //renderVolume = volumes.size>=2 && volumes[0].tiled() && volumes[0].sampleSize==1 && volumes[1].tiled() && volumes[1].sampleSize==1;
    return true;
}

string SliceView::name() { return names[currentIndex]; }

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
    if(renderVolume) {
        int2 delta = cursor-lastPos;
        lastPos = cursor;
        if(event != Motion && !(event==Press && button==RightButton)) return false;
        rotation += vec2(-2*PI*delta.x/size.x,2*PI*delta.y/size.y);
        rotation.y= clip(float(-PI),rotation.y,float(0)); // Keep pitch between [-PI,0]
    } else {
        Image image = slice(volumes[currentIndex], sliceZ, true, true, true);
        while(2*image.size()<=size) image=upsample(image);
        int2 centered = (size-image.size())/2;
        float z = clip(0.f, float(cursor.x-centered.x)/(image.size().x-1), 1.f);
        if(sliceZ != z) sliceZ = z;
    }
    return true;
}

int2 SliceView::sizeHint() {
    assert_(volumes);
    const Volume& volume = volumes[currentIndex];
    int2 size = volume.sampleCount.xy(), margin = 2*volume.margin.xy();
    while(size<int2(1024)) size*=2, margin *= 2;
    return renderVolume ? 1024 : int2(align(4,size.x-margin.x),align(4,size.y-margin.y));
}

void SliceView::render(int2 position, int2 size) {
    if(renderVolume) {
        assert_(position==int2(0) && size == framebuffer.size());
        mat3 view;
        view.rotateX(rotation.y); // pitch
        view.rotateZ(rotation.x); // yaw
        assert_(volumes.size>=2 && volumes[0].tiled() && volumes[0].sampleSize==1 && volumes[1].tiled() && volumes[1].sampleSize==1);
        ::render(framebuffer, volumes[0], volumes[1], view);
    } else {
        Image image = slice(volumes[currentIndex], sliceZ, true, true, true);
        const Volume& volume = volumes[currentIndex];
        int2 imageSize = volume.sampleCount.xy();
        while(2*imageSize<=size) image=upsample(image), imageSize*=2;
        int2 centered = position+(size-image.size())/2;
        blit(centered, image);
    }
}
