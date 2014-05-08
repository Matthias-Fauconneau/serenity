#include "thread.h"
#include "netcdf.h"
#include "volume.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"

struct Test {
    Test() {
        Map file = File("Data/LIN_PSS/block00000000.nc"_, home());
        NetCDF cdf( file );
        log(cdf.attributes, cdf.variables);
    }
} test;
