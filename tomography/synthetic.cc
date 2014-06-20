#include "phantom.h"
#include "file.h"
#include "data.h"
#include "image.h"
#include "project.h"
#include "layout.h"
#include "window.h"
#include "view.h"

const uint N = fromInteger(arguments()[0]);
Phantom phantom (16);
CLVolume volume (N, phantom.volume(N));
SliceView sliceView (volume, 512/N);
VolumeView volumeView (volume, Projection(volume.size, volume.size), 512/N);
HBox layout ({ &sliceView , &volumeView });
Window window (&layout, str(N));
