PREFIX=$1
test -z $PREFIX && export PREFIX=/pool/users/$USER
./build.sh debug rock $PREFIX  && chmod +x $PREFIX/rock.debug
./build.sh release rock $PREFIX && chmod +x $PREFIX/rock.release
./build.sh fast rock && cp /dev/shm/fast/rock.fast $PREFIX/rock.fast
cp rock/rock $PREFIX/rock.process &&
./test.sh
