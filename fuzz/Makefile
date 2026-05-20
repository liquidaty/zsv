# Makefile for AFL++ and libFuzzer fuzzing of libzsv

# =============
# AFL++ fuzzing
# =============

# https://aflplus.plus/docs/env_variables/
export AFL_SKIP_CPUFREQ=1
export AFL_AUTORESUME=1
export AFL_NO_FASTRESUME=1
export AFL_NO_CRASH_README=1
export AFL_IGNORE_UNKNOWN_ENVS=1
export AFL_IGNORE_SEED_PROBLEMS=1
# export AFL_PRINT_FILENAMES=1
# export AFL_NO_UI=1
export AFL_QUIET=1
# export AFL_DEBUG=1

UNAME_S = $(shell uname -s)

ifeq ($(CI),true)
  export AFL_NO_FORKSRV=1
  export AFL_NO_STARTUP_CALIBRATION=1
  export AFL_FAST_CAL=1
  export AFL_PIZZA_MODE=-1
else
# https://aflplus.plus/docs/install/#macos-on-x86_64-and-arm64
  ifeq ($(UNAME_S),Darwin)
    export AFL_NO_FORKSRV=1
  endif
endif

JOBS = 2
ifeq ($(shell command -v yq 2>/dev/null),)
  $(error 'yq' command is not available on this system. Please install)
endif
ifeq ($(shell command -v nproc 2>/dev/null),)
  $(error 'nproc' command is not available on this system. Please install 'coreutils'.)
endif
NPROC = $(shell nproc --ignore=1 2>/dev/null)
IS_JOBS_VALID = $(shell [ "$(JOBS)" -eq "$(JOBS)" ] 2>/dev/null && [ "$(JOBS)" -ge 2 ] && [ "$(JOBS)" -le "$(NPROC)" ] && echo 1 || echo 0)
ifneq ($(IS_JOBS_VALID),1)
  $(error Invalid JOBS value [$(JOBS)] for parallel fuzzing. Valid range: [min=2, max=$(NPROC)])
endif

CC = clang
AFL_CC = afl-clang-fast
ifeq ($(UNAME_S),Linux)
  AFL_CC = afl-clang-lto
endif

PREFIX ?= ..
INCLUDE_DIR = $(PREFIX)/include

CFLAGS = -g3 -O2 -march=native -fsigned-char \
         -DNDEBUG -DNO_UTF8_CHECK \
         -DHAVE_MEMMEM -DHAVE___BUILTIN_EXPECT \
         -I $(INCLUDE_DIR)

unexport ASAN_OPTIONS
unexport MSAN_OPTIONS

AFL_ASAN_ENV_VARS = AFL_USE_ASAN=1 AFL_USE_UBSAN=1 AFL_UBSAN_VERBOSE=1
AFL_MSAN_ENV_VARS = AFL_USE_MSAN=1 AFL_USE_UBSAN=1 AFL_UBSAN_VERBOSE=1

SAN_ENV_VARS =
ifndef SAN
  SAN = asan
endif
ifeq ($(SAN),asan)
  SAN_ENV_VARS = $(AFL_ASAN_ENV_VARS)
  REPRO_SAN_FLAGS = -fsanitize=address,undefined -fno-sanitize-recover=undefined
else ifeq ($(SAN),msan)
  SAN_ENV_VARS = $(AFL_MSAN_ENV_VARS)
  REPRO_SAN_FLAGS = -fsanitize=memory -fsanitize-memory-track-origins=2 -fno-sanitize-recover=all
else
  $(error Invalid SAN [$(SAN)]. Supported values are 'asan' and 'msan')
endif

ifndef HARNESS
  HARNESS = afl++
endif
ifeq ($(HARNESS),afl++)
  CFLAGS += -DZSV_VERSION=\"fuzz_$(HARNESS)_$(SAN)\" -DAFL_HARNESS
else ifeq ($(HARNESS),libfuzzer)
  CFLAGS += -DZSV_VERSION=\"fuzz_$(HARNESS)_$(SAN)\" -DLIBFUZZER_HARNESS -fsanitize=fuzzer
else
  $(error Invalid HARNESS [$(HARNESS)]. Supported harnesses are 'afl++' and 'libfuzzer')
endif

REPRO_CFLAGS = -g3 -O2 -march=native -fsigned-char \
              $(REPRO_SAN_FLAGS) \
              -fno-omit-frame-pointer \
              -DZSV_VERSION=\"repro_$(SAN)\" -DNDEBUG -DNO_UTF8_CHECK \
              -DHAVE_MEMMEM -DHAVE___BUILTIN_EXPECT \
              -I $(INCLUDE_DIR) -DREPRO

SOURCES = harness.c

AFL_DICT_FILE  = csv.dict

AFL_DIR = ./afl
AFL_FUZZ_BIN = $(AFL_DIR)/fuzz
AFL_REPRO_BIN = $(AFL_DIR)/repro_$(SAN)
AFL_OUTPUT_DIR = $(AFL_DIR)/out

# Corpus generation stages:
# 1. Filter raw corpus to only include files <= 1KB (ultra-fast seeds)
# 2. Use afl-cmin to minimize the number of files while preserving coverage
# 3. Use afl-tmin to minimize the size of each remaining file while preserving behavior
AFL_CORPUS_DIR_RAW = ../data/test
AFL_CORPUS_DIR_RAW_COUNT = $(shell find $(AFL_CORPUS_DIR_RAW) -type f | wc -l)
AFL_CORPUS_DIR_GENERATED = $(AFL_DIR)/corpus
AFL_CORPUS_DIR_FILTERED = $(AFL_CORPUS_DIR_GENERATED)/filtered
AFL_CORPUS_DIR_OPTIMIZED = $(AFL_CORPUS_DIR_GENERATED)/optimized
AFL_CORPUS_DIR_MINIMIZED = $(AFL_CORPUS_DIR_GENERATED)/minimized

# SAND: Decoupling Sanitization from Fuzzing for Low Overhead
# https://github.com/AFLplusplus/AFLplusplus/blob/stable/docs/SAND.md
ifndef SAND_MODE
  SAND_MODE = 0
endif
ifeq ($(SAND_MODE),1)
  AFL_FUZZ_BIN_ASAN = $(AFL_FUZZ_BIN)_asan
  AFL_FUZZ_BIN_MSAN = $(AFL_FUZZ_BIN)_msan
  AFL_SAND_MODE_ASAN_ENV_VARS = AFL_LLVM_ONLY_FSRV=1 $(AFL_ASAN_ENV_VARS)
  AFL_SAND_MODE_MSAN_ENV_VARS = AFL_LLVM_ONLY_FSRV=1 $(AFL_MSAN_ENV_VARS)
  AFL_SAND_MODE_RUN_FLAGS = -w $(AFL_FUZZ_BIN_ASAN) -w $(AFL_FUZZ_BIN_MSAN)
endif

.PHONY: help
help:
	@echo "AFL++ Fuzzing"
	@echo ""
	@echo "Usage (run from inside the zsv repo fuzz/ directory):"
	@echo "  make afl-run          [PREFIX=..]           # single AFL++ instance"
	@echo "  make afl-run-parallel [PREFIX=..] [JOBS=N]  # 1 main + N secondary"
	@echo "  make afl-stop                               # stop AFL++ fuzzing"
	@echo "  make afl-show-progress [ALL=0]              # show fuzzing progress"
	@echo "  make afl-show-progress-summary [ALL=0]      # show fuzzing progress (summary only)"
	@echo "  make afl-watch-progress [ALL=0]             # watch fuzzing progress"
	@echo "  make afl-watch-progress-summary [ALL=0]     # watch fuzzing progress (summary only)"
	@echo "  make afl-list-crashes                       # list crash files"
	@echo "  make afl-repro-crash [CRASH_FILE=<file>]    # reproduce specific crash"
	@echo "  make afl-repro-crash-all                    # reproduce all crashes"
	@echo "  make afl-repro-test-raw-corpus [PREFIX=..]  # clang build + raw corpus run"
	@echo "  make afl-clean                              # stop fuzzing and clean binaries and output"
	@echo "  make afl-clean-all                          # stop fuzzing and clean everything including corpus"
	@echo ""
	@echo "Notes:"
	@echo "  Run 'sudo afl-system-config' once to configure your system for optimal performance."
	@echo ""
	@echo "  PREFIX must point to the zsv repo root (default: ..)"
	@echo "  JOBS is the number of parallel fuzzing jobs to run (default: 2)"
	@echo "  ALL for show/watch commands controls whether to show all sessions. (default: 0 = current session only)"
	@echo "  TIMEOUT_SECONDS for run commands controls when to stop. (default: 0 = run indefinitely)"
	@echo "  STOP_ON_CRASH for run commands controls whether to stop on the first crash. (default: 0 = disabled)"
	@echo ""
	@echo "  SAN is to select Address Sanitizer (asan) or Memory Sanitizer (msan) (default: asan)"
	@echo "  By default, Undefined Behavior Sanitizer is automatically included for both."
	@echo ""
	@echo "  HARNESS is for selecting the AFL++ (afl++) or LibFuzzer (libfuzzer) harness (default: afl++)"
	@echo "  LibFuzzer has been observed to be faster and may be the default in future."
	@echo "  To fuzz with LibFuzzer harness, run:"
	@echo "    make afl-run HARNESS=libfuzzer"
	@echo ""
	@echo "  SAND_MODE enables the decoupled sanitization using multiple sanitizers. (default: 0 = disabled)"
	@echo "  With SAND approach, multiple sanitizers such as ASAN and MSAN can work together."
	@echo "  See https://github.com/AFLplusplus/AFLplusplus/blob/stable/docs/SAND.md for more details."
	@echo ""
	@echo "  The corpus is generated automatically only if it does not exist"
	@echo "  or if the raw test data changes. Otherwise, it is safely skipped."
	@echo "  Based on raw corpus size, the corpus generation may take a while."
	@echo ""
	@echo "  Be mindful of running 'afl-clean' or 'afl-clean-all' while fuzzing is in progress."
	@echo "  The fuzzing job(s) will be stopped first before clean to prevent any side effects."
	@echo ""
	@echo ""
	@echo "Dockerized Fuzzing (df) based on libFuzzer"
	@echo ""
	@echo "Usage (run from inside the zsv repo fuzz/ directory):"
	@echo "  make df-run [ZSV_BRANCH=<branch>] [JOBS=N]  # clone/build/run dockerized fuzzer"
	@echo "  make df-stop                                # stop all fuzzing containers"
	@echo "  make df-show-logs                           # show logs for all containers"
	@echo "  make df-watch-logs                          # tail logs for all containers"
	@echo "  make df-list-crashes                        # list crash files from dockerized fuzzing"
	@echo "  make df-clean                               # stop containers and remove logs"
	@echo "  make df-clean-all [ZSV_BRANCH=<branch>]     # stop containers and remove fuzzer repo, logs, and image"
	@echo ""
	@echo "Notes:"
	@echo "  ZSV_BRANCH is the git branch to fuzz (default: main)"
	@echo "  JOBS is the number of parallel fuzzing jobs to run (default: 2)"
	@echo ""

ZSV_H = $(INCLUDE_DIR)/zsv.h
ZSV_H_IN = $(INCLUDE_DIR)/zsv.h.in
$(ZSV_H): $(ZSV_H_IN)
	sed 's/__ZSV_EXTRAS__DEFINE__//' < $(ZSV_H_IN) > $(ZSV_H)

$(AFL_FUZZ_BIN): $(SOURCES) $(ZSV_H) afl-clean-all
	@mkdir -p $(AFL_DIR)
ifeq ($(SAND_MODE),0)
	$(SAN_ENV_VARS) $(AFL_CC) $(CFLAGS) $(SOURCES) -o $(AFL_FUZZ_BIN)
else
	$(AFL_CC) $(CFLAGS) $(SOURCES) -o $(AFL_FUZZ_BIN)
	$(AFL_SAND_MODE_ASAN_ENV_VARS) $(AFL_CC) $(CFLAGS) $(SOURCES) -o $(AFL_FUZZ_BIN_ASAN)
	$(AFL_SAND_MODE_ASAN_ENV_VARS) $(AFL_CC) $(CFLAGS) $(SOURCES) -o $(AFL_FUZZ_BIN_MSAN)
endif

afl-build: afl-clean-all $(AFL_FUZZ_BIN) afl-repro-build

$(AFL_CORPUS_DIR_MINIMIZED): $(wildcard $(AFL_CORPUS_DIR_RAW)/*)
	@echo "========================================================="
	@echo "STAGE 1: Isolating ultra-fast seeds strictly <= 1KB..."
	@echo "========================================================="
	@rm -rf $(AFL_CORPUS_DIR_FILTERED)
	@mkdir -p $(AFL_CORPUS_DIR_FILTERED)
	@find $(AFL_CORPUS_DIR_RAW) -type f -size -1025c -exec cp {} $(AFL_CORPUS_DIR_FILTERED)/ \;
	@RAW_COUNT=$$(find $(AFL_CORPUS_DIR_RAW) -type f | wc -l); \
	FILTERED_COUNT=$$(find $(AFL_CORPUS_DIR_FILTERED) -type f | wc -l); \
	echo "Filtered to $$FILTERED_COUNT files (<= 1KB) from raw corpus of $$RAW_COUNT files."
	@echo "Filtered corpus saved to: $(AFL_CORPUS_DIR_FILTERED)"

	@echo "========================================================="
	@echo "STAGE 2: Optimizing corpus file count (afl-cmin)..."
	@echo "========================================================="
	@rm -rf $(AFL_CORPUS_DIR_OPTIMIZED)
	@mkdir -p $(AFL_CORPUS_DIR_OPTIMIZED)
	afl-cmin -e -i $(AFL_CORPUS_DIR_FILTERED) -o $(AFL_CORPUS_DIR_OPTIMIZED) -T "all" -- $(AFL_FUZZ_BIN) -
	@FILTERED_COUNT=$$(find $(AFL_CORPUS_DIR_FILTERED) -type f | wc -l); \
	OPTIMIZED_COUNT=$$(find $(AFL_CORPUS_DIR_OPTIMIZED) -type f | wc -l); \
	echo "Optimized to $$OPTIMIZED_COUNT files from filtered corpus of $$FILTERED_COUNT files."
	@echo "Optimized corpus saved to: $(AFL_CORPUS_DIR_OPTIMIZED)"

	@echo "========================================================="
	@echo "STAGE 3: Minimizing remaining file sizes (afl-tmin)..."
	@echo "========================================================="
	@rm -rf $(AFL_CORPUS_DIR_MINIMIZED)
	@mkdir -p $(AFL_CORPUS_DIR_MINIMIZED)
	@find $(AFL_CORPUS_DIR_OPTIMIZED) -type f | xargs -P $(shell nproc) -I {} \
		sh -c 'afl-tmin -e -i "$$1" -o "$(AFL_CORPUS_DIR_MINIMIZED)/$$(basename "$$1")" -- $(AFL_FUZZ_BIN) -' _ {}
	@echo "Minimized corpus saved to: $(AFL_CORPUS_DIR_MINIMIZED)"

afl-corpus: $(AFL_FUZZ_BIN) $(AFL_CORPUS_DIR_MINIMIZED)

AFL_RUN_FLAGS = -u -x $(AFL_DICT_FILE) -i $(AFL_CORPUS_DIR_MINIMIZED) -o $(AFL_OUTPUT_DIR) $(AFL_SAND_MODE_RUN_FLAGS)
STOP_ON_CRASH = 0
ifneq ($(STOP_ON_CRASH),0)
  AFL_RUN_FLAGS += -I "pkill -INT afl-fuzz"
endif
TIMEOUT_SECONDS = 0
ifneq ($(TIMEOUT_SECONDS),0)
  AFL_RUN_FLAGS += -V $(TIMEOUT_SECONDS)
endif

afl-run: afl-corpus
	@afl-fuzz -P explore $(AFL_RUN_FLAGS) -- $(AFL_FUZZ_BIN)

afl-run-parallel: afl-corpus
ifneq ($(CI),true) # do not run main in CI (secondaries only)
	@afl-fuzz -M main -P explore -p explore $(AFL_RUN_FLAGS) -- $(AFL_FUZZ_BIN) &
endif
	for i in $$(seq 1 $$(($(JOBS) - 1))); do \
		case $$(($$i % 4)) in \
			0) STRATEGY="explore"; SCHEDULE="explore" ;; \
			1) STRATEGY="explore"; SCHEDULE="fast" ;; \
			2) STRATEGY="exploit"; SCHEDULE="explore" ;; \
			3) STRATEGY="exploit"; SCHEDULE="fast" ;; \
		esac; \
		afl-fuzz -S s$$i -P $$STRATEGY -p $$SCHEDULE $(AFL_RUN_FLAGS) -- $(AFL_FUZZ_BIN) >/dev/null 2>&1 & \
	done; \
	wait

afl-stop:
	@COUNT=$$(pgrep "afl-fuzz" | wc -l); \
	if [ "$$COUNT" -gt 0 ]; then \
		pkill -INT "afl-fuzz" 2>/dev/null || true; \
		echo "Stopped $$COUNT active 'afl-fuzz' processes."; \
	else \
		echo "No active 'afl-fuzz' processes found to stop."; \
	fi

afl-validate-output-dir:
	@if [ ! -d $(AFL_OUTPUT_DIR) ]; then \
		echo "Fuzzing output directory $(AFL_OUTPUT_DIR) does not exist." && \
		echo "Please run 'make afl-run' or 'make afl-run-parallel' first." && \
		echo "Run 'make help' for more details." && \
		exit 1; \
	fi

AFL_PROGRESS_FLAGS = -m
ifeq ($(ALL),1)
  AFL_PROGRESS_FLAGS += -d
endif

afl-show-progress: afl-validate-output-dir
	@afl-whatsup $(AFL_PROGRESS_FLAGS) $(AFL_OUTPUT_DIR)

afl-show-progress-summary: afl-validate-output-dir
	@afl-whatsup -s $(AFL_PROGRESS_FLAGS) $(AFL_OUTPUT_DIR)

afl-watch-progress: afl-validate-output-dir
	@watch -c afl-whatsup $(AFL_PROGRESS_FLAGS) $(AFL_OUTPUT_DIR)

afl-watch-progress-summary: afl-validate-output-dir
	@watch -c afl-whatsup -s $(AFL_PROGRESS_FLAGS) $(AFL_OUTPUT_DIR)

afl-list-crashes: afl-validate-output-dir
	@find $(AFL_OUTPUT_DIR) -type f -path "*/crashes/id:*"

$(AFL_REPRO_BIN): $(SOURCES) $(ZSV_H)
	@mkdir -p $(AFL_DIR)
	$(CC) $(REPRO_CFLAGS) $(SOURCES) -o $(AFL_REPRO_BIN)

afl-repro-build: $(AFL_REPRO_BIN)

afl-repro-crash: afl-repro-build
	@if [ -z "$$CRASH_FILE" ]; then \
		echo "usage: 'make $@ CRASH_FILE=<file>'"; \
		exit 1; \
	elif [ ! -f "$$CRASH_FILE" ]; then \
		echo "CRASH_FILE='$$CRASH_FILE' does not exist or is not a file."; \
		exit 1; \
	fi; \
	echo "Reproducing crash: $$CRASH_FILE"; \
	$(AFL_REPRO_BIN) "$$CRASH_FILE" || true

afl-repro-crash-all: afl-repro-build afl-validate-output-dir
	@CRASHES="$(shell make afl-list-crashes)"; \
	for crash in $$CRASHES; do \
		echo "Reproducing crash: $$crash"; \
		$(AFL_REPRO_BIN) "$$crash" || true; \
	done

afl-repro-test-raw-corpus: afl-repro-build
	@echo "Testing corpus (raw): $(AFL_CORPUS_DIR_RAW)"
	@ok=0; ko=0; \
	for f in $(AFL_CORPUS_DIR_RAW)/*; do \
		[ -f "$$f" ] || continue; \
		if $(AFL_REPRO_BIN) "$$f" 2>/dev/null; then ok=$$((ok+1)); \
		else ko=$$((ko+1)); echo "  CRASH: $$f"; fi; \
	done; \
	echo "Passed: $$ok / Failed: $$ko"

afl-clean: afl-stop
	rm -f $(AFL_FUZZ_BIN) $(AFL_REPRO_BIN) .afl-tmin-temp-*
	rm -f $(AFL_FUZZ_BIN_ASAN) $(AFL_FUZZ_BIN_MSAN)
	rm -rf $(AFL_OUTPUT_DIR)

afl-clean-all: afl-clean
	rm -rf $(AFL_DIR)/fuzz*dSYM $(AFL_DIR)/repro*dSYM
	rm -f $(AFL_DIR)/fuzz* $(AFL_DIR)/repro*
	rm -rf $(AFL_CORPUS_DIR_GENERATED)
	rm -rf $(AFL_DIR)

.PHONY: afl-build afl-run afl-run-parallel \
		afl-validate-output-dir afl-show-progress afl-show-progress-summary \
		afl-list-crashes afl-repro-crash afl-repro-crash-all \
		afl-repro-build afl-repro-test-raw-corpus afl-clean afl-clean-all

# ======================
# dockerized fuzzer (df)
# ======================

DF_REPO_URL = https://github.com/matteoalba/fuzz-zsv
DF_DIR = df
DF_ENV_FILE = $(DF_DIR)/.env
DF_COMPOSE_FILE = $(DF_DIR)/docker-compose.yml
DF_LOGS_DIR = $(DF_DIR)/logs_local
DF_CRASHES_DIR = $(DF_LOGS_DIR)/crashes
ZSV_BRANCH = main
DF_IMAGE = $(DF_DIR)
DF_IMAGE_WITH_TAG = $(DF_IMAGE):$(ZSV_BRANCH)

ifeq ($(shell uname),Darwin)
  SED_I=-i.bak
else
  SED_I=-i
endif

df-run:
	@echo "Running dockerized fuzzer '$(DF_IMAGE_WITH_TAG)' with $(JOBS) jobs..."
	@if [ ! -d "$(DF_DIR)" ]; then \
		git clone --depth=1 $(DF_REPO_URL) $(DF_DIR); \
	fi

	@echo "Updating $(DF_ENV_FILE)..."
	@sed $(SED_I) "s|^TARGET_VERSION=.*|TARGET_VERSION=$(ZSV_BRANCH)|" $(DF_ENV_FILE)
	@sed $(SED_I) "s|^FUZZER_JOBS=.*|FUZZER_JOBS=$(JOBS)|" $(DF_ENV_FILE)
	@echo "=== $(DF_ENV_FILE) STARTS ==="
	@cat "$(DF_ENV_FILE)"
	@echo "==== $(DF_ENV_FILE) ENDS ===="

	@echo "Updating $(DF_COMPOSE_FILE)..."
	@yq '.services.fuzzer.image = "$(DF_IMAGE_WITH_TAG)"' -i $(DF_COMPOSE_FILE)
	@yq 'del(.services.fuzzer.restart)' -i $(DF_COMPOSE_FILE)
	@echo "=== $(DF_COMPOSE_FILE) STARTS ==="
	@cat $(DF_COMPOSE_FILE)
	@echo "==== $(DF_COMPOSE_FILE) ENDS ===="

	# Ensure logs directory exists before starting containers to avoid potential issues with volume mounts
	@mkdir -p $(DF_LOGS_DIR)

	@echo "Building and running fuzzing containers for branch $(ZSV_BRANCH) with $(JOBS) jobs..."
	@(cd $(DF_DIR) && \
	docker compose build && \
	docker compose up -d --wait --scale fuzzer=$(JOBS))
	@echo "Dockerized fuzzer '$(DF_IMAGE_WITH_TAG)' started with $(JOBS) jobs."
	@echo "Run 'make df-watch-logs' to watch logs."

df-stop:
	@echo "Stopping all fuzzing containers..."
	@(cd $(DF_DIR) && docker compose down 2>/dev/null || true)

df-show-logs:
	@echo "Showing logs for all fuzzing containers..."
	@(cd $(DF_DIR) && docker compose logs)

df-watch-logs:
	@echo "Watching logs for all fuzzing containers..."
	@echo "Press Ctrl+C to stop watching logs."
	@(cd $(DF_DIR) && docker compose logs -f)

df-list-crashes:
	@echo "Listing crash files from dockerized fuzzing containers..."
	@find $(DF_CRASHES_DIR) -type f -path '*/crash-*' 2>/dev/null

df-clean: df-stop
	rm -rf $(DF_LOGS_DIR)

df-clean-all: df-clean
	rm -rf $(DF_DIR)
	docker rmi -f $(DF_IMAGE_WITH_TAG) 2>/dev/null
	@if docker images --format '{{.Repository}}' $(DF_DIR) >/dev/null 2>&1; then \
		echo ""; \
		echo "Looks like the images for '$(DF_IMAGE)' still exist:"; \
		docker images $(DF_IMAGE); \
		echo ""; \
		echo "Run 'make df-clean-all ZSV_BRANCH=<YOUR-BRANCH-HERE>' to clean them up."; \
		echo "Alternatively, remove them manually with 'docker rmi -f <IMAGE_ID>' if needed."; \
	fi

.PHONY: df-run df-stop df-show-logs df-watch-logs df-list-crashes df-clean
