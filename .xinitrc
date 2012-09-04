desktop &
for ((;;)) ; do
	taskbar
	pidof desktop || desktop &
	sleep 1
done
