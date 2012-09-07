function die { 
	echo $*; exit 
}
APPS=$(grep -l "Application(" *.cc | cut -d. -f1)
test $1 && echo "$APPS" | grep -qx $1 || die "Available applications: " $APPS
export BUILD=release 
export TARGET=$1
make && make install
#make && killall -q $1; make install
