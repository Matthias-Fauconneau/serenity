 Serenity is written using [C++11][http://en.wikipedia.org/wiki/C++11]

### Configuring local development environment without a package manager
Install the scripts
 cp scripts/* $PREFIX/bin
 ln -s $PREFIX/lib $PREFIX/lib64

Fetch the package index
 index

Build and install gcc dependencies
 build gmp-5.1.2
 build mpfr-3.1.2
 build mpc-1.0.1

Build and install gcc
 build gcc-4.8.1 --disable-multilib --enable-languages="c,c++" --with-mpc=$PREFIX

Add $PREFIX/{bin,lib,include} to your system paths
 export PATH=$PREFIX/bin:$PATH
 export LD_LIBRARY_PATH=$PREFIX/lib:$LD_LIBRARY_PATH
 export CPPFLAGS=-I$PREFIX/include
 export LDFLAGS=-L$PREFIX/lib

Build and install an application
 sh build.sh fast test $PREFIX/bin

Optionnal: Build and install gdb (debugger)
 build gdb-7.6

Optionnal: Build and install Qt and Qt Creator (IDE)
 build-qt
 build qt-creator-2.8.0
 You can now open serenity.creator with Qt Creator
 The code can be browsed using F2 to follow symbols (Alt-Left to go back)

Optionnal: Build and install git
 build git-1.8.3.2
