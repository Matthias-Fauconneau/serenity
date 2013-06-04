TARGET=$1
test -n $TARGET || TARGET=/pool/users/$USER
./build.sh debug rock $TARGET/rock.debug
./build.sh fast rock $TARGET/rock.fast
cp files/rock $TARGET/rock.process
