function die { 
	echo $*; exit 
}
APPS=$(grep -l "application" *.cc | cut -d. -f1)
test $1 && echo "$APPS" | grep -qx $1 || die "Available applications: " $APPS
export TARGET=$1
export BUILD=release
test $2 && export BUILD=$2
make install
