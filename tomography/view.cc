#include "view.h"

static uint projectionIndex = 0;
static float maxValue = 0;

bool ProjectionView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { projectionIndex = clip(0, int(cursor.x*(projections.size-1)/(size.x-1)), int(projections.size)); return true; }
    return false;
}

int2 ProjectionView::sizeHint() { return upsampleFactor * projections[projectionIndex].size(); }

void ProjectionView::render(const Image& target) {
    ImageF image = share(projections[projectionIndex]);
    for(uint unused i: range(log2(upsampleFactor))) image = upsample(image);
    //assert_(target.size() == image.size(), target.size(), image.size());
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}


bool VolumeView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { projectionIndex = clip(0, int(cursor.x*(projections.size-1)/(size.x-1)), int(projections.size)); return true; }
    return false;
}

int2 VolumeView::sizeHint() { return upsampleFactor * projections[projectionIndex].imageSize; }

void VolumeView::render(const Image& target) {
    ImageF image = ImageF( projections[projectionIndex].imageSize );
    project(image, volume, projections[projectionIndex]);
    for(uint unused i: range(log2(upsampleFactor))) image = upsample(image);
    //assert_(target.size() == image.size(), target.size(), image.size());
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}

/*bool DiffView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { projectionIndex = clip(0.f, float(cursor.x)/float(size.x-1), 1.f); return true; }
    return false;
}

int2 DiffView::sizeHint() {
    assert_(projections.sampleCount.xy()==sensorSize, projections.sampleCount.xy());
    return projections.sampleCount.xy();
}

void DiffView::render(const Image& target) {
    ImageF reprojection = ImageF( target.size() );
    project(reprojection, volume, Projection(volume.sampleCount, reprojection.size(), projectionIndex));
    ImageF source = slice(projections, projectionIndex*(projections.sampleCount.z-1));
    ImageF image = ImageF( target.size() );
    for(uint y: range(image.height)) for(uint x: range(image.width)) image(x,y) = abs(reprojection(x,y)-source(x,y));
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}*/

static float sliceIndex = 0;

bool SliceView::mouseEvent(int2 cursor, int2 size, Event, Button button) {
    if(button) { sliceIndex = clip(0.f, float(cursor.x)/float(size.x-1), 1.f); return true; }
    return false;
}

int2 SliceView::sizeHint() { return upsampleFactor * volume.sampleCount.xy(); }

void SliceView::render(const Image& target) {
    ImageF image = slice(volume, sliceIndex*(volume.sampleCount.z-1));
    while(image.size() < target.size()) image = upsample(image);
    convert(clip(target, (target.size()-image.size())/2+Rect(image.size())), image, maxValue);
}
