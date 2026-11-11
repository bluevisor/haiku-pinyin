#!/usr/bin/env python3
# Dev only: chooses items of Fcitx5's status area for test-session.sh, as
# clicking them in KDE's input method panel (kimpanel) does. It takes the
# panel's D-Bus name, waits for Fcitx5 to switch to it, then asks.
#   panel.py <action name>...
import sys
import time

from gi.repository import Gio, GLib

bus = Gio.bus_get_sync(Gio.BusType.SESSION)
bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus",
              "org.freedesktop.DBus", "RequestName",
              GLib.Variant("(su)", ("org.kde.impanel", 0)), None,
              Gio.DBusCallFlags.NONE, -1, None)
time.sleep(1)
for name in sys.argv[1:]:
    bus.emit_signal(None, "/org/kde/impanel", "org.kde.impanel",
                    "TriggerProperty", GLib.Variant("(s)", ("/Fcitx/" + name,)))
    bus.flush_sync(None)
    time.sleep(0.3)
