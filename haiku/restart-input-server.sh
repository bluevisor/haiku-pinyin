#!/bin/sh
# Restarts the input_server so it loads a newly installed add-on: the
# running one never reloads a replaced file. app_server starts a fresh
# input_server as soon as the old one exits.
#
# Each input_server adds its methods to Deskbar's keyboard menu, which
# survives the restart, so the old entries are removed first (or 海酷拼音
# shows up twice). Don't run this while a Deskbar menu is open: the new
# input_server waits on Deskbar while holding a lock its event thread
# needs, and keyboard and mouse freeze until it's restarted again.
desklink --remove=MethodReplicant >/dev/null
for team in $(ps | grep "[s]ervers/input_server" | awk '{print $(NF-3)}'); do
	kill "$team"
done
sleep 4
ps | grep -q "[s]ervers/input_server" && echo "input_server restarted"
