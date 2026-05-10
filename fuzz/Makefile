.PHONY: build run clean help

INCLUDE_DIR=/usr/local/include
FUZZOUT=/tmp/zsvfuzzout
FUZZBIN=/tmp/zsvfuzz

help:
	@echo "To run fuzzer:"
	@echo "  make run [INCLUDE_DIR=/usr/local/include] [SIMD=1]"
	@echo ""
	@echo "Requirements:"
	@echo "  afl-clang-fast and afl-fuzz must be installed"
	@echo "  zsv.h must be present in INCLUDE_DIR"
	@echo ""

clean:
	rm -rf $(FUZZBIN) $(FUZZOUT) $(FUZZBIN).dSYM

build: $(FUZZBIN)


ifeq ($(SIMD),1)
  FUZZ_TEST_SIMD_FLAG=-DFUZZ_TEST_SIMD=1
else
  FUZZ_TEST_SIMD_FLAG=
endif
$(FUZZBIN): fuzz.c
	afl-clang-fast -o $(FUZZBIN) $(FUZZ_TEST_SIMD_FLAG) -g3 -fsanitize=address,undefined fuzz.c -I/usr/local/include

run: $(FUZZBIN)
	@echo "Output will be written to $(FUZZOUT)"
	afl-fuzz -i ../data/test -o $(FUZZOUT) $(FUZZBIN)

