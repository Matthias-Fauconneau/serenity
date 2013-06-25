PREFIX=$1
test -z $PREFIX && export PREFIX=/pool/users/$USER
./build.sh debug rock $PREFIX  &&
./build.sh fast rock $PREFIX &&
./build.sh release rock $PREFIX &&
cp rock/process $PREFIX/rock.process &&
./test.sh
