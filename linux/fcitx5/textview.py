#!/usr/bin/env python3
# Dev only: a GTK text view for test-session.sh. Writes what it holds to
# <file> on every change, and the inline composition to <file>.preedit.
#   textview.py <file>
import os
import sys

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk  # noqa: E402

path = sys.argv[1]


def write(name, text):
    with open(name + ".new", "w") as f:
        f.write(text)
    os.replace(name + ".new", name)


def changed(buffer):
    write(path, buffer.get_text(buffer.get_start_iter(),
                                buffer.get_end_iter(), False))


window = Gtk.Window(title="hp-fcitx5-test")
window.set_default_size(640, 240)
view = Gtk.TextView()
view.get_buffer().connect("changed", changed)
view.connect("preedit-changed", lambda view, text: write(path + ".preedit", text))
window.add(view)
window.connect("destroy", Gtk.main_quit)
window.show_all()
write(path, "")
Gtk.main()
