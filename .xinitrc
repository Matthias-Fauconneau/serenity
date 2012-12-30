xkbcomp ~/.xkm $DISPLAY
taskbar &
for ((;;)) ; do
	desktop
	pidof taskbar || taskbar &
	sleep 1
done
