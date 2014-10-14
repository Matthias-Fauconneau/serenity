 Serenity is written using [C++14][http://en.wikipedia.org/wiki/C++14] and requires a recent compiler (CLang 3.4, GCC 4.9).

Usage:
 Build to /var/tmp/serenity.clang++/serenity/$TARGET:
  sh build.sh $TARGET
 Without optimizations:
  sh build.sh debug $TARGET

 Build and install:
  sh build.sh $TARGET $PREFIX/bin

 with TARGET the name of any application source file (e.g test, player, dust).
