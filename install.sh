function die { 
	echo $*; exit 
}
APPS=$(grep -l "Application(" *.cc | cut -d. -f1)
test $1 && echo "$APPS" | grep -qx $1 || die "Available applications: " $APPS
BUILD=release 
TARGET=$1
make && killall -q $1; make install
