#include "thread.h"
#include "netcdf.h"
#include "volume.h"
#include "view.h"
#include "project.h"
#include "window.h"

Map file (File("Data/tomo_floatFcbp_nc/block00000000.nc"_,home()));
NetCDF cdf (file);
ref<uint> dimensions = cdf.variables["tomo_float"_].dimensions.values;
VolumeF volume (int3(dimensions[2], dimensions[1], dimensions[0]), cdf.variables["tomo_float"_]);
View view ([](const ImageF& target, const mat4& projection) { project(target, volume, projection.scale(1./norm(volume.sampleCount))); });
Window window (&view, "Viewer"_);
