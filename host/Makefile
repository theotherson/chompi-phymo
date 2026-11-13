# Desktop builds of PHYMO (src/): WAV renders for listening, and analysis.
# Nothing here runs on the CHOMPI.
#
#   make -C host && host/render
CXX ?= clang++
CXXFLAGS ?= -std=c++14 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unknown-attributes
CORE := $(wildcard ../src/*.h ../src/fx/*.h ../src/fx/*/*.h)

all: render analyse string_test body_test modal_test fm_test chorus_test poly_test space_test arp_test output_test features_test midi_test seq_test strike_test bow_test mod_test preset_test vocal_test

render: render.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ render.cpp

analyse: analyse.cpp wav.h
	$(CXX) $(CXXFLAGS) -o $@ analyse.cpp

# preset_test: the panel itself, over shim/'s stand-ins for the CHOMPI's
# hardware (a copy, gen/panel.h, so its "hardware.h" is shim's: a file's
# own folder is searched first; its switches' names from the real one)
gen/swid.h: ../code/src/hardware.h
	mkdir -p gen && sed -n '/enum class SwId/,/^        };/p' $< > $@

gen/panel.h: ../code/src/panel.h
	mkdir -p gen && cp $< $@

preset_test: preset_test.cpp wav.h gen/panel.h gen/swid.h shim/hardware.h shim/temp_led_stuff.h ../code/src/preset.h $(CORE)
	$(CXX) $(CXXFLAGS) -iquote shim -iquote gen -iquote ../code/src -o $@ preset_test.cpp

mod_test: mod_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ mod_test.cpp

bow_test: bow_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ bow_test.cpp

strike_test: strike_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ strike_test.cpp

string_test: string_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ string_test.cpp

body_test: body_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ body_test.cpp

modal_test: modal_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ modal_test.cpp

vocal_test: vocal_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ vocal_test.cpp

fm_test: fm_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ fm_test.cpp

chorus_test: chorus_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ chorus_test.cpp

poly_test: poly_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ poly_test.cpp

space_test: space_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ space_test.cpp

arp_test: arp_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ arp_test.cpp

output_test: output_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ output_test.cpp

features_test: features_test.cpp $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ features_test.cpp

midi_test: midi_test.cpp ../code/src/preset.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ midi_test.cpp

seq_test: seq_test.cpp wav.h $(CORE)
	$(CXX) $(CXXFLAGS) -o $@ seq_test.cpp

clean:
	rm -f render analyse string_test chorus_test poly_test space_test arp_test output_test features_test midi_test seq_test seq_test preset_test
	rm -rf gen
