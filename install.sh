PREFIX=$1
test -z $PREFIX && export PREFIX=/pool/users/$USER
rm $PREFIX/rock.debug && ./build.sh debug rock $PREFIX  && chmod +x $PREFIX/rock.debug &&
rm $PREFIX/rock.release && ./build.sh release rock $PREFIX && chmod +x $PREFIX/rock.release &&
rm $PREFIX/rock.fast && ./build.sh fast rock $PREFIX && chmod +x $PREFIX/rock.fast &&
cp rock/rock $PREFIX/rock.process &&
./test.sh
