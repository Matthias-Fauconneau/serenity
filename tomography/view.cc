#include "view.h"

static uint projectionIndex = 0;
static float maxValue = 0;

bool ProjectionView::mouseEvent(const Image& target, int2 cursor, int2 size, Event, Button button) {
    if(button) { projectionIndex = clip(0, int(cursor.x*(projections.size-1)/(size.x-1)), int(projections.size)); render(target); return true; }
    return false;
}

int2 ProjectionView::sizeHint() { return upsampleFactor * projections[projectionIndex].size(); }

void ProjectionView::render(const Image& target) {
    ImageF image = share(projections[projectionIndex]);
    for(uint _unused i: range(log2(upsampleFactor))) image = upsample(image);
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}


static float sliceIndex = 0;

bool SliceView::mouseEvent(const Image& target, int2 cursor, int2 size, Event, Button button) {
    if(button) { sliceIndex = clip(0.f, float(cursor.x)/float(size.x-1), 1.f); render(target); return true; }
    return false;
}

int2 SliceView::sizeHint() { return upsampleFactor * volume->sampleCount.xy(); }

void SliceView::render(const Image& target) {
    ImageF image = slice(*volume, sliceIndex*(volume->sampleCount.z-1));
    while(image.size() < target.size()) image = upsample(image);
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}


bool VolumeView::mouseEvent(const Image& target, int2 cursor, int2 size, Event, Button button) {
    if(button) { projectionIndex = clip(0, int(cursor.x*(projections.size-1)/(size.x-1)), int(projections.size)); render(target); return true; }
    return false;
}

int2 VolumeView::sizeHint() { return upsampleFactor * projections[projectionIndex].imageSize; }

void VolumeView::render(const Image& target) {
    ImageF image = ImageF( projections[projectionIndex].imageSize );
#if GL
    if(volume->sampleCount <= int3(256) && !gpuVolume) gpuVolume = unique<GLTexture>(*volume);
    if(gpuVolume) projectGL(image, gpuVolume, projections[projectionIndex]);
    else
#endif
    //project(image, *volume, projections[projectionIndex]);
    for(uint _unused i: range(log2(upsampleFactor))) image = upsample(image);
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}
