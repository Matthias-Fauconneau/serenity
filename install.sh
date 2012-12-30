function die { 
	echo $*; exit 
}
APPS=$(grep -l "application" *.cc | cut -d. -f1)
test $1 && echo "$APPS" | grep -qx $1 || die "Available applications: " $APPS
export BUILD=fast 
export TARGET=$1
make all && make install
