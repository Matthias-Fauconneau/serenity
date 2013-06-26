#!/bin/sh
cd /dev/shm
test -e test || mkdir test
test -e berea-740 || ln -s /ptmp/berea-740 berea-740
ROCK="fast/rock.fast berea-740 source.cylinder=512,512,250,250,749 threshold=30743 resolution=1.0"
$ROCK distribution-radius-scaled histogram-radius porosity volume volume-total minimalRadius=5 cdl test &&
echo "Successfully executed all tests"
