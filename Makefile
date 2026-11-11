# Haiku Pinyin. The engine, its tools and tests build anywhere; the input method
# add-on builds on Haiku, the IBus engine on Linux.
#   make            engine, hpcli, tests (+ the add-on on Haiku, the IBus
#                   engine on Linux when ibus-1.0 is found)
#   make data       build/data from sources/ (tools/fetch_sources.sh first)
#   make test       run the engine tests
#   make install    (Haiku) the add-on and data into ~/config/non-packaged
#   make ibus       (Linux) build/ibus-engine-haiku-pinyin
#   sudo make install-ibus      (Linux) into PREFIX (/usr), where IBus looks
#   linux/install-user.sh       (Linux) into ~/.local, no root needed
#   make mac        (macOS) build/HaikuPinyin.app, universal
#   make install-mac            (macOS) into ~/Library/Input Methods
#   make mac-release            (macOS) the signed, notarized download in dist/

CXX ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-unused-parameter
BUILD := build
OBJ := $(BUILD)/obj

ENGINE_SOURCES := $(wildcard engine/*.cpp)
ENGINE_OBJECTS := $(ENGINE_SOURCES:engine/%.cpp=$(OBJ)/%.o)
ENGINE := $(BUILD)/libhaikupinyin.a

HAIKU := $(shell uname -s | grep -q Haiku && echo yes)
ADDON := $(BUILD)/HaikuPinyin
ADDON_SOURCES := $(wildcard haiku/*.cpp)
ADDON_OBJECTS := $(ADDON_SOURCES:haiku/%.cpp=$(OBJ)/haiku_%.o)

VERSION := $(shell cat VERSION)
PKG_CONFIG ?= pkg-config
LINUX := $(shell uname -s | grep -q Linux && echo yes)
IBUS_FOUND := $(if $(LINUX),$(shell $(PKG_CONFIG) --exists ibus-1.0 && echo yes))
IBUS_ENGINE := $(BUILD)/ibus-engine-haiku-pinyin
PREFIX ?= /usr
LIBEXECDIR ?= $(PREFIX)/libexec
DATADIR ?= $(PREFIX)/share/haiku-pinyin
COMPONENTDIR ?= $(PREFIX)/share/ibus/component

all: $(ENGINE) $(BUILD)/hpcli $(BUILD)/engine_test $(if $(HAIKU),$(ADDON)) \
	$(if $(IBUS_FOUND),$(IBUS_ENGINE))

$(OBJ)/%.o: engine/%.cpp engine/*.h
	@mkdir -p $(OBJ)
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

$(ENGINE): $(ENGINE_OBJECTS)
	rm -f $@
	ar rcs $@ $^

$(BUILD)/hpcli: cli/hpcli.cpp $(ENGINE)
	$(CXX) $(CXXFLAGS) $< $(ENGINE) -o $@

$(BUILD)/engine_test: tests/engine_test.cpp $(ENGINE)
	$(CXX) $(CXXFLAGS) $< $(ENGINE) -o $@

data: $(BUILD)/data/pinyin.dict

$(BUILD)/data/pinyin.dict: tools/build_data.py
	python3 tools/build_data.py

test: $(BUILD)/engine_test data
	$(BUILD)/engine_test $(BUILD)/data

# The add-on resolves BInputServerMethod against the input_server itself.
$(OBJ)/haiku_%.o: haiku/%.cpp haiku/*.h engine/*.h
	@mkdir -p $(OBJ)
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

$(ADDON): $(ADDON_OBJECTS) $(ENGINE)
	$(CXX) -shared -o $@ $(ADDON_OBJECTS) $(ENGINE) -lbe \
		/boot/system/servers/input_server

install: all data
	haiku/install.sh

ibus: $(IBUS_ENGINE)

$(IBUS_ENGINE): linux/*.cpp linux/*.h engine/*.h $(ENGINE)
	$(CXX) $(CXXFLAGS) $$($(PKG_CONFIG) --cflags ibus-1.0) \
		-DHP_DATADIR='"$(DATADIR)"' -DHP_VERSION='"$(VERSION)"' \
		$(filter %.cpp,$^) $(ENGINE) $$($(PKG_CONFIG) --libs ibus-1.0) -pthread $(LDFLAGS) -o $@

# IBus only reads components from /usr/share/ibus/component (or
# $$IBUS_COMPONENT_PATH), so PREFIX defaults to /usr.
install-ibus: $(IBUS_ENGINE) data
	install -Dm755 $(IBUS_ENGINE) $(DESTDIR)$(LIBEXECDIR)/ibus-engine-haiku-pinyin
	install -d $(DESTDIR)$(DATADIR)
	install -m644 build/data/* linux/icon.svg $(DESTDIR)$(DATADIR)
	install -d $(DESTDIR)$(COMPONENTDIR)
	sed -e 's|@LIBEXECDIR@|$(LIBEXECDIR)|g' -e 's|@DATADIR@|$(DATADIR)|g' \
		-e 's|@VERSION@|$(VERSION)|g' linux/haiku-pinyin.xml.in \
		> $(DESTDIR)$(COMPONENTDIR)/haiku-pinyin.xml
	@echo "Installed. As yourself, run linux/enable.sh: it restarts IBus, adds"
	@echo "海酷拼音 Haiku Pinyin to GNOME's input sources and switches to it."

uninstall-ibus:
	rm -f $(DESTDIR)$(LIBEXECDIR)/ibus-engine-haiku-pinyin \
		$(DESTDIR)$(COMPONENTDIR)/haiku-pinyin.xml
	rm -rf $(DESTDIR)$(DATADIR)

# Fcitx5 (Linux): the same engine as a Fcitx5 input method addon. Needs
# Fcitx5's headers (libfcitx5core-dev libfcitx5config-dev libfcitx5utils-dev
# fcitx5-modules-dev on Debian/Ubuntu, fcitx5-devel on Fedora, fcitx5 on
# Arch); plain make builds it too when they're found.
#   make fcitx5                  build/libhaiku-pinyin.so
#   sudo make install-fcitx5     into Fcitx5's addon directory and PREFIX
FCITX5_MODULES := Fcitx5Core Fcitx5Config Fcitx5Utils
FCITX5_FOUND := $(if $(LINUX),$(shell $(PKG_CONFIG) --exists $(FCITX5_MODULES) && echo yes))
FCITX5_ADDON := $(BUILD)/libhaiku-pinyin.so
FCITX5_SOURCES := linux/fcitx5/HaikuPinyin.cpp linux/Common.cpp linux/Cloud.cpp
# Fcitx5 loads addons only from its own directory (or $$FCITX_ADDON_DIRS).
FCITX5_ADDONDIR ?= $(shell $(PKG_CONFIG) --variable=libdir Fcitx5Core)/fcitx5
FCITX5_DATADIR ?= $(PREFIX)/share/fcitx5
ICONDIR ?= $(PREFIX)/share/icons/hicolor

all: $(if $(FCITX5_FOUND),$(FCITX5_ADDON))

fcitx5: $(FCITX5_ADDON)

# C++20: newer Fcitx5 headers need it (std::span in 5.1.23); older ones
# build either way.
$(FCITX5_ADDON): $(FCITX5_SOURCES) linux/fcitx5/*.h linux/Common.h linux/Cloud.h \
		engine/*.h $(ENGINE)
	$(CXX) $(CXXFLAGS) -std=c++20 -fPIC -shared \
		$$($(PKG_CONFIG) --cflags $(FCITX5_MODULES)) \
		-DHP_DATADIR='"$(DATADIR)"' $(FCITX5_SOURCES) $(ENGINE) \
		$$($(PKG_CONFIG) --libs $(FCITX5_MODULES)) -pthread $(LDFLAGS) \
		-Wl,--no-undefined -Wl,--exclude-libs,ALL -o $@

install-fcitx5: $(FCITX5_ADDON) data
	install -Dm755 $(FCITX5_ADDON) $(DESTDIR)$(FCITX5_ADDONDIR)/libhaiku-pinyin.so
	install -d $(DESTDIR)$(DATADIR)
	install -m644 build/data/* linux/icon.svg $(DESTDIR)$(DATADIR)
	install -Dm644 linux/icon.svg $(DESTDIR)$(ICONDIR)/scalable/apps/haiku-pinyin.svg
	install -d $(DESTDIR)$(FCITX5_DATADIR)/addon $(DESTDIR)$(FCITX5_DATADIR)/inputmethod
	sed -e 's|@VERSION@|$(VERSION)|g' linux/fcitx5/addon.conf.in \
		> $(DESTDIR)$(FCITX5_DATADIR)/addon/haiku-pinyin.conf
	install -m644 linux/fcitx5/inputmethod.conf \
		$(DESTDIR)$(FCITX5_DATADIR)/inputmethod/haiku-pinyin.conf
	@echo "Installed. Restart Fcitx5 (fcitx5 -r) and add 海酷拼音 Haiku Pinyin"
	@echo "in fcitx5-configtool (Input Method > Chinese)."

uninstall-fcitx5:
	rm -f $(DESTDIR)$(FCITX5_ADDONDIR)/libhaiku-pinyin.so \
		$(DESTDIR)$(FCITX5_DATADIR)/addon/haiku-pinyin.conf \
		$(DESTDIR)$(FCITX5_DATADIR)/inputmethod/haiku-pinyin.conf \
		$(DESTDIR)$(ICONDIR)/scalable/apps/haiku-pinyin.svg
	rm -rf $(DESTDIR)$(DATADIR)

clean: clean-fcitx5
clean-fcitx5:
	rm -f $(FCITX5_ADDON)

# Packages for other distributions, built in Docker (linux-deb, below, makes
# the Debian/Ubuntu ones, the Fcitx5 addon's among them).
#   make linux-sources   dist/haiku-pinyin-data-<v>.tar.xz, a release asset
#                        the Arch and Fedora packages build from
#   make linux-arch      dist/arch/: PKGBUILD and .SRCINFO for the AUR, and
#                        the packages, installed and checked in a container
#   make linux-rpm       dist/fedora/: the spec, source rpm and rpms, the same
linux-sources: data
	linux/sources.sh

linux-arch: data
	linux/arch.sh

linux-rpm: data
	linux/rpm.sh

.PHONY: fcitx5 install-fcitx5 uninstall-fcitx5 clean-fcitx5 linux-sources \
	linux-arch linux-rpm

# The app bundle builds with clang and swiftc from a script: two
# architectures, Settings.swift, the data inside, signing.
mac: data
	macos/build.sh

install-mac: mac
	macos/install.sh

# The download: signed, notarized, in dist/ with the updater's latest.json.
mac-release: data
	macos/release.sh

# Linux packages for a release: arm64 and amd64 .deb, built in Docker.
linux-deb: data
	linux/deb.sh

# After the Mac, Linux and Haiku packages are in dist/: the signed apt
# repository and SHA256SUMS.asc.
sign-release:
	tools/sign-release.sh

clean:
	rm -rf $(OBJ) $(ENGINE) $(BUILD)/hpcli $(BUILD)/engine_test $(ADDON) \
		$(IBUS_ENGINE)

.PHONY: all data test install ibus install-ibus uninstall-ibus mac install-mac mac-release \
	linux-deb sign-release clean
