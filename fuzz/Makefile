.PHONY: build run run-fast run-compat clean help

INCLUDE_DIR?=../include
PARSER?=fast
FUZZOUT?=/tmp/zsvfuzzout
FUZZBIN?=/tmp/zsvfuzz
FUZZ_CC?=afl-clang-fast

# Stop conditions (compose — whichever trips first wins):
#   TIME_LIMIT     = wall-clock seconds, unset = unlimited  (afl-fuzz -V)
#   STOP_ON_CRASH  = 1 to exit on first saved crash         (AFL_BENCH_UNTIL_CRASH)
STOP_ON_CRASH?=1

ifeq ($(PARSER),fast)
  FUZZ_PARSER_ID=3
else ifeq ($(PARSER),compat)
  FUZZ_PARSER_ID=255
else ifeq ($(PARSER),default)
  FUZZ_PARSER_ID=0
else
  $(error PARSER must be one of: fast, compat, default (got '$(PARSER)'))
endif

# macOS: System V shm is tiny (shmmax=4MB), crash reporter interferes,
# cpufreq sysfs doesn't exist. Bake in the workarounds so `make run` works
# without the caller having to export env vars.
UNAME_S:=$(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  AFL_ENV += AFL_MAP_SIZE=65536 AFL_SKIP_CPUFREQ=1 AFL_I_DONT_CARE_ABOUT_MISSING_CRASHES=1
endif

ifeq ($(STOP_ON_CRASH),1)
  AFL_ENV += AFL_BENCH_UNTIL_CRASH=1
endif

ifneq ($(strip $(TIME_LIMIT)),)
  AFL_OPTS += -V $(TIME_LIMIT)
endif

help:
	@echo "To run fuzzer:"
	@echo "  make run [PARSER=fast|compat|default] [TIME_LIMIT=<sec>] [STOP_ON_CRASH=0|1]"
	@echo ""
	@echo "Convenience targets:"
	@echo "  make run-fast    # fuzz the branchless SIMD parser (ZSV_MODE_DELIM_FAST)"
	@echo "  make run-compat  # fuzz the compat/standard parser (ZSV_MODE_DELIM)"
	@echo ""
	@echo "Stop conditions (compose — whichever trips first wins):"
	@echo "  TIME_LIMIT=<sec>        wall-clock limit (afl-fuzz -V); unset = unlimited"
	@echo "  STOP_ON_CRASH=1         stop on first saved crash (default; set =0 to disable)"
	@echo ""
	@echo "Requirements:"
	@echo "  afl-clang-fast and afl-fuzz must be installed"
	@echo "  zsv.h must be present in INCLUDE_DIR (default: ../include)"
	@echo ""

clean:
	rm -f $(FUZZBIN)
	rm -rf $(FUZZBIN).dSYM $(FUZZOUT)

build: $(FUZZBIN)

$(FUZZBIN): fuzz.c
	$(FUZZ_CC) -o $(FUZZBIN) -g3 -fsanitize=address,undefined -DFUZZ_PARSER=$(FUZZ_PARSER_ID) fuzz.c -I$(INCLUDE_DIR)

run: $(FUZZBIN)
	@echo "Output: $(FUZZOUT) (parser=$(PARSER)$(if $(strip $(TIME_LIMIT)), time_limit=$(TIME_LIMIT)s)$(if $(filter 1,$(STOP_ON_CRASH)), stop_on_crash=yes))"
	$(AFL_ENV) afl-fuzz $(AFL_OPTS) -i ../data/test -o $(FUZZOUT) $(FUZZBIN)

run-fast:
	$(MAKE) run PARSER=fast

run-compat:
	$(MAKE) run PARSER=compat
