#!/bin/sh
test -e /dev/shm/test || mkdir /dev/shm/test
cd /dev/shm/test
test -e berea-740 || ln -s /ptmp/berea-740 berea-740
ROCK="/dev/shm/fast/rock.fast berea-740 cylinder=512,512,250,250,749 threshold=30743 resolution=1.0"
$ROCK distribution histogram porosity pore-volume total-volume distribution-radius-connected-scaled histogram-radius-connected porosity-connected volume-connected volume-total &&
$ROCK ascii-connected cdl-connected &&
$ROCK porosity-connected-minimalRadius minimalRadius=5 &&
#$ROCK porosity-connected-minimalRadius minimalRadius={0..10} &&
echo "Successfully executed all tests"
