#!/bin/sh
function die { echo "Test failed"; exit -1; }
cd /dev/shm
test -e test || mkdir test
test -e berea-740 || ln -s /ptmp/berea-740 berea-740
#test -e berea-pruned.bmp || ln -s /ptmp/berea-pruned.bmp berea-pruned.bmp
fast/rock.fast berea-pruned.bmp source.cylinder=200,200,190,8,376 threshold=lorentz threshold lorentz-parameters test || die
fast/rock.fast berea-740 source.cylinder=512,512,250,250,750 source.downsample threshold=30743 minimalSqRadius=3 floodfill=1 distribution-radius-scaled histogram-radius porosity volume volume-total cdl test || die
echo "Successfully executed all tests"
