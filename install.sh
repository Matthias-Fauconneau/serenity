PREFIX=$1
test -z $PREFIX && export PREFIX=/pool/users/$USER
rm $PREFIX/rock.fast -f && ./build.sh fast rock $PREFIX && chmod +x $PREFIX/rock.fast &&
rm $PREFIX/rock.debug -f && ./build.sh debug rock $PREFIX  && chmod +x $PREFIX/rock.debug &&
rm $PREFIX/rock.release -f && ./build.sh release rock $PREFIX && chmod +x $PREFIX/rock.release &&
cp rock/rock $PREFIX/rock.process &&
./test.sh
