.PHONY: build run run-parallel test-build test-run clean help

export AFL_USE_ASAN=1
export AFL_USE_UBSAN=1

PREFIX     ?= ..

FUZZBIN    = /tmp/zsvfuzz
FUZZOUT    = /tmp/zsvfuzzout
TESTBIN    = /tmp/zsvfuzz_test
CORPUS_DIR = $(PREFIX)/data/test
DICT_FILE  = csv.dict
JOBS      ?= $(shell nproc --ignore=1 2>/dev/null || echo 2)

CFLAGS = -g3 -O2 -fsigned-char \
         -DZSV_VERSION=\"fuzz\" -DNDEBUG -DNO_UTF8_CHECK \
         -DHAVE_MEMMEM -DHAVE___BUILTIN_EXPECT \
         -I $(PREFIX)/include -I $(PREFIX)/src

TEST_CFLAGS = -g3 -O1 -fsigned-char \
              -fsanitize=address,undefined -fno-sanitize-recover=undefined \
              -fno-omit-frame-pointer \
              -DZSV_VERSION=\"fuzz\" -DNDEBUG -DNO_UTF8_CHECK \
              -DHAVE_MEMMEM -DHAVE___BUILTIN_EXPECT \
              -I $(PREFIX)/include -I $(PREFIX)/src

# Use dictionary only if the file exists
DICT_ARG = $(if $(wildcard $(DICT_FILE)),-x $(DICT_FILE),)

help:
	@echo "Usage (run from inside the zsv repo fuzz/ directory):"
	@echo "  make run          [PREFIX=..]          # single AFL++ instance"
	@echo "  make run-parallel [PREFIX=..] [JOBS=N] # 1 master + N secondary"
	@echo "  make test-build   [PREFIX=..]          # clang+ASan build (no AFL++ needed)"
	@echo "  make test-run     [PREFIX=..]          # clang+ASan build + corpus run"
	@echo ""
	@echo "Requirements (run/run-parallel): afl-clang-fast, afl-fuzz"
	@echo "Requirements (test-build/test-run): clang with ASan support"
	@echo "PREFIX must point to the zsv repo root (default: ..)"

# ── AFL++ build ───────────────────────────────────────────────────────────────
build: $(FUZZBIN)

$(FUZZBIN): fuzz.c
	afl-clang-fast $(CFLAGS) fuzz.c -o $(FUZZBIN)

# ── AFL++ run ─────────────────────────────────────────────────────────────────
run: $(FUZZBIN)
	@echo "Output: $(FUZZOUT)"
	afl-fuzz -i $(CORPUS_DIR) $(DICT_ARG) -o $(FUZZOUT) -- $(FUZZBIN)

run-parallel: $(FUZZBIN)
	@mkdir -p $(FUZZOUT)
	afl-fuzz -M master -i $(CORPUS_DIR) $(DICT_ARG) -o $(FUZZOUT) -- $(FUZZBIN) &
	@for i in $$(seq 1 $$(($(JOBS) - 1))); do \
		afl-fuzz -S s$$i -i $(CORPUS_DIR) $(DICT_ARG) -o $(FUZZOUT) -- $(FUZZBIN) &; \
	done

# ── Smoke-test (no AFL++ needed) ─────────────────────────────────────────────
test-build: fuzz.c
	clang $(TEST_CFLAGS) fuzz.c -o $(TESTBIN)

test-run: test-build
	@echo "Testing corpus: $(CORPUS_DIR)"
	@ok=0; ko=0; \
	for f in $(CORPUS_DIR)/*; do \
		[ -f "$$f" ] || continue; \
		if $(TESTBIN) < "$$f" 2>/dev/null; then ok=$$((ok+1)); \
		else ko=$$((ko+1)); echo "  CRASH: $$f"; fi; \
	done; \
	echo "Passed: $$ok / Failed: $$ko"

# ─────────────────────────────────────────────────────────────────────────────
clean:
	rm -f $(FUZZBIN) $(TESTBIN)
	rm -rf $(FUZZOUT)
