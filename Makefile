# Unofficial native JLCooperPanelDaemon - build and test helpers.
#   make            build build/JLCooperPanelDaemon for this machine's architecture
#   make test       run the black-box tests (fake panel + fake Resolve; needs python3)
#   make test-uds   same, plus the listening-socket mode on /var/tmp/davinci_socket
#   make asan       build with AddressSanitizer + UBSan and run tests, a short fuzz run and the mapping-file fuzzer
#   make fuzz-mapping  only the mapping-file parser fuzzer
#   make clean

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall
BUILD    := build
BIN      := $(BUILD)/JLCooperPanelDaemon
SRC      := src/JLCooperPanelDaemon.cpp
PYTHON   ?= python3

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

test: $(BIN)
	$(PYTHON) tests/test_daemon.py $(BIN)
	$(PYTHON) tests/test_mapping.py $(BIN)

test-uds: $(BIN)
	$(PYTHON) tests/test_daemon.py $(BIN) --uds
	$(PYTHON) tests/test_mapping.py $(BIN)

$(BIN)-asan: $(SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $<

$(BUILD)/fuzz-mapping: tests/fuzz_mapping.cpp $(SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer -o $@ $<

fuzz-mapping: $(BUILD)/fuzz-mapping
	$(BUILD)/fuzz-mapping 20000 $(BUILD)/fuzz_mapping.tmp

asan: $(BIN)-asan
	ASAN_OPTIONS=detect_leaks=0 $(PYTHON) tests/test_daemon.py $(BIN)-asan
	ASAN_OPTIONS=detect_leaks=0 $(PYTHON) tests/test_mapping.py $(BIN)-asan
	$(PYTHON) tests/fuzz.py $(BIN)-asan 15
	$(MAKE) fuzz-mapping

clean:
	rm -rf $(BUILD)

.PHONY: all test test-uds asan fuzz-mapping clean
