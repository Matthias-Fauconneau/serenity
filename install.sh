cd `dirname $0`
./build.sh $* /usr/local/bin && chmod +x /usr/local/bin/$1
cd -
