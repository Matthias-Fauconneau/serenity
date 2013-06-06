TARGET=$1
test -z $TARGET && export TARGET=/pool/users/$USER
./build.sh debug rock $TARGET
./build.sh fast rock $TARGET
#./build.sh release rock $TARGET
cp rock/rock $TARGET/rock.process
