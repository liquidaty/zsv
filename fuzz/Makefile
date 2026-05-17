unexport ASAN_OPTIONS
export AFL_USE_ASAN=1
export AFL_USE_UBSAN=1

PREFIX ?= ..
INCLUDE_DIR = $(PREFIX)/include

CFLAGS = -g3 -O2 -march=native -fsigned-char \
         -DZSV_VERSION=\"fuzz\" -DNDEBUG -DNO_UTF8_CHECK \
         -DHAVE_MEMMEM -DHAVE___BUILTIN_EXPECT \
         -I $(INCLUDE_DIR)

TEST_CFLAGS = -g3 -O2 -march=native -fsigned-char \
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

# Corpus generation stages:
# 1. Filter raw corpus to only include files <= 1KB (ultra-fast seeds)
# 2. Use afl-cmin to minimize the number of files while preserving coverage
# 3. Use afl-tmin to minimize the size of each remaining file while preserving behavior
CORPUS_DIR_GENERATED = ./corpus-generated
CORPUS_DIR_FILTERED = $(CORPUS_DIR_GENERATED)/filtered
CORPUS_DIR_OPTIMIZED = $(CORPUS_DIR_GENERATED)/optimized
CORPUS_DIR_MINIMIZED = $(CORPUS_DIR_GENERATED)/minimized

DICT_FILE  = csv.dict
JOBS ?= $(shell nproc --ignore=1 2>/dev/null || echo 2)

help:
	@echo "Usage (run from inside the zsv repo fuzz/ directory):"
	@echo "  make run          [PREFIX=..]          # single AFL++ instance"
	@echo "  make run-parallel [PREFIX=..] [JOBS=N] # 1 master + N secondary"
	@echo "  make test-build   [PREFIX=..]          # clang+ASan build (no AFL++ needed)"
	@echo "  make test-run     [PREFIX=..]          # clang+ASan build + corpus run"
	@echo "  make watch-process                     # watch fuzzing progress"
	@echo "  make watch-process-summary             # watch fuzzing progress (summary only)"
	@echo "  make list-crashes                      # list crash files"
	@echo "  make clean                             # clean fuzzing binaries and output"
	@echo "  make clean-all                         # clean everything including generated corpus"
	@echo ""
	@echo "Notes:"
	@echo "  The corpus is generated automatically only if it does not exist"
	@echo "  or if the raw test data changes. Otherwise, it is safely skipped."
	@echo "  Based on raw corpus size, the corpus generation may take a while."
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
	@echo ""
	@echo "For dockerized fuzzing, run 'make help-dockerized-fuzzer' for usage details."

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
	@rm -rf $(CORPUS_DIR_FILTERED)
	@mkdir -p $(CORPUS_DIR_FILTERED)
	@find $(CORPUS_DIR_RAW) -type f -size -1025c -exec cp {} $(CORPUS_DIR_FILTERED)/ \;
	@echo "Filtered corpus saved to: $(CORPUS_DIR_FILTERED)"

	@echo "========================================================="
	@echo "STAGE 2: Optimizing corpus file count (afl-cmin)..."
	@echo "========================================================="
	@rm -rf $(CORPUS_DIR_OPTIMIZED)
	@mkdir -p $(CORPUS_DIR_OPTIMIZED)
	afl-cmin -e -i $(CORPUS_DIR_FILTERED) -o $(CORPUS_DIR_OPTIMIZED) -- $(FUZZBIN)
	@echo "Optimized corpus saved to: $(CORPUS_DIR_OPTIMIZED)"

	@echo "========================================================="
	@echo "STAGE 3: Minimizing remaining file sizes (afl-tmin)..."
	@echo "========================================================="
	@rm -rf $(CORPUS_DIR_MINIMIZED)
	@mkdir -p $(CORPUS_DIR_MINIMIZED)
	@find $(CORPUS_DIR_OPTIMIZED) -type f | xargs -P $(shell nproc) -I {} \
		sh -c 'afl-tmin -i "$$1" -o "$(CORPUS_DIR_MINIMIZED)/$$(basename "$$1")" -- $(FUZZBIN)' _ {}
	@echo "Minimized corpus saved to: $(CORPUS_DIR_MINIMIZED)"

corpus: $(FUZZBIN) $(CORPUS_DIR_MINIMIZED)

run: corpus
	@echo "Output: $(FUZZOUT)"
	afl-fuzz -i $(CORPUS_DIR_MINIMIZED) -x $(DICT_FILE) -o $(FUZZOUT) -- $(FUZZBIN)

run-parallel: corpus
	@echo "Launching AFL++ with $(JOBS) parallel jobs (1 master + $$(($(JOBS) - 1)) secondary)"
	@mkdir -p $(FUZZOUT)
	@afl-fuzz -M master -i $(CORPUS_DIR_MINIMIZED) -x $(DICT_FILE) -o $(FUZZOUT) -- $(FUZZBIN) &
	@for i in $$(seq 1 $$(($(JOBS) - 1))); do \
		afl-fuzz -S s$$i -i $(CORPUS_DIR_MINIMIZED) -x $(DICT_FILE) -o $(FUZZOUT) -- $(FUZZBIN) >/dev/null 2>&1 & \
	done; \
	wait

validate-output:
	@if [ ! -d $(FUZZOUT) ]; then \
		echo "Fuzzing output directory $(FUZZOUT) does not exist." && \
		echo "Please run 'make run' or 'make run-parallel' first." && \
		echo "Run 'make help' for more details." && \
		exit 1; \
	fi

watch-process: validate-output
	@watch -c afl-whatsup $(FUZZOUT)

watch-process-summary: validate-output
	@watch -c afl-whatsup -s $(FUZZOUT)

list-crashes: validate-output
	@find $(FUZZOUT) -type f -path "*/crashes/id:*"

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

clean:
	rm -f $(FUZZBIN) $(TESTBIN) .afl-tmin-temp-*
	rm -rf $(FUZZOUT)

clean-all: clean
	rm -rf $(CORPUS_DIR_GENERATED)

# dockerized fuzzer targets

help-dockerized-fuzzer:
	@echo "Usage (run from inside the zsv repo fuzz/ directory):"
	@echo "  make run-dockerized-fuzzer [ZSV_BRANCH=<branch>] [JOBS=N]  # clone/build/run dockerized fuzzer"
	@echo "  make stop-dockerized-fuzzer                                # stop all fuzzing containers"
	@echo "  make show-dockerized-fuzzer-logs                           # show logs for all containers"
	@echo "  make watch-dockerized-fuzzer-logs                          # tail logs for all containers"
	@echo "  make list-dockerized-fuzzer-crashes                        # list crash files from dockerized fuzzing"
	@echo "  make clean-dockerized-fuzzer                               # stop containers and remove logs"
	@echo "  make clean-all-dockerized-fuzzer [ZSV_BRANCH=<branch>]     # stop containers and remove fuzzer repo, logs, and image"
	@echo ""
	@echo "Notes:"
	@echo "  Requires 'docker', 'docker compose' and 'yq' commands."
	@echo "  ZSV_BRANCH is the git branch to fuzz (default: main)."
	@echo "  JOBS is the number of parallel fuzzing jobs to run (default: 2)."

DOCKERIZED_FUZZER_REPO_URL = https://github.com/matteoalba/fuzz-zsv
DOCKERIZED_FUZZER_DIR = zsv-fuzzer
DOCKERIZED_FUZZER_ENV_FILE = $(DOCKERIZED_FUZZER_DIR)/.env
DOCKERIZED_FUZZER_COMPOSE_FILE = $(DOCKERIZED_FUZZER_DIR)/docker-compose.yml
DOCKERIZED_FUZZER_LOGS_DIR = $(DOCKERIZED_FUZZER_DIR)/logs_local
DOCKERIZED_FUZZER_CRASHES_DIR = $(DOCKERIZED_FUZZER_LOGS_DIR)/crashes
ZSV_BRANCH = main
DOCKERIZED_FUZZER_IMAGE = $(DOCKERIZED_FUZZER_DIR)
DOCKERIZED_FUZZER_IMAGE_WITH_TAG = $(DOCKERIZED_FUZZER_IMAGE):$(ZSV_BRANCH)
run-dockerized-fuzzer:
	@echo "Running dockerized fuzzer '$(DOCKERIZED_FUZZER_IMAGE_WITH_TAG)' with $(JOBS) jobs..."
	@if [ ! -d "$(DOCKERIZED_FUZZER_DIR)" ]; then \
		git clone --depth=1 $(DOCKERIZED_FUZZER_REPO_URL) $(DOCKERIZED_FUZZER_DIR); \
	fi

	@echo "Updating $(DOCKERIZED_FUZZER_ENV_FILE)..."
	@sed -i "s|^TARGET_VERSION=.*|TARGET_VERSION=$(ZSV_BRANCH)|" $(DOCKERIZED_FUZZER_ENV_FILE)
	@sed -i "s|^FUZZER_JOBS=.*|FUZZER_JOBS=$(JOBS)|" $(DOCKERIZED_FUZZER_ENV_FILE)
	@echo "=== $(DOCKERIZED_FUZZER_ENV_FILE) STARTS ==="
	@cat "$(DOCKERIZED_FUZZER_ENV_FILE)"
	@echo "==== $(DOCKERIZED_FUZZER_ENV_FILE) ENDS ===="

	@echo "Updating $(DOCKERIZED_FUZZER_COMPOSE_FILE)..."
	@yq '.services.fuzzer.image = "$(DOCKERIZED_FUZZER_IMAGE_WITH_TAG)"' -i $(DOCKERIZED_FUZZER_COMPOSE_FILE)
	@yq 'del(.services.fuzzer.restart)' -i $(DOCKERIZED_FUZZER_COMPOSE_FILE)
	@echo "=== $(DOCKERIZED_FUZZER_COMPOSE_FILE) STARTS ==="
	@cat $(DOCKERIZED_FUZZER_COMPOSE_FILE)
	@echo "==== $(DOCKERIZED_FUZZER_COMPOSE_FILE) ENDS ===="

	# Ensure logs directory exists before starting containers to avoid potential issues with volume mounts
	@mkdir -p $(DOCKERIZED_FUZZER_LOGS_DIR)

	@echo "Building and running fuzzing containers for branch $(ZSV_BRANCH) with $(JOBS) jobs..."
	@(cd $(DOCKERIZED_FUZZER_DIR) && \
	docker compose build && \
	docker compose up -d --wait --scale fuzzer=$(JOBS))
	@echo "Dockerized fuzzer '$(DOCKERIZED_FUZZER_IMAGE_WITH_TAG)' started with $(JOBS) jobs."
	@echo "Run 'make watch-dockerized-fuzzer-logs' to watch logs."

stop-dockerized-fuzzer:
	@echo "Stopping all fuzzing containers..."
	@(cd $(DOCKERIZED_FUZZER_DIR) && docker compose down 2>/dev/null || true)

show-dockerized-fuzzer-logs:
	@echo "Showing logs for all fuzzing containers..."
	@(cd $(DOCKERIZED_FUZZER_DIR) && docker compose logs)

watch-dockerized-fuzzer-logs:
	@echo "Watching logs for all fuzzing containers..."
	@echo "Press Ctrl+C to stop watching logs."
	@(cd $(DOCKERIZED_FUZZER_DIR) && docker compose logs -f)

list-dockerized-fuzzer-crashes:
	@echo "Listing crash files from dockerized fuzzing containers..."
	@find $(DOCKERIZED_FUZZER_CRASHES_DIR) -type f -path '*/crash-*' 2>/dev/null

clean-dockerized-fuzzer: stop-dockerized-fuzzer
	rm -rf $(DOCKERIZED_FUZZER_LOGS_DIR)

clean-all-dockerized-fuzzer: clean-dockerized-fuzzer
	rm -rf $(DOCKERIZED_FUZZER_DIR)
	docker rmi -f $(DOCKERIZED_FUZZER_IMAGE_WITH_TAG) 2>/dev/null
	@if docker images --format '{{.Repository}}' $(DOCKERIZED_FUZZER_DIR) >/dev/null 2>&1; then \
		echo ""; \
		echo "Looks like the images for '$(DOCKERIZED_FUZZER_IMAGE)' still exist:"; \
		docker images $(DOCKERIZED_FUZZER_IMAGE); \
		echo ""; \
		echo "Run 'make clean-all-dockerized-fuzzer ZSV_BRANCH=<YOUR-BRANCH-HERE>' to clean them up."; \
		echo "Alternatively, remove them manually with 'docker rmi -f <IMAGE_ID>' if needed."; \
	fi

.PHONY: help build run run-parallel \
		validate-output watch-process watch-process-summary \
		list-crashes test-build test-run \
		clean clean-all \
		run-dockerized-fuzzer stop-dockerized-fuzzer \
		show-dockerized-fuzzer-logs watch-dockerized-fuzzer-logs \
		list-dockerized-fuzzer-crashes clean-dockerized-fuzzer
