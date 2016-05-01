cd `dirname $0`
BUILD=fast ./build.sh $* /usr/local/bin #&& chmod +x /usr/local/bin/$1
cd -
