PREFIX=$1
test -z $PREFIX && export PREFIX=/pool/users/$USER
rm $PREFIX/rock.fast -f && ./build.sh rock fast $PREFIX && chmod +x $PREFIX/rock.fast &&
rm $PREFIX/rock.debug -f && ./build.sh rock debug $PREFIX  && chmod +x $PREFIX/rock.debug &&
#rm $PREFIX/rock.release -f && ./build.sh rock release $PREFIX && chmod +x $PREFIX/rock.release &&
cp rock/rock $PREFIX/rock.process &&
./test.sh
