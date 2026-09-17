# Native Haiku build; needs only the development tools bundled with Haiku.
# `make check-host` builds and runs the portable core tests on Linux or Haiku.
.DEFAULT_GOAL := all
CXX ?= g++
BUILD ?= build-haiku
CPPFLAGS += -Isrc -Ivendor
CXXFLAGS ?= -O2 -g
CXXFLAGS += -std=c++17 -Wall -Wextra -Wno-multichar -Wno-unused-parameter
CORE = $(wildcard src/core/*.cpp)
UI = $(wildcard src/ui/*.cpp) src/main.cpp
CORE_OBJ = $(CORE:%.cpp=$(BUILD)/%.o)
UI_OBJ = $(UI:%.cpp=$(BUILD)/%.o)
LIBS = -lbe -ltracker -ltranslation
.PHONY: all check check-host check-native package clean icon
all: $(BUILD)/TurboChook
$(BUILD)/TurboChook: $(CORE_OBJ) $(UI_OBJ) resources/TurboChook.rdef resources/branding/turbochook-icon.hvif
	$(CXX) -o $@.new $(CORE_OBJ) $(UI_OBJ) $(LIBS)
	rc -o $(BUILD)/TurboChook.rsrc resources/TurboChook.rdef
	xres -o $@.new $(BUILD)/TurboChook.rsrc
	mimeset -f $@.new
	mv $@.new $@
$(BUILD)/%.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/turbochook_core_tests: $(CORE_OBJ) $(BUILD)/tests/CoreTests.o
	$(CXX) -o $@ $^ -lpthread
check-host: $(BUILD)/turbochook_core_tests
	$(BUILD)/turbochook_core_tests
check: check-host
$(BUILD)/turbochook_native_tests: $(CORE_OBJ) $(filter-out $(BUILD)/src/main.o,$(UI_OBJ)) $(BUILD)/tests/NativeTests.o
	$(CXX) -o $@ $^ $(LIBS)
check-native: $(BUILD)/turbochook_native_tests
	$(BUILD)/turbochook_native_tests
icon:
	python3 tools/make-icon.py resources/branding/turbochook-icon.hvif resources/branding/turbochook-icon-preview-host.png
package: all
	bash tools/package-haiku.sh
clean:
	rm -rf $(BUILD)
-include $(CORE_OBJ:.o=.d) $(UI_OBJ:.o=.d) $(BUILD)/tests/CoreTests.d $(BUILD)/tests/NativeTests.d
