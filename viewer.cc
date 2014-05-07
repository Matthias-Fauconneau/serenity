#include "thread.h"
#include "netcdf.h"
#include "volume.h"
#include "view.h"
#include "project.h"
#include "window.h"

string path = "Data/tomo_floatFcbp_nc"_;
Map file (File(path+"/block00000000.nc"_,home()));
NetCDF cdf (file);
ref<uint> dimensions = cdf.variables["tomo_float"_].dimensions.values;
VolumeF volume (int3(dimensions[2], dimensions[1], dimensions[0]), cdf.variables["tomo_float"_]);
View view ( &volume );
Window window (&view, "Viewer"_);
