export ASAN_OPTIONS=abort_on_error=1:detect_leaks=0:symbolize=0
export AFL_USE_ASAN=1
export AFL_USE_UBSAN=1

PREFIX ?= ..
INCLUDE_DIR = $(PREFIX)/include

CFLAGS = -g3 -O2 -fsigned-char \
         -DZSV_VERSION=\"fuzz\" -DNDEBUG -DNO_UTF8_CHECK \
         -DHAVE_MEMMEM -DHAVE___BUILTIN_EXPECT \
         -I $(INCLUDE_DIR)

TEST_CFLAGS = -g3 -O2 -fsigned-char \
              -fsanitize=address,undefined -fno-sanitize-recover=undefined \
              -fno-omit-frame-pointer \
              -DZSV_VERSION=\"fuzz\" -DNDEBUG -DNO_UTF8_CHECK \
              -DHAVE_MEMMEM -DHAVE___BUILTIN_EXPECT \
              -I $(INCLUDE_DIR)

SOURCES = fuzz.c

FUZZBIN = ./zsvfuzz
FUZZOUT = ./zsvfuzzout
TESTBIN = $(FUZZBIN)_test
CORPUS_DIR_RAW = ../data/test
CORPUS_DIR_OPTIMIZED = corpus-optimized
CORPUS_DIR_MINIMIZED = corpus-minimized
DICT_FILE  = csv.dict
JOBS ?= $(shell nproc --ignore=1 2>/dev/null || echo 2)

help:
	@echo "Usage (run from inside the zsv repo fuzz/ directory):"
	@echo "  make run          [PREFIX=..]          # single AFL++ instance"
	@echo "  make run-parallel [PREFIX=..] [JOBS=N] # 1 master + N secondary"
	@echo "  make test-build   [PREFIX=..]          # clang+ASan build (no AFL++ needed)"
	@echo "  make test-run     [PREFIX=..]          # clang+ASan build + corpus run"
	@echo "  make list-crashes                      # list crash files"
	@echo "  make clean                             # clean fuzzing binaries and output"
	@echo "  make clean-all                         # clean everything including generated corpus"
	@echo ""
	@echo "Notes:"
	@echo "  The generated corpus is generated automatically only if it does not exist"
	@echo "  or if the raw test data changes. Otherwise, it is safely skipped."
	@echo "  Based on raw corpus size, the corpus generation may take a while."
	@echo ""
	@echo "To check status for parallel fuzzing, run:"
	@echo "  afl-whatsup $(FUZZOUT)"
	@echo "or, for continuous updates, run with watch like this:"
	@echo "  watch -c afl-whatsup $(FUZZOUT)"
	@echo ""
	@echo "To reproduce a specific crash, run:"
	@echo "  $(FUZZBIN) < crash-file"
	@echo "or, with test binary with stderr logs, run:"
	@echo "  make test-build"
	@echo "and, run:"
	@echo "  $(TESTBIN) < crash-file"
	@echo ""
	@echo "Requirements (run/run-parallel): afl-clang-fast, afl-fuzz"
	@echo "Requirements (test-build/test-run): clang with ASan support"
	@echo "PREFIX must point to the zsv repo root (default: ..)"

ZSV_H = $(INCLUDE_DIR)/zsv.h
ZSV_H_IN = $(INCLUDE_DIR)/zsv.h.in
$(ZSV_H): $(ZSV_H_IN)
	sed 's/__ZSV_EXTRAS__DEFINE__//' < $(ZSV_H_IN) > $(ZSV_H)

$(FUZZBIN): $(SOURCES) $(ZSV_H)
	afl-clang-fast $(CFLAGS) $(SOURCES) -o $(FUZZBIN)

build: $(FUZZBIN)

$(CORPUS_DIR_MINIMIZED): $(wildcard $(CORPUS_DIR_RAW)/*)
	@echo "========================================================="
	@echo "STAGE 1: Isolating ultra-fast seeds strictly <= 1KB..."
	@echo "========================================================="
	@rm -rf $(CORPUS_DIR_OPTIMIZED).filtered
	@mkdir -p $(CORPUS_DIR_OPTIMIZED).filtered
	@# Find and copy ONLY files smaller than or equal to 1024 bytes
	find $(CORPUS_DIR_RAW) -type f -size -1025c -exec cp {} $(CORPUS_DIR_OPTIMIZED).filtered/ \;

	@echo "========================================================="
	@echo "STAGE 2: Optimizing corpus file count (afl-cmin)..."
	@echo "========================================================="
	@rm -rf $(CORPUS_DIR_OPTIMIZED)
	@mkdir -p $(CORPUS_DIR_OPTIMIZED)
	afl-cmin -i $(CORPUS_DIR_OPTIMIZED).filtered -o $(CORPUS_DIR_OPTIMIZED) -- $(FUZZBIN)

	@echo "========================================================="
	@echo "STAGE 3: Minimizing remaining file sizes (afl-tmin)..."
	@echo "========================================================="
	@rm -rf $(CORPUS_DIR_MINIMIZED)
	@mkdir -p $(CORPUS_DIR_MINIMIZED)
	find $(CORPUS_DIR_OPTIMIZED) -type f | xargs -P $(shell nproc) -I {} \
		sh -c 'afl-tmin -i "$$1" -o "$(CORPUS_DIR_MINIMIZED)/$$(basename "$$1")" -- $(FUZZBIN)' _ {}
	@echo "Optimized and minimized corpus saved to: $(CORPUS_DIR_MINIMIZED)"

corpus: $(FUZZBIN) $(CORPUS_DIR_MINIMIZED)

run: corpus
	@echo "Output: $(FUZZOUT)"
	afl-fuzz -i $(CORPUS_DIR_MINIMIZED) -x $(DICT_FILE) -o $(FUZZOUT) -- $(FUZZBIN)

run-parallel: corpus
	@echo "Launching AFL++ with $(JOBS) parallel jobs (1 master + $$(($(JOBS) - 1)) secondary)"
	@echo "Output: $(FUZZOUT)"
	@echo "Press Ctrl+C to stop fuzzing"
	@mkdir -p $(FUZZOUT)
	afl-fuzz -M master -i $(CORPUS_DIR_MINIMIZED) -x $(DICT_FILE) -o $(FUZZOUT) -- $(FUZZBIN) &
	for i in $$(seq 1 $$(($(JOBS) - 1))); do \
		afl-fuzz -S s$$i -i $(CORPUS_DIR_MINIMIZED) -x $(DICT_FILE) -o $(FUZZOUT) -- $(FUZZBIN) >/dev/null 2>&1 & \
	done; \
	wait

# Smoke-test (no AFL++ needed)
test-build: $(SOURCES)
	clang $(TEST_CFLAGS) $(SOURCES) -o $(TESTBIN)

test-run: test-build
	@echo "Testing corpus (raw): $(CORPUS_DIR_RAW)"
	@ok=0; ko=0; \
	for f in $(CORPUS_DIR_RAW)/*; do \
		[ -f "$$f" ] || continue; \
		if $(TESTBIN) < "$$f" 2>/dev/null; then ok=$$((ok+1)); \
		else ko=$$((ko+1)); echo "  CRASH: $$f"; fi; \
	done; \
	echo "Passed: $$ok / Failed: $$ko"

list-crashes:
	@if [ ! -d $(FUZZOUT) ]; then \
		echo "Fuzzing output directory $(FUZZOUT) does not exist." && \
		echo "Please run 'make run' or 'make run-parallel' first." && \
		echo "Run 'make help' for more details." && \
		exit 1; \
	fi
	find $(FUZZOUT) -type f -path "*/crashes/id:*"

clean:
	rm -f $(FUZZBIN) $(TESTBIN) .afl-tmin-temp-*
	rm -rf $(FUZZOUT)

clean-all: clean
	rm -rf $(CORPUS_DIR_OPTIMIZED).filtered $(CORPUS_DIR_OPTIMIZED) $(CORPUS_DIR_MINIMIZED)

.PHONY: help build run run-parallel test-build test-run clean clean-all
