#!/bin/sh
function die { echo "Test failed"; exit -1; }
cd /dev/shm
test -e test || mkdir test
test -e berea-740 || ln -s /ptmp/berea-740 berea-740
ROCK="fast/rock.fast berea-740 source.cylinder=512,512,250,250,749 threshold=30743 resolution=1.0"
$ROCK downsample minimalSqRadius=0 distribution-radius-scaled histogram-radius porosity volume volume-total cdl test || die
#for r in 1 2 3 4 5 6 7 8 9; do; $ROCK downsample minimalSqRadius=$(expr $r \* $r) distribution-radius-scaled histogram-radius porosity volume volume-total test || die; done
echo "Successfully executed all tests"
