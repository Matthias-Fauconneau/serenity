#!/bin/sh
function die { echo "Test failed"; exit -1; }
cd /dev/shm
test -e test || mkdir test
SRC=berea-740.tif
test -e $SRC || ln -s /ptmp/$SRC $SRC
fast/rock.fast $SRC source.cylinder=512,512,250,250,750 source.downsample threshold=30743 minimalSqRadius=3 floodfill=1 distribution-radius-scaled histogram-radius porosity volume volume-total cdl test || die
echo "Successfully executed all tests"
