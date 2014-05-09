#include "thread.h"
#include "netcdf.h"

struct Test {
    Test() {
        //Map file = File("Data/LIN_PSS/block00000000.nc"_, home());
        Map file = File("Results/FBP/block00000000.nc"_, home());
        NetCDF cdf( file );
        log(cdf.attributes, cdf.variables);
    }
} test;
