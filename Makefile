# Qingpin. The engine, its tools and tests build anywhere; the input method
# add-on builds on Haiku.
#   make            engine, qpcli, tests (+ the add-on on Haiku)
#   make data       build/data from sources/ (tools/fetch_sources.sh first)
#   make test       run the engine tests
#   make install    (Haiku) the add-on and data into ~/config/non-packaged

CXX ?= c++
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-unused-parameter
BUILD := build
OBJ := $(BUILD)/obj

ENGINE_SOURCES := $(wildcard engine/*.cpp)
ENGINE_OBJECTS := $(ENGINE_SOURCES:engine/%.cpp=$(OBJ)/%.o)
ENGINE := $(BUILD)/libqingpin.a

HAIKU := $(shell uname -s | grep -q Haiku && echo yes)
ADDON := $(BUILD)/Qingpin
ADDON_SOURCES := $(wildcard haiku/*.cpp)
ADDON_OBJECTS := $(ADDON_SOURCES:haiku/%.cpp=$(OBJ)/haiku_%.o)

all: $(ENGINE) $(BUILD)/qpcli $(BUILD)/engine_test $(if $(HAIKU),$(ADDON))

$(OBJ)/%.o: engine/%.cpp engine/*.h
	@mkdir -p $(OBJ)
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

$(ENGINE): $(ENGINE_OBJECTS)
	rm -f $@
	ar rcs $@ $^

$(BUILD)/qpcli: cli/qpcli.cpp $(ENGINE)
	$(CXX) $(CXXFLAGS) $< $(ENGINE) -o $@

$(BUILD)/engine_test: tests/engine_test.cpp $(ENGINE)
	$(CXX) $(CXXFLAGS) $< $(ENGINE) -o $@

data: $(BUILD)/data/qingpin.dict

$(BUILD)/data/qingpin.dict: tools/build_data.py
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

clean:
	rm -rf $(OBJ) $(ENGINE) $(BUILD)/qpcli $(BUILD)/engine_test $(ADDON)

.PHONY: all data test install clean
