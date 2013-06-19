PREFIX=$1
test -z $PREFIX && export PREFIX=/pool/users/$USER
./build.sh debug rock $PREFIX  &&
./build.sh fast rock $PREFIX &&
./build.sh release rock $PREFIX &&
cp rock/rock $PREFIX/rock.process &&
./test.sh &&
echo "Successfully executed all tests"