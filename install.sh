PREFIX=$1
test -z $PREFIX && export PREFIX=/pool/users/$USER
rm $PREFIX/rock.fast -f && ./build.sh rock fast $PREFIX && chmod +x $PREFIX/rock.fast &&
rm $PREFIX/rock.debug -f && ./build.sh rock debug $PREFIX  && chmod +x $PREFIX/rock.debug &&
cp rock/rock $PREFIX/rock.process &&
./test.sh
