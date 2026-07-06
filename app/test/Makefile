# Makefile for use with GNU make

# Tests assert default (non-agent) output formats, so neutralize AI_AGENT (which
# the harness running these tests may set) for all recipes. Tests that exercise
# the AI_AGENT=TOON default set it explicitly per command (e.g. `AI_AGENT=1 ...`).
unexport AI_AGENT

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_DIR:=$(shell basename "${THIS_MAKEFILE_DIR}")
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

CONFIGFILE ?= ../../config.mk
ifneq ($(QUIET),1)
  $(info # Using config file ${CONFIGFILE})
endif
include ${CONFIGFILE}

CONFIGFILEPATH=$(shell ls ${CONFIGFILE} >/dev/null 2>/dev/null && realpath ${CONFIGFILE})
ifeq ($(CONFIGFILEPATH),)
  $(error Config file ${CONFIGFILE} not found)
endif

CC ?= cc

WIN=
ifeq ($(WIN),)
  WIN=0
  ifneq ($(findstring w64,$(CC)),) # e.g. mingw64
    WIN=1
  endif
endif

ifeq ($(DEBUG),1)
  DBG_SUBDIR+=dbg
else
  DBG_SUBDIR+=rel
endif

ifeq ($(WIN),0)
  BUILD_SUBDIR=$(shell uname)/${DBG_SUBDIR}
  EXE=
else
  BUILD_SUBDIR=win/${DBG_SUBDIR}
  EXE=.exe
endif

export THIS_LIB_BASE=$(shell cd ../.. && pwd)
CCBN=$(shell basename ${CC})
BUILD_DIR=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}
export TMP_DIR=${THIS_LIB_BASE}/tmp
TEST_DATA_DIR=${THIS_LIB_BASE}/data

ifeq ($(WIN),1)
  TEST_DATA_DIR=./../../data
  TMP_DIR=./../../tmp
endif

SOURCES=count echo count-pull select select-pull sql 2json serialize flatten pretty desc stack 2db 2tsv jq compare
ifneq ($(ZSV_EXTRAS),)
  SOURCES+=overwrite
endif
# 2toon exists only when TOON features are enabled (configure --disable-toon omits it)
ifneq ($(ZSV_NO_TOON),1)
  SOURCES+=2toon
  # test-compare's --toon equivalence check pipes through 2toon (empty under --disable-toon)
  COMPARE_TOON_DEP=${BUILD_DIR}/bin/zsv_2toon${EXE}
endif
TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${SOURCES}))

TESTS=$(addprefix test-,${SOURCES}) test-blank-leading-rows test-rm test-mv test-2json-help test-paste test-flatten-2 test-flatten-dupcol
ifneq ($(ZSV_NO_TOON),1)
  TESTS+=test-2toon-help
endif

ifeq ($(ZSVSHEET_BUILD),1)
  SOURCES+=sheet
  CFLAGS+=-DZSVSHEET_BUILD
  CFLAGS+=${CFLAGS_NCURSES}
  LDFLAGS+=${LDFLAGS_NCURSES}
  TESTS+=test-sheet test-sheet-subcommand
endif

ifneq ($(ZSV_NO_PARALLEL),1)
  TESTS+=test-parallel
endif
TESTS+=test-check
TESTS+=test-rename-select
TESTS+=test-quoted-nonstandard
# The redline tests pin output determinism via SOURCE_DATE_EPOCH and exercise the unified
# `cli` / `--help-topic` routing. emscripten's getenv does not see host env vars (only a
# minimal built-in ENV), and that multi-binary orchestration is not set up in the emcc/wasm
# test sandbox, so register these on native builds only. The --json-redline feature is plain
# C and is covered on every native platform (cf. the CROSS_COMPILING guard below).
ifneq ($(CCBN),emcc)
TESTS+=test-compare-redline
TESTS+=test-compare-redline-v2
# test-2db-csv pipes zsv_2json | zsv_2db and orchestrates multiple standalone
# binaries; like the redline tests, this multi-binary orchestration is not set
# up in the emcc/wasm sandbox (binaries are *.em.js run via node, not directly
# executable). The CSV->DB path is plain C and is covered on every native build.
TESTS+=test-2db-csv
# Uniform unrecognized-option rejection across commands; execs the standalone
# binaries directly, so native-only (like the redline/2db-csv orchestration above).
TESTS+=test-unrecognized-option
endif

# Security regression test - only run when not cross-compiling
ifneq ($(CROSS_COMPILING),1)
  TESTS+=test-vuln-cve-underflow
  # ASan-instrumented regression for the zsv_finish->cell_dl OOB. Skipped
  # if SAN_CC (clang+sanitizers) is unavailable; build target itself runs
  # the sanitizer.
  ifneq ($(shell command -v clang 2>/dev/null),)
    TESTS+=test-vuln-finish-cell-dl-oob
    TESTS+=test-vuln-fast-nonstandard-oob
  endif
endif

COLOR_NONE=\033[0m
COLOR_GREEN=\033[1;32m
COLOR_RED=\033[1;31m
COLOR_BLUE=\033[1;34m
COLOR_PINK=\033[1;35m
COLOR_YELLOW=\033[1;33m

TEST_INIT=mkdir -p ${TMP_DIR} && printf "${COLOR_PINK}$@: ${COLOR_NONE}\n"
TEST_SKIP=printf "${COLOR_BLUE}$@: ${COLOR_YELLOW}Skipped${COLOR_NONE}\n"
TEST_PASS=printf "${COLOR_BLUE}$@: ${COLOR_GREEN}Passed${COLOR_NONE}\n"
TEST_FAIL=(printf "${COLOR_BLUE}$@: ${COLOR_RED}Failed!${COLOR_NONE}\n" && exit 1)

ARGS-sql='select [Loan Number] from data'

LEAKS=
ifneq ($(LEAKS),)
  PREFIX=leaks 2>/dev/null --atExit --
  REDIRECT=>${TMP_DIR}/leaks.txt; grep leak ${TMP_DIR}/leaks.txt | grep bytes \# # stop processing at this step
  REDIRECT1=>${TMP_DIR}/leaks.txt; grep leak ${TMP_DIR}/leaks.txt | grep bytes ) \# # stop processing at this step
  REDIRECT2=>${TMP_DIR}/leaks.txt; grep leak ${TMP_DIR}/leaks.txt | grep bytes ) \# # stop processing at this step
  export CMP=\# # don't run this step
else
  PREFIX=
  REDIRECT=>
  REDIRECT1=>
  REDIRECT2=-o
  CMP ?= cmp
  export CMP
endif

ifeq ($(CCBN),emcc)
ifeq ($(TESTING),1)
  NODE_EXISTS := $(shell command -v node 2>/dev/null)
  ifeq ($(NODE_EXISTS),)
    $(error "NodeJS not found. Install NodeJS to run wasm tests.")
  endif
  PREFIX="node"
  EXE=.em.js
endif
endif

BIG_FILE ?= none
EXPECT_TIMEOUT ?= 5
EXPECT=TMUX_SOCKET=$@ ../../scripts/test-expect.sh
export EXPECTED_PATH=expected

MAKE_BIN=$(notdir ${MAKE})

DATE_TIME:=$(shell date +%F-%H-%M-%S)
export TIMINGS_CSV:=${TMP_DIR}/timings-${DATE_TIME}.csv

help:
	@echo "To run all tests:"
	@echo "  ${MAKE_BIN} test-all [LEAKS=1]"
	@echo ""
	@echo "Main tests:"
	@echo "  ${MAKE_BIN} test"
	@echo ""
	@echo "Only extension tests:"
	@echo "  ${MAKE_BIN} test-ext"
	@echo ""
	@echo "To run individual test:"
	@echo "  ${MAKE_BIN} test-xxx"
	@echo "where xxx can be: blank-leading-rows ${SOURCES}"
	@echo ""
	@echo "To run on cli:"
	@echo "  ${MAKE_BIN} CLI"

clean:
	rm -rf ${TMP_DIR}
	@${MAKE} -C parallel clean

CLI:
	@echo "Testing CLI..."
	@${MAKE} CLI1=1 test -n | sed 's/\/[^ ]*\/bin\/zsv_/zsv /g' | sh

test-ext:
	(cd ${THIS_MAKEFILE_DIR}/../ext_example && "${MAKE}" CONFIGFILE=${CONFIGFILE} test)

test-all: test test-ext

test: ${TESTS}

test-paste:
	@echo "TO DO: test paste"

${TIMINGS_CSV}:
	@mkdir -p ${TMP_DIR}
	@echo -n "Test, Stage, Time" > ${TIMINGS_CSV}

.SECONDARY: worldcitiespop_mil.csv

.PHONY: help test test-% test-stack clean

test-prop:
	@${TEST_INIT}
	@EXE=${BUILD_DIR}/bin/zsv_prop${EXE} ${MAKE} -C prop test

ifneq ($(ZSV_NO_PARALLEL),1)
test-parallel: ${BUILD_DIR}/bin/zsv_select${EXE} ${BUILD_DIR}/bin/zsv_count${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@CMP="${CMP}" \
	SELECT_EXE=${BUILD_DIR}/bin/zsv_select${EXE} \
	COUNT_EXE=${BUILD_DIR}/bin/zsv_count${EXE} \
	${MAKE} -C parallel test
endif

test-unrecognized-option: ${TARGETS} ${BUILD_DIR}/bin/zsv_paste${EXE} ${BUILD_DIR}/bin/zsv_check${EXE} ${BUILD_DIR}/bin/zsv_prop${EXE}
	@${TEST_INIT}
	@sh unrecognized-option.sh "${BUILD_DIR}/bin" "${EXE}" "${TMP_DIR}" && ${TEST_PASS} || ${TEST_FAIL}

test-check: test-check-1 test-check-2

test-check-1: ${BUILD_DIR}/bin/zsv_check${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/check.csv -o ${TMP_DIR}/$@.out1 && (echo "should have failed!") || true
	@${CMP} ${TMP_DIR}/$@.out1 expected/$@.out1 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/check.csv --display-row -o ${TMP_DIR}/$@.out2 && (echo "should have failed!" && exit 1) || true
	@${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}

test-check-2: ${BUILD_DIR}/bin/zsv_check${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --display-row ${TEST_DATA_DIR}/test/latin1.csv -o ${TMP_DIR}/$@.out && (echo "should have failed!") || true
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

# Security regression test for integer underflow vulnerability
test-vuln-cve-underflow: ${TMP_DIR}/test_vuln_cve_underflow
	@${TEST_INIT}
	@$< && ${TEST_PASS} || ${TEST_FAIL}

${TMP_DIR}/test_vuln_cve_underflow: test_vuln_cve_underflow.c ${BUILD_DIR}/lib/libzsv.a
	@mkdir -p ${TMP_DIR}
	@${CC} ${CFLAGS} -I../../include test_vuln_cve_underflow.c -L${BUILD_DIR}/lib -lzsv -o $@

# -----------------------------------------------------------------------------
# Sanitizer-driven reproduction of the integer-underflow investigation.
#
#   make test-vuln-asan-repro
#     Builds sec/repro.c with the parser unity TU under
#     -fsanitize=address,undefined and runs it against every payload in
#     sec/poc/. Any sanitizer report aborts the process and fails the target.
#
#   make test-vuln-asan-fuzz [FUZZ_ITERS=N]
#     Builds sec/fuzz.c (a portable in-process random-mutation fuzzer that
#     does not require libFuzzer or AFL toolchains), seeds it from sec/poc/,
#     and runs FUZZ_ITERS iterations under ASan+UBSan. Default 1,000,000.
#
# Neither target is part of the default `make test`. Both require a clang
# with sanitizer runtimes (override via `make ... SAN_CC=/path/to/clang`).
# -----------------------------------------------------------------------------
SAN_CC ?= clang
SAN_BUILD_FLAGS = -fsanitize=address,undefined -fno-omit-frame-pointer \
                  -fno-sanitize-recover=all -O1 -g -std=gnu11 -fsigned-char \
                  -DZSV_VERSION=\"sec-review\" -DZSV_HAVE_NEON \
                  -DHAVE_MEMMEM -DHAVE___BUILTIN_EXPECT \
                  -I${THIS_LIB_BASE}/include -I${THIS_LIB_BASE}/src \
                  -I${THIS_LIB_BASE}/app/external/sqlite3
FUZZ_ITERS ?= 1000000

${TMP_DIR}/sec_repro: sec/repro.c ${THIS_LIB_BASE}/src/zsv.c
	@mkdir -p ${TMP_DIR}
	@${SAN_CC} ${SAN_BUILD_FLAGS} ${THIS_LIB_BASE}/src/zsv.c sec/repro.c -o $@

${TMP_DIR}/sec_fuzz: sec/fuzz.c ${THIS_LIB_BASE}/src/zsv.c
	@mkdir -p ${TMP_DIR}
	@${SAN_CC} ${SAN_BUILD_FLAGS} ${THIS_LIB_BASE}/src/zsv.c sec/fuzz.c -o $@

${TMP_DIR}/sec_repro_finish_oob: sec/repro_finish_cell_dl_OOB.c ${THIS_LIB_BASE}/src/zsv.c
	@mkdir -p ${TMP_DIR}
	@${SAN_CC} ${SAN_BUILD_FLAGS} ${THIS_LIB_BASE}/src/zsv.c sec/repro_finish_cell_dl_OOB.c -o $@

${TMP_DIR}/sec_repro_fast_nonstandard_oob: sec/repro_fast_nonstandard_OOB.c ${THIS_LIB_BASE}/src/zsv.c
	@mkdir -p ${TMP_DIR}
	@${SAN_CC} ${SAN_BUILD_FLAGS} ${THIS_LIB_BASE}/src/zsv.c sec/repro_fast_nonstandard_OOB.c -o $@

test-vuln-asan-repro: ${TMP_DIR}/sec_repro
	@${TEST_INIT}
	@for f in sec/poc/*.bin; do \
	  printf "  -- %s --\n" "$$f"; \
	  ${TMP_DIR}/sec_repro "$$f" || ${TEST_FAIL}; \
	done
	@${TEST_PASS}

# Tight-buffer regression for the zsv_finish->cell_dl heap-buffer-overflow.
# The PoC payload is generated programmatically inside the binary so the
# repro is self-contained.
test-vuln-finish-cell-dl-oob: ${TMP_DIR}/sec_repro_finish_oob
	@${TEST_INIT}
	@${TMP_DIR}/sec_repro_finish_oob && ${TEST_PASS} || ${TEST_FAIL}

# Regression for the FAST SIMD scanner OOB on non-standard CSV (mid-cell
# quote that fills the buffer with no row terminator). Verifies that:
#   (a) ASan does not report heap-buffer-overflow in zsv_finish, AND
#   (b) the fast engine returns zsv_status_nonstandard_csv (detection on).
test-vuln-fast-nonstandard-oob: ${TMP_DIR}/sec_repro_fast_nonstandard_oob
	@${TEST_INIT}
	@${TMP_DIR}/sec_repro_fast_nonstandard_oob && ${TEST_PASS} || ${TEST_FAIL}

test-vuln-asan-fuzz: ${TMP_DIR}/sec_fuzz
	@${TEST_INIT}
	@printf "  fuzzing parser for ${FUZZ_ITERS} iterations under ASan+UBSan\n"
	@${TMP_DIR}/sec_fuzz sec/poc ${FUZZ_ITERS} || ${TEST_FAIL}
	@${TEST_PASS}

test-overwrite: ${BUILD_DIR}/bin/zsv_overwrite${EXE} ${BUILD_DIR}/bin/zsv_echo${EXE} ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@CMP="${CMP}" \
	EXE=${BUILD_DIR}/bin/zsv_overwrite${EXE} \
	ECHO_EXE=${BUILD_DIR}/bin/zsv_echo${EXE} \
	SQL_EXE=${BUILD_DIR}/bin/zsv_sql${EXE} \
	${MAKE} -C overwrite test

test-echo: \
	test-echo1 test-echo-overwrite-all test-echo-eol test-echo-chars test-echo-trim \
	test-echo-skip-until test-echo-contiguous test-echo-trim-columns test-echo-trim-columns-2 test-echo-buffsize \
	test-echo-quoted test-echo-rows

test-echo-rows: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/6x3.csv --start-row 3 ${REDIRECT} ${TMP_DIR}/$@.out1
	@${CMP} ${TMP_DIR}/$@.out1 expected/$@.out1 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/6x3.csv --end-row 3 ${REDIRECT} ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/6x3.csv --between-row 2 4 ${REDIRECT} ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL}

test-echo-overwrite-all: test-echo-overwrite test-echo-overwrite-csv test-echo-overwrite-auto

test-echo1: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-eol: test-echo-eol-1 test-echo-eol-2 test-echo-eol-3 test-echo-eol-4

test-echo-eol-%: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/no-eol-$*.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-trim: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --trim ${TEST_DATA_DIR}/test/echo-trim.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-skip-until: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --skip-until ASF ${TEST_DATA_DIR}/test/echo-skip-until.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-contiguous: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --contiguous ${TEST_DATA_DIR}/test/../../data/test/echo-contiguous.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-trim-columns: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --trim-columns ${TEST_DATA_DIR}/test/echo-trim-columns.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-trim-columns-2: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --trim --trim-columns ${TEST_DATA_DIR}/test/echo-trim-columns.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-quoted: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/echo-quoted.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-buffsize: ${BUILD_DIR}/bin/zsv_echo${EXE} ${TEST_DATA_DIR}/bigger-than-buff.csv
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/bigger-than-buff.csv --buff-size 131072 ${REDIRECT} ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err
	@(${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${CMP} ${TMP_DIR}/$@.err expected/$@.err) && ${TEST_PASS} || ${TEST_FAIL}

test-echo-chars: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@echo '東京都' | ${PREFIX} $< -u '?' ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-overwrite: ${BUILD_DIR}/bin/zsv_echo${EXE}
ifneq ($(ZSV_EXTRAS),)
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv --overwrite 'sqlite3://${TEST_DATA_DIR}/loans_1-overwrite.db?sql=select row,col,value from overwrites order by row,col' ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
endif

test-echo-overwrite-auto: ${BUILD_DIR}/bin/zsv_echo${EXE}
ifneq ($(ZSV_EXTRAS),)
	@${TEST_INIT}
	@rm -f ${TEST_DATA_DIR}/.zsv/data/loans_2.csv/overwrite.sqlite3
	@cp -p ${TEST_DATA_DIR}/loans_1.csv ${TEST_DATA_DIR}/loans_2.csv
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_2.csv ${REDIRECT} ${TMP_DIR}/$@-before.out
	@${CMP} ${TMP_DIR}/$@-before.out expected/$@-before.out && ${TEST_PASS} || ${TEST_FAIL}
	@mkdir -p ${TEST_DATA_DIR}/.zsv/data/loans_2.csv
	@sqlite3 ${TEST_DATA_DIR}/.zsv/data/loans_2.csv/overwrite.sqlite3 < ${TEST_DATA_DIR}/loans_2_overwrite.sql
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_2.csv ${REDIRECT} ${TMP_DIR}/$@-after-ignored.out
	@${CMP} ${TMP_DIR}/$@-after-ignored.out expected/$@-after-ignored.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_2.csv --apply-overwrites ${REDIRECT} ${TMP_DIR}/$@-after-applied.out
	@${CMP} ${TMP_DIR}/$@-after-applied.out expected/$@-after-applied.out && ${TEST_PASS} || ${TEST_FAIL}
endif

test-echo-overwrite-csv: ${BUILD_DIR}/bin/zsv_echo${EXE} ${TEST_DATA_DIR}/loans_1-overwrite.csv
ifneq ($(ZSV_EXTRAS),)
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv --overwrite '${TEST_DATA_DIR}/loans_1-overwrite.csv' ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
endif

worldcitiespop_mil.csv:
	curl -LOk 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'

worldcitiespop_mil.tsv: worldcitiespop_mil.csv ${BUILD_DIR}/bin/zsv_2tsv${EXE}
	@${BUILD_DIR}/bin/zsv_2tsv${EXE} $< > $@

test-count test-count-pull: test-% : test-1-% test-2-%

ifeq ($(ZSV_NO_PARALLEL),1)
  ifeq ($(HAVE_PCRE2_8),1)
    HELP_SELECT_LINES=45
    HELP_COUNT_LINES=6
  else
    HELP_SELECT_LINES=43
    HELP_COUNT_LINES=6
  endif
else
  ifeq ($(HAVE_PCRE2_8),1)
    HELP_SELECT_LINES=47
    HELP_COUNT_LINES=8
  else
    HELP_SELECT_LINES=46
    HELP_COUNT_LINES=8
  endif
endif
test-cli: ${CLI}
	@${TEST_INIT}
	@[ "${CLI}" = "" ] && echo 1>&2 'test-cli: missing CLI env var' && exit 1 || exit 0
	@${PREFIX} $< help select 2>&1 > ${TMP_DIR}/$@.out
# help/usage now prefixes the command with the host program name (e.g. `zsv select`,
# or whatever argv[0]/$$ZSV_PROG_NAME resolves to), so match the text as a suffix.
	@head -1 ${TMP_DIR}/$@.out | tr -d '\r\n' | grep -qE 'select: extracts and outputs specified columns$$' && [ $$(( `cat ${TMP_DIR}/$@.out | wc -l` )) = "${HELP_SELECT_LINES}" ] && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< help count 2>&1 > ${TMP_DIR}/$@.out
	@head -1 ${TMP_DIR}/$@.out | tr -d '\r\n' | grep -qE 'Usage: .*count \[options\]$$' && [ $$(( `cat ${TMP_DIR}/$@.out | wc -l` )) = "${HELP_COUNT_LINES}" ] && ${TEST_PASS} || ${TEST_FAIL}

test-1-count test-1-count-pull: test-1-% : ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@cat worldcitiespop_mil.csv | ${PREFIX} $< ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-1-count.out && ${TEST_PASS} || ${TEST_FAIL}
	@cat worldcitiespop_mil.csv | ${PREFIX} $< --parser fast ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-1-count.out && ${TEST_PASS} || ${TEST_FAIL}

test-2-count test-2-count-pull: ${BUILD_DIR}/bin/zsv_count${EXE} ${TEST_DATA_DIR}/test/buffsplit_quote.csv
	@${TEST_INIT}
	@for x in 5000 5002 5004 5006 5008 5010 5013 5015 5017 5019 5021 5101 5105 5111 5113 5115 5117 5119 5121 5123 5125 5127 5129 5131 5211 5213 5215 5217 5311 5313 5315 5317 5413 5431 5433 5455 6133; do \
	${PREFIX} $< -r $$x ${TEST_DATA_DIR}/test/buffsplit_quote.csv; \
	done > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-2-count.out && ${TEST_PASS} || ${TEST_FAIL}

# Test nonstandard quoting: compat parser always, fast parser never
# Verifies count and select produce correct output for CSV with mid-cell quotes,
# standard quoted fields with commas, multiline quoted fields, and mixed patterns.
test-quoted-nonstandard: ${BUILD_DIR}/bin/zsv_count${EXE} ${BUILD_DIR}/bin/zsv_select${EXE} ${TEST_DATA_DIR}/test/quoted_nonstandard.csv
	@${TEST_INIT}
	@${PREFIX} ${BUILD_DIR}/bin/zsv_count${EXE} --parser compat ${TEST_DATA_DIR}/test/quoted_nonstandard.csv ${REDIRECT} ${TMP_DIR}/$@-count-compat.out
	@${CMP} ${TMP_DIR}/$@-count-compat.out expected/test-quoted-nonstandard-count.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${BUILD_DIR}/bin/zsv_select${EXE} --parser compat ${TEST_DATA_DIR}/test/quoted_nonstandard.csv ${REDIRECT} ${TMP_DIR}/$@-select-compat.out
	@${CMP} ${TMP_DIR}/$@-select-compat.out expected/test-quoted-nonstandard-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-select test-select-pull: test-% : test-onlycrlf-% test-n-% test-6-% test-7-% test-8-% test-9-% test-10-% test-11-% test-12-% test-13-% test-search-% test-regex-% test-quotebuff-% test-fixed-1-% test-fixed-2-% test-fixed-3-% test-fixed-4-% test-merge-% test-stdin-filename-% test-corrupt-props-%

# --stdin-filename: apply file properties saved under the given path to stdin input;
# the path need not exist (only its .zsv/data cache does), an explicit
# command-line option still overrides the saved property, and an input read
# from an actual file keeps its own property lookup
test-stdin-filename-select test-stdin-filename-select-pull: test-stdin-filename-% : ${BUILD_DIR}/bin/zsv_%${EXE} ${BUILD_DIR}/bin/zsv_prop${EXE}
	@${TEST_INIT}
	@mkdir -p ${TMP_DIR}/$@
	@printf 'Col,Type\nA,B\n1,x\n2,y\n' > ${TMP_DIR}/$@/data.csv
	@${BUILD_DIR}/bin/zsv_prop${EXE} ${TMP_DIR}/$@/file.csv -d 2 >/dev/null
	@h=`${PREFIX} $< --stdin-filename ${TMP_DIR}/$@/file.csv < ${TMP_DIR}/$@/data.csv | head -1`; [ "$$h" = "Col A,Type B" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --stdin-filename ${TMP_DIR}/$@/file.csv - < ${TMP_DIR}/$@/data.csv | head -1`; [ "$$h" = "Col A,Type B" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --stdin-filename ${TMP_DIR}/$@/file.csv -d 1 < ${TMP_DIR}/$@/data.csv 2>/dev/null | head -1`; [ "$$h" = "Col,Type" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --stdin-filename ${TMP_DIR}/$@/file.csv ${TMP_DIR}/$@/data.csv | head -1`; [ "$$h" = "Col,Type" ] && ${TEST_PASS} || ${TEST_FAIL}
# rc assertions run unwrapped: under LEAKS=1, leaks(1) owns the exit code
	@$< --stdin-filename >/dev/null 2>&1; rc=$$?; [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ${TEST_PASS} || ${TEST_FAIL}
	@$< --stdin-filename - >/dev/null 2>&1; rc=$$?; [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ${TEST_PASS} || ${TEST_FAIL}

# a corrupt .zsv/data property cache must fail loudly (nonzero exit + message
# naming the props file), not silently exit 0 with no output
# rc assertion runs unwrapped: under LEAKS=1, leaks(1) owns the exit code
test-corrupt-props-select test-corrupt-props-select-pull: test-corrupt-props-% : ${BUILD_DIR}/bin/zsv_%${EXE} ${BUILD_DIR}/bin/zsv_prop${EXE}
	@${TEST_INIT}
	@rm -rf ${TMP_DIR}/$@ && mkdir -p ${TMP_DIR}/$@
	@printf 'Col,Type\nA,B\n1,x\n' > ${TMP_DIR}/$@/file.csv
	@${BUILD_DIR}/bin/zsv_prop${EXE} ${TMP_DIR}/$@/file.csv -d 2 >/dev/null
	@echo 'not json' > ${TMP_DIR}/$@/.zsv/data/file.csv/props.json
	@$< ${TMP_DIR}/$@/file.csv > ${TMP_DIR}/$@/out.txt 2> ${TMP_DIR}/$@/err.txt; rc=$$?; \
	  grep -q 'unable to parse properties file' ${TMP_DIR}/$@/err.txt \
	    && [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ! [ -s ${TMP_DIR}/$@/out.txt ] && ${TEST_PASS} || ${TEST_FAIL}

# sql buffers stdin to a temp file; --stdin-filename must still apply saved props
test-stdin-filename-sql: ${BUILD_DIR}/bin/zsv_sql${EXE} ${BUILD_DIR}/bin/zsv_prop${EXE}
	@${TEST_INIT}
	@mkdir -p ${TMP_DIR}/$@
	@printf 'Col,Type\nA,B\n1,x\n2,y\n' > ${TMP_DIR}/$@/data.csv
	@${BUILD_DIR}/bin/zsv_prop${EXE} ${TMP_DIR}/$@/file.csv -d 2 >/dev/null
	@h=`${PREFIX} $< --stdin-filename ${TMP_DIR}/$@/file.csv - 'select * from data' < ${TMP_DIR}/$@/data.csv | head -1`; [ "$$h" = "Col A,Type B" ] && ${TEST_PASS} || ${TEST_FAIL}

test-merge-select test-merge-select-pull: test-merge-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --merge ${TEST_DATA_DIR}/test/select-merge.csv ${REDIRECT} ${TMP_DIR}/test-merge-%.out
	@${CMP} ${TMP_DIR}/test-merge-%.out expected/test-merge-select.out && ${TEST_PASS} || ${TEST_FAIL}

# --rename: targeted column rename by name (renames every match) or #N (1-based input index; the only
# way to target a blank-named or one of several duplicate-named columns). Header-only and streaming:
# data rows are byte-identical, and a selector matching no column / an out-of-range index is a hard
# error with non-zero exit. See SPEC-zsv-select-rename.md and the f1-rename-tests.make AC harness.
test-rename-select: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@printf 'id,name,amount\n1,a,10\n2,b,20\n' > ${TMP_DIR}/$@-r.csv
	@printf 'id,,amount\n1,x,10\n2,y,20\n' > ${TMP_DIR}/$@-blank.csv
	@printf 'id,name,name,amount\n1,a,b,10\n2,c,d,20\n' > ${TMP_DIR}/$@-dup.csv
	@h=`${PREFIX} $< --rename 'name=full_name' ${TMP_DIR}/$@-r.csv | head -1`; [ "$$h" = "id,full_name,amount" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --rename '#3=amt' ${TMP_DIR}/$@-r.csv | head -1`; [ "$$h" = "id,name,amt" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --rename '#2=subtype' ${TMP_DIR}/$@-blank.csv | head -1`; [ "$$h" = "id,subtype,amount" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --rename 'name=n' ${TMP_DIR}/$@-dup.csv | head -1`; [ "$$h" = "id,n,n,amount" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --rename '#2=first_name' ${TMP_DIR}/$@-dup.csv | head -1`; [ "$$h" = "id,first_name,name,amount" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< -n --rename '#2=subtype' ${TMP_DIR}/$@-blank.csv -- 1 2 | head -1`; [ "$$h" = "id,subtype" ] && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --rename 'name=full_name' ${TMP_DIR}/$@-r.csv | tail -n +2 > ${TMP_DIR}/$@-data.out; tail -n +2 ${TMP_DIR}/$@-r.csv > ${TMP_DIR}/$@-data.exp; ${CMP} ${TMP_DIR}/$@-data.out ${TMP_DIR}/$@-data.exp && ${TEST_PASS} || ${TEST_FAIL}
# rc assertions run unwrapped: under LEAKS=1, leaks(1) owns the exit code
	@$< --rename 'nope=x' ${TMP_DIR}/$@-r.csv >/dev/null 2>${TMP_DIR}/$@-err.txt; rc=$$?; grep -q nope ${TMP_DIR}/$@-err.txt && [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ${TEST_PASS} || ${TEST_FAIL}
	@$< --rename '#9=x' ${TMP_DIR}/$@-r.csv >/dev/null 2>&1; rc=$$?; [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ${TEST_PASS} || ${TEST_FAIL}
# hash-escape addendum (AC14-AC18): address columns whose name begins with '#'
	@printf 'id,#foo,amt\n1,a,10\n' > ${TMP_DIR}/$@-hf.csv
	@printf 'id,#5,amt\n1,a,10\n' > ${TMP_DIR}/$@-h5.csv
	@h=`${PREFIX} $< --rename '\#foo=x' ${TMP_DIR}/$@-hf.csv | head -1`; [ "$$h" = "id,x,amt" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --rename '#foo=x' ${TMP_DIR}/$@-hf.csv | head -1`; [ "$$h" = "id,x,amt" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --rename '\#5=NEW' ${TMP_DIR}/$@-h5.csv | head -1`; [ "$$h" = "id,NEW,amt" ] && ${TEST_PASS} || ${TEST_FAIL}
	@h=`${PREFIX} $< --rename '#2=b' ${TMP_DIR}/$@-r.csv | head -1`; [ "$$h" = "id,b,amount" ] && ${TEST_PASS} || ${TEST_FAIL}
	@$< --rename '#5=oops' ${TMP_DIR}/$@-h5.csv >/dev/null 2>&1; rc=$$?; [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ${TEST_PASS} || ${TEST_FAIL}
# unmatched escaped selector reports the RESOLVED literal name (#9), not the raw arg (\#9)
	@$< --rename '\#9=x' ${TMP_DIR}/$@-hf.csv >/dev/null 2>${TMP_DIR}/$@-err2.txt; rc=$$?; grep -q '"#9"' ${TMP_DIR}/$@-err2.txt && [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ${TEST_PASS} || ${TEST_FAIL}
# F1b: a header-phase --rename error must NOT leave a 0-byte -o output file behind
	@rm -f ${TMP_DIR}/$@-oerr.csv; $< --rename '#9=x' ${TMP_DIR}/$@-r.csv -o ${TMP_DIR}/$@-oerr.csv >/dev/null 2>&1; rc=$$?; [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && [ ! -f ${TMP_DIR}/$@-oerr.csv ] && ${TEST_PASS} || ${TEST_FAIL}
# F1b: same for a bad column selector (the deferred-open fix covers all header-phase errors)
	@rm -f ${TMP_DIR}/$@-oselerr.csv; $< ${TMP_DIR}/$@-r.csv -o ${TMP_DIR}/$@-oselerr.csv -- nope >/dev/null 2>&1; rc=$$?; [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && [ ! -f ${TMP_DIR}/$@-oselerr.csv ] && ${TEST_PASS} || ${TEST_FAIL}
# F1b: a pre-existing -o target is preserved (not truncated) on a header-phase error
	@printf 'KEEP\n' > ${TMP_DIR}/$@-okeep.csv; ${PREFIX} $< --rename '#9=x' ${TMP_DIR}/$@-r.csv -o ${TMP_DIR}/$@-okeep.csv >/dev/null 2>&1; g=`cat ${TMP_DIR}/$@-okeep.csv 2>/dev/null`; [ "$$g" = "KEEP" ] && ${TEST_PASS} || ${TEST_FAIL}
# F1b: a successful --rename with -o still writes the file with the renamed header
	@rm -f ${TMP_DIR}/$@-ook.csv; ${PREFIX} $< --rename 'name=full_name' ${TMP_DIR}/$@-r.csv -o ${TMP_DIR}/$@-ook.csv >/dev/null 2>&1; h=`head -1 ${TMP_DIR}/$@-ook.csv 2>/dev/null`; [ "$$h" = "id,full_name,amount" ] && ${TEST_PASS} || ${TEST_FAIL}

test-quotebuff-select test-quotebuff-select-pull: test-quotebuff-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${THIS_MAKEFILE_DIR}/select-quotebuff-gen.sh | ${PREFIX} $< -B 4096 ${REDIRECT} ${TMP_DIR}/$@.out
# use a dummy ${CMP} call so that it works with LEAKS=1
	@${CMP} /dev/null /dev/null && sed 's/"/Q/g' < ${TMP_DIR}/$@.out | grep QQ >/dev/null && ${TEST_FAIL} || ${TEST_PASS}

test-n-select test-n-select-pull: test-n-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}

	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv -u "?" -R 4 -d 2 ${REDIRECT} ${TMP_DIR}/test-n-$*.out
	@${CMP} ${TMP_DIR}/test-n-$*.out expected/test-select.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv -u "?" -H 10 -R 4 -d 2 -x 'Cash Out Amount' ${REDIRECT} ${TMP_DIR}/test-n-$*-x.out
	@${CMP} ${TMP_DIR}/test-n-$*-x.out expected/test-select-x.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $< ${TEST_DATA_DIR}/test/embedded.csv -e 'X' ${REDIRECT} ${TMP_DIR}/test-2-$*.out
	@${CMP} ${TMP_DIR}/test-2-$*.out expected/test-2-select.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $< ${TEST_DATA_DIR}/test/embedded_dos.csv -e 'X' ${REDIRECT} ${TMP_DIR}/test-3-$*.out
	@${CMP} ${TMP_DIR}/test-3-$*.out expected/test-3-select.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv -u "?" -R 4 -d 2 -N ${REDIRECT} ${TMP_DIR}/test-4-$*.out
	@${CMP} ${TMP_DIR}/test-4-$*.out expected/test-4-select.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $< ${TEST_DATA_DIR}/quoted.csv -e 'x' ${REDIRECT} ${TMP_DIR}/test-5-$*.out
	@${CMP} ${TMP_DIR}/test-5-$*.out expected/test-5-select.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $< ${TEST_DATA_DIR}/stack1.csv --no-header -H 10 ${REDIRECT} ${TMP_DIR}/test-no-header-$*.out
	@${CMP} ${TMP_DIR}/test-no-header-$*.out expected/test-no-header-select.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $< ${TEST_DATA_DIR}/stack1.csv --prepend-header 'my,Header.' -H 10 ${REDIRECT} ${TMP_DIR}/test-prepend-header-$*.out
	@${CMP} ${TMP_DIR}/test-prepend-header-$*.out expected/test-prepend-header-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-6-select test-6-select-pull: test-6-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/tab.txt -t ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-6-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-7-select test-7-select-pull: test-7-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/white.csv ${REDIRECT} ${TMP_DIR}/$@.out1
	@${CMP} ${TMP_DIR}/$@.out1 expected/test-7-select.out1 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --whitespace-clean ${TEST_DATA_DIR}/test/white.csv ${REDIRECT} ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/test-7-select.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --whitespace-clean-no-newline ${TEST_DATA_DIR}/test/white.csv ${REDIRECT} ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/test-7-select.out3 && ${TEST_PASS} || ${TEST_FAIL}

test-8-select test-8-select-pull: test-8-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/white_utf8.csv ${REDIRECT} ${TMP_DIR}/$@.out1
	@${CMP} ${TMP_DIR}/$@.out1 expected/test-8-select.out1 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --whitespace-clean ${TEST_DATA_DIR}/test/white_utf8.csv ${REDIRECT} ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/test-8-select.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --whitespace-clean-no-newline ${TEST_DATA_DIR}/test/white_utf8.csv ${REDIRECT} ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/test-8-select.out3 && ${TEST_PASS} || ${TEST_FAIL}

test-9-select test-9-select-pull: test-9-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/quoted3.csv -q ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-9-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-10-select: test-10-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< expected/test-2tsv-2.out -t --unescape ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-onlycrlf-select: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
ifneq ($(ZSV_NO_ONLY_CRLF),1)
	@${PREFIX} $< -O "|" --no-quote ../../data/test/crlf.txt > ${TMP_DIR}/$@.out1
	@${CMP} ${TMP_DIR}/$@.out1 expected/$@.out1 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< -O "|" --only-crlf --no-quote ../../data/test/crlf.txt > ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --only-crlf ../../data/test/crlf-2.csv > ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL}
endif

test-10-select-pull test-search-select-pull test-regex-select-pull test-onlycrlf-select-pull:
	@echo 'N/a ($@)'

test-11-select test-11-select-pull: test-11-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@echo "A1,B1" | ${PREFIX} $< --header-row "column1,column2" > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-11-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-12-select test-12-select-pull: test-12-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -n -x 1-2 -x 6 -x 8-10 -x 13- ${TEST_DATA_DIR}/test/desc.csv > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-12-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-13-select test-13-select-pull: test-13-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@(${PREFIX} $< --header-row-span 2 ${TEST_DATA_DIR}/test/select-13.csv | head -1 > ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/test-13-select.out && ${TEST_PASS} || ${TEST_FAIL})

test-search-select: test-search-% : ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@${PREFIX} $< worldcitiespop_mil.csv --search escu > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
	@echo "TO DO: select --search with overwritten values"

test-regex-select: test-regex-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
ifeq ($(HAVE_PCRE2_8),1)
	@${PREFIX} $< worldcitiespop_mil.csv --regex-search '^esc[uo]' > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
else
	@echo "  regex not available in this build"
endif

test-fixed-1-select-pull test-fixed-2-select-pull test-fixed-3-select-pull test-fixed-4-select-pull:
	@echo "$@: n/a"

test-fixed-1-select: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed.csv --fixed 3,7,12,18,20,21,22 ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-fixed-1-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-fixed-2-select: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed-auto.txt --fixed-auto ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-fixed-2-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-fixed-3-select: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed-auto2.txt --fixed-auto ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-fixed-3-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-fixed-4-select: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed-auto3.txt --fixed-auto ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-fixed-4-select.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed-auto4a.txt --fixed-auto ${REDIRECT} ${TMP_DIR}/$@a.out
	@${CMP} ${TMP_DIR}/$@a.out expected/test-fixed-4a-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-rm: ${BUILD_DIR}/bin/zsv_prop${EXE} ${BUILD_DIR}/bin/zsv_rm${EXE}
	@${TEST_INIT}
	@echo 'hi' > ${TMP_DIR}/$@.csv
	@${PREFIX} ${BUILD_DIR}/bin/zsv_prop${EXE} ${TMP_DIR}/$@.csv -R 1 -d 2 ${REDIRECT} /dev/null
	@find ${TMP_DIR}/.zsv/data/$@.csv/props.json -type f >/dev/null
	@${PREFIX} ${BUILD_DIR}/bin/zsv_rm${EXE} ${TMP_DIR}/$@.csv -f ${REDIRECT} /dev/null
	@find ${TMP_DIR}/.zsv/data/$@.csv/props.json -type f 2>/dev/null && ${TEST_FAIL} || ${TEST_PASS}

test-mv: ${BUILD_DIR}/bin/zsv_prop${EXE} ${BUILD_DIR}/bin/zsv_mv${EXE}
	@${TEST_INIT}
	@echo 'hi' > ${TMP_DIR}/$@.csv
	@rm -rf ${TMP_DIR}/.zsv/data/$@.csv ${TMP_DIR}/.zsv/data/$@-moved.csv ${TMP_DIR}/$@-moved.csv
	@${PREFIX} ${BUILD_DIR}/bin/zsv_prop${EXE} ${TMP_DIR}/$@.csv -R 1 -d 2 ${REDIRECT} /dev/null
	@find ${TMP_DIR}/.zsv/data/$@.csv/props.json -type f >/dev/null
	@${PREFIX} ${BUILD_DIR}/bin/zsv_mv${EXE} ${TMP_DIR}/$@.csv ${TMP_DIR}/$@-moved.csv ${REDIRECT} /dev/null
	@find ${TMP_DIR}/.zsv/data/$@-moved.csv/props.json -type f >/dev/null && ${TEST_PASS} || ${TEST_FAIL}

test-blank-leading-rows: test-blank-leading-rows-1 test-blank-leading-rows-2 test-blank-leading-rows-3 test-blank-leading-rows-4

test-blank-leading-rows-1: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/blank-leading-rows.csv ${REDIRECT} ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err
	@(${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${CMP} ${TMP_DIR}/$@.err expected/$@.err) && ${TEST_PASS} || ${TEST_FAIL}

test-blank-leading-rows-2: ${BUILD_DIR}/bin/zsv_serialize${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -S ${TEST_DATA_DIR}/test/blank-leading-rows.csv ${REDIRECT} ${TMP_DIR}/$@.out 2>&1
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-blank-leading-rows-3: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -R 1 ${TEST_DATA_DIR}/test/blank-leading-rows.csv ${REDIRECT} ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err
	@(${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${CMP} ${TMP_DIR}/$@.err expected/$@.err) && ${TEST_PASS} || ${TEST_FAIL}

test-blank-leading-rows-4: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -R 2 ${TEST_DATA_DIR}/test/blank-leading-rows.csv ${REDIRECT} ${TMP_DIR}/$@.out 2>&1
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack: test-stack1 test-stack2 test-stack3 test-stack4 test-stack-filename

test-stack-filename: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_INIT}
	@cd ${TEST_DATA_DIR} && ${PREFIX} $< --prepend-filename stack[12].csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack1: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/stack[12].csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack2: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/stack2-[12].csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack3: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_INIT}
	@echo 'a,b,c' > ${TMP_DIR}/1.csv
	@${PREFIX} $< ${TMP_DIR}/1.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack4: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_INIT}
	@echo 'a,b,c,a,b' > ${TMP_DIR}/$@-1.csv
	@echo '1,2,3,4,5' >> ${TMP_DIR}/$@-1.csv
	@echo 'a,b,c,a,b' > ${TMP_DIR}/$@-2.csv
	@echo 'z1,z2,z3,z4,z5' >> ${TMP_DIR}/$@-2.csv
	@${PREFIX} $< ${TMP_DIR}/$@-1.csv ${TMP_DIR}/$@-2.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --unique ${TMP_DIR}/$@-1.csv ${TMP_DIR}/$@-2.csv ${REDIRECT} ${TMP_DIR}/$@-2.out
	@${CMP} ${TMP_DIR}/$@-2.out expected/$@-2.out && ${TEST_PASS} || ${TEST_FAIL}

test-sql test-flatten : test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

test-flatten-2 : test-%-2: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< --row-id 'Loan Number' -- 'Current Loan Amount'=array 'Current Interest Rate=json' < "${TEST_DATA_DIR}/test/$*-2.csv" ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

# flatten is a producer: when the row-id column name also occurs as an output value
# it would otherwise emit a duplicate header (a,a,b). Ensure it disambiguates to
# a,a_2,b by default and notes the rename on stderr (renames are never silent).
test-flatten-dupcol: ${BUILD_DIR}/bin/zsv_flatten${EXE}
	@${TEST_INIT}
	@printf 'a,key,val\n1,a,x\n1,b,y\n' > ${TMP_DIR}/$@.csv
	@${PREFIX} $< ${TMP_DIR}/$@.csv --row-id a --col-name key -V val > ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out \
	  && grep -q 'renamed duplicate column "a"' ${TMP_DIR}/$@.err && ${TEST_PASS} || ${TEST_FAIL}

test-pretty: test-pretty-1 test-pretty-escape-chars

test-pretty-1 : test-%-1: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

test-pretty-escape-chars: ${BUILD_DIR}/bin/zsv_pretty${EXE}
	@${TEST_INIT}
	@(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/pretty-escape.csv -M ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

test-2tsv: test-2tsv-1 test-2tsv-2 test-2tsv-3

test-2tsv-1 test-2tsv-2: test-% : ${BUILD_DIR}/bin/zsv_2tsv${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for 2tsv" && exit 1) || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

test-2tsv-3: ${BUILD_DIR}/bin/zsv_2tsv${EXE}
	@${TEST_INIT}
	@(${PREFIX} $< --convert-to-space < ${TEST_DATA_DIR}/test/2tsv-2.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

${THIS_MAKEFILE_DIR}/../../data/quoted5.csv: ${THIS_MAKEFILE_DIR}/../../data/quoted5.csv.bz2
	@bzip2 -d -c $< > $@

test-serialize-quoted: ${BUILD_DIR}/bin/zsv_serialize${EXE} ${THIS_MAKEFILE_DIR}/../../data/quoted5.csv
	@${TEST_INIT}
	@rm -f ${TMP_DIR}/$@.out
	@(${PREFIX} $< ./../../data/quoted5.csv ${REDIRECT1} ${TMP_DIR}/$@.out1 && \
	grep xxxxxx < ${TMP_DIR}/$@.out1 | grep Produktinfo > ${TMP_DIR}/$@.out && rm ${TMP_DIR}/$@.out1 && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

test-serialize-additional: ${BUILD_DIR}/bin/zsv_serialize${EXE} ${TEST_DATA_DIR}/test/serialize.csv
	@${TEST_INIT}
	@(${PREFIX} $< < ${TEST_DATA_DIR}/test/serialize.csv -a 'Interest Paid Through Date' -a 'original INTEREST Only Term' ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

test-serialize-position-id: ${BUILD_DIR}/bin/zsv_serialize${EXE} ${TEST_DATA_DIR}/test/serialize.csv
	@${TEST_INIT}
	@(${PREFIX} $< --id-column 3 < ${TEST_DATA_DIR}/test/serialize.csv -a 'Interest Paid Through Date' -a 'original INTEREST Only Term' ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

test-serialize : test-%: ${BUILD_DIR}/bin/zsv_%${EXE} test-serialize-quoted test-serialize-additional test-serialize-position-id
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< -p < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@-2.out && \
	${CMP} ${TMP_DIR}/$@-2.out expected/$@-2.out && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< -p < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@-2.out && \
	${CMP} ${TMP_DIR}/$@-2.out expected/$@-2.out && ${TEST_PASS} || ${TEST_FAIL})

test-sql: test-sql2 test-sql3 test-sql4 test-sql5 test-stdin-filename-sql test-sql-dupcol test-sql-dupcol-rename test-sql-dupcol-msg \
          test-sql-dupcol-default test-sql-dupcol-3x test-sql-dupcol-collide test-sql-dupcol-caseins \
          test-sql-dupcol-nodup
test-sql2: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@echo ${ARGS-sql} > ${TMP_DIR}/$@.sql
	@(${PREFIX} $< '@'${TMP_DIR}/$@.sql ${TEST_DATA_DIR}/test/sql.csv ${REDIRECT1} ${TMP_DIR}/$@.out) && \
	${CMP} ${TMP_DIR}/$@.out expected/test-sql.out && ${TEST_PASS} || ${TEST_FAIL}

test-sql3: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@(${PREFIX} $< --join-indexes 8 ${TEST_DATA_DIR}/test/sql.csv ${TEST_DATA_DIR}/test/sql.csv ${REDIRECT1} ${TMP_DIR}/$@.out) && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-sql4: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@(${PREFIX} $< ${TEST_DATA_DIR}/test/blank-leading-rows.csv -d 2 'select * from data' ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-sql5: ${BUILD_DIR}/bin/zsv_sql${EXE} # test blank rows
	@${TEST_INIT}
	@echo 'a,b,c' > ${TMP_DIR}/1.csv
	@(${PREFIX} $< ${TMP_DIR}/1.csv 'select * from data' ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

# Regression for the duplicate-column-name abort under the opt-out flag: input whose
# header has the same name twice makes SQLite reject the CREATE TABLE. With
# --error-on-duplicate-columns, zsv sql must report the error and exit *gracefully*
# (normal nonzero code), not die by signal (SIGABRT -> 128+6=134), which is what the
# pre-fix double-free in zsv_sqlite3_add_csv() caused. We assert the exit code is a
# graceful failure (nonzero and < 128) rather than a signal death.
# rc assertion runs unwrapped: under LEAKS=1, leaks(1) owns the exit code
test-sql-dupcol: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@printf 'a,b,a\n1,2,3\n' > ${TMP_DIR}/$@.csv
	@$< --error-on-duplicate-columns ${TMP_DIR}/$@.csv 'select 1 from data limit 1' > ${TMP_DIR}/$@.out 2>&1; \
	  rc=$$?; \
	  printf '  exit=%s\n' "$$rc"; \
	  [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ${TEST_PASS} || ${TEST_FAIL}

# Back-compat: --rename-duplicate-columns still makes repeated header names unique
# (a,b,a -> a,b,a_2). The renamed data goes to stdout; the rename is noted on stderr
# as a single summary line containing the token 'auto-renamed' (never silent). (F7 G7)
test-sql-dupcol-rename: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@printf 'a,b,a\n1,2,3\n' > ${TMP_DIR}/$@.csv
	@${PREFIX} $< --rename-duplicate-columns ${TMP_DIR}/$@.csv 'select * from data' > ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err
	@n=$$(grep -c -i 'auto-renamed' ${TMP_DIR}/$@.err); ${CMP} ${TMP_DIR}/$@.out expected/$@.out \
	  && [ "$$n" = "1" ] && ${TEST_PASS} || ${TEST_FAIL}

# Opt-out: with --error-on-duplicate-columns a duplicate header is detected at parse
# time and reported with a single actionable message naming the column and its 1-based
# positions (rather than SQLite's generic "bad schema"), while exiting gracefully. (F7 G5)
# rc assertion runs unwrapped: under LEAKS=1, leaks(1) owns the exit code
test-sql-dupcol-msg: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@printf 'a,b,a\n1,2,3\n' > ${TMP_DIR}/$@.csv
	@$< --error-on-duplicate-columns ${TMP_DIR}/$@.csv 'select * from data' > ${TMP_DIR}/$@.out 2>&1; \
	  rc=$$?; \
	  printf '  exit=%s\n' "$$rc"; \
	  grep -q 'duplicate column name "a" at columns 1 and 3' ${TMP_DIR}/$@.out \
	    && [ "$$rc" -ne 0 ] && [ "$$rc" -lt 128 ] && ${TEST_PASS} || ${TEST_FAIL}

# F7 G1/G2: duplicate input columns are auto-disambiguated by DEFAULT (no flag) ->
# exit 0, header BV_NUM,name,amount,BV_NUM_2, and exactly ONE 'auto-renamed' stderr line.
test-sql-dupcol-default: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@printf 'BV_NUM,name,amount,BV_NUM\n1,a,10,100\n' > ${TMP_DIR}/$@.csv
	@${PREFIX} $< ${TMP_DIR}/$@.csv 'select * from data' > ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err; \
	  rc=$$?; h=$$(sed -n '1p' ${TMP_DIR}/$@.out); n=$$(grep -c -i 'auto-renamed' ${TMP_DIR}/$@.err); \
	  [ "$$rc" = "0" ] && [ "$$h" = "BV_NUM,name,amount,BV_NUM_2" ] && [ "$$n" = "1" ] && ${TEST_PASS} || ${TEST_FAIL}

# F7 G3/G4: three 'a' columns -> a,a_2,a_3,b, and the two renames COLLAPSE to one line.
test-sql-dupcol-3x: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@printf 'a,a,a,b\n1,2,3,4\n' > ${TMP_DIR}/$@.csv
	@${PREFIX} $< ${TMP_DIR}/$@.csv 'select * from data' > ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err; \
	  rc=$$?; h=$$(sed -n '1p' ${TMP_DIR}/$@.out); n=$$(grep -c -i 'auto-renamed' ${TMP_DIR}/$@.err); \
	  [ "$$rc" = "0" ] && [ "$$h" = "a,a_2,a_3,b" ] && [ "$$n" = "1" ] && ${TEST_PASS} || ${TEST_FAIL}

# F7 G8 + decision D1 (collision-stability): a,a,a_2 -> a,a_3,a_2. The synthesized name
# skips past the user's explicit a_2 (left intact); all three output headers are unique.
test-sql-dupcol-collide: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@printf 'a,a,a_2\n1,2,3\n' > ${TMP_DIR}/$@.csv
	@${PREFIX} $< ${TMP_DIR}/$@.csv 'select * from data' > ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err; \
	  rc=$$?; h=$$(sed -n '1p' ${TMP_DIR}/$@.out); \
	  uq=$$(printf '%s' "$$h" | tr ',' '\n' | sort -u | grep -c .); \
	  [ "$$rc" = "0" ] && [ "$$h" = "a,a_3,a_2" ] && [ "$$uq" = "3" ] && ${TEST_PASS} || ${TEST_FAIL}

# F7 G9: case-insensitive duplicate (BV_NUM/bv_num) is auto-fixed -> exit 0, 2 columns.
test-sql-dupcol-caseins: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@printf 'BV_NUM,bv_num\n1,2\n' > ${TMP_DIR}/$@.csv
	@${PREFIX} $< ${TMP_DIR}/$@.csv 'select * from data' > ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err; \
	  rc=$$?; h=$$(sed -n '1p' ${TMP_DIR}/$@.out); \
	  tot=$$(printf '%s' "$$h" | tr ',' '\n' | grep -c .); \
	  [ "$$rc" = "0" ] && [ "$$tot" = "2" ] && ${TEST_PASS} || ${TEST_FAIL}

# F7 G6 (guard, no false positive): a file with no duplicates exits 0 with NO summary line.
test-sql-dupcol-nodup: ${BUILD_DIR}/bin/zsv_sql${EXE}
	@${TEST_INIT}
	@printf 'a,b,c\n1,2,3\n' > ${TMP_DIR}/$@.csv
	@${PREFIX} $< ${TMP_DIR}/$@.csv 'select * from data' > ${TMP_DIR}/$@.out 2> ${TMP_DIR}/$@.err; \
	  rc=$$?; n=$$(grep -c -i 'auto-renamed' ${TMP_DIR}/$@.err); \
	  [ "$$rc" = "0" ] && [ "$$n" = "0" ] && ${TEST_PASS} || ${TEST_FAIL}

${BUILD_DIR}/bin/zsv_%${EXE}:
	${MAKE} -C .. $@ CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG}

# The unified `cli` (zsv) multitool is built by the parent app/Makefile, not by the
# zsv_% pattern rule above. test-compare-redline-v2 exercises `cli help compare <topic>`
# routing, so the standalone suite must be able to build it on a clean tree (CI) the same
# way it builds the zsv_* binaries -- by delegating to the parent.
${BUILD_DIR}/bin/cli${EXE}:
	${MAKE} -C .. $@ CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG}

test-2db: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv ${BUILD_DIR}/bin/zsv_2json${EXE} ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} ${BUILD_DIR}/bin/zsv_select${EXE} -H 24999 -N worldcitiespop_mil.csv > ${TMP_DIR}/$@.csv
	@${PREFIX} ${BUILD_DIR}/bin/zsv_2json${EXE} --database --index "country_ix on country" --unique-index "ux on [#]" ${TMP_DIR}/$@.csv > ${TMP_DIR}/$@.json
	@(${PREFIX} $< ${ARGS-$*} -o ${TMP_DIR}/$@.db --table data --overwrite < ${TMP_DIR}/test-2db.json ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
	@sqlite3 ${TMP_DIR}/$@.db .schema | sed 's/ IF NOT EXISTS//' | sed 's/"data"/data/g' > ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@sqlite3 ${TMP_DIR}/$@.db "select count(*) from data" > ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL}

# test-2db-csv: build a SQLite3 DB directly from CSV input (no JSON round-trip).
# Covers basic conversion, legacy-pipeline agreement, --table, extension
# detection / overrides, .tsv TAB, edge cases (header-only, ragged, empty &
# duplicate headers, quoted fields) and error cases.
DB2=${BUILD_DIR}/bin/zsv_2db${EXE}
test-2db-csv: ${BUILD_DIR}/bin/zsv_2db${EXE} ${BUILD_DIR}/bin/zsv_2json${EXE}
	@${TEST_INIT}
	@# --- fixtures ---
	@printf 'name,age,city\nAlice,30,NYC\nBob,25,LA\nCarol,40,SF\n' > ${TMP_DIR}/$@-basic.csv
	@# 1a. basic CSV -> DB: schema
	@(${PREFIX} ${DB2} ${TMP_DIR}/$@-basic.csv -o ${TMP_DIR}/$@-basic.db --overwrite ${REDIRECT1} ${TMP_DIR}/$@.run.out)
	@sqlite3 ${TMP_DIR}/$@-basic.db .schema | sed 's/ IF NOT EXISTS//' > ${TMP_DIR}/$@.schema.out
	@${CMP} ${TMP_DIR}/$@.schema.out expected/$@.schema.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 1b. row count
	@sqlite3 ${TMP_DIR}/$@-basic.db 'select count(*) from mytable' > ${TMP_DIR}/$@.count.out
	@${CMP} ${TMP_DIR}/$@.count.out expected/$@.count.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 1c. data
	@sqlite3 ${TMP_DIR}/$@-basic.db 'select * from mytable' > ${TMP_DIR}/$@.data.out
	@${CMP} ${TMP_DIR}/$@.data.out expected/$@.data.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 1d. agreement with the legacy 2json | 2db pipeline (same bytes in, same rows out)
	@${BUILD_DIR}/bin/zsv_2json${EXE} --database ${TMP_DIR}/$@-basic.csv | ${DB2} -o ${TMP_DIR}/$@-legacy.db --overwrite >/dev/null 2>&1
	@sqlite3 ${TMP_DIR}/$@-legacy.db 'select * from mytable' > ${TMP_DIR}/$@.legacy.out
	@${CMP} ${TMP_DIR}/$@.legacy.out ${TMP_DIR}/$@.data.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 2. --table honored for CSV input
	@${DB2} ${TMP_DIR}/$@-basic.csv -o ${TMP_DIR}/$@-tbl.db --table people --overwrite >/dev/null 2>&1
	@sqlite3 ${TMP_DIR}/$@-tbl.db 'select count(*) from people' > ${TMP_DIR}/$@.tbl.out
	@${CMP} ${TMP_DIR}/$@.tbl.out expected/$@.count.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 3a. extension override: --from-csv forces CSV on a .json-named file
	@cp ${TMP_DIR}/$@-basic.csv ${TMP_DIR}/$@-asjson.json
	@${DB2} --from-csv ${TMP_DIR}/$@-asjson.json -o ${TMP_DIR}/$@-csvjson.db --overwrite >/dev/null 2>&1
	@sqlite3 ${TMP_DIR}/$@-csvjson.db 'select count(*) from mytable' > ${TMP_DIR}/$@.csvjson.out
	@${CMP} ${TMP_DIR}/$@.csvjson.out expected/$@.count.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 3b. .tsv auto-detects CSV and sets TAB delimiter
	@printf 'a\tb\tc\n1\t2\t3\n' > ${TMP_DIR}/$@-x.tsv
	@${DB2} ${TMP_DIR}/$@-x.tsv -o ${TMP_DIR}/$@-tsv.db --overwrite >/dev/null 2>&1
	@sqlite3 ${TMP_DIR}/$@-tsv.db 'select * from mytable' > ${TMP_DIR}/$@.tsv.out
	@${CMP} ${TMP_DIR}/$@.tsv.out expected/$@.tsv.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 4a. header-only input -> empty table, exit 0
	@printf 'h1,h2,h3\n' > ${TMP_DIR}/$@-honly.csv
	@${DB2} ${TMP_DIR}/$@-honly.csv -o ${TMP_DIR}/$@-honly.db --overwrite >/dev/null 2>&1 && echo "rows=0 exit=$$?" > ${TMP_DIR}/$@.honly.out
	@printf 'rows=0 exit=0\n' | ${CMP} - ${TMP_DIR}/$@.honly.out && ${TEST_PASS} || ${TEST_FAIL}
	@sqlite3 ${TMP_DIR}/$@-honly.db 'select count(*) from mytable' > ${TMP_DIR}/$@.honlycount.out
	@printf '0\n' | ${CMP} - ${TMP_DIR}/$@.honlycount.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 4b. empty header cell -> synthesized column_<n>; duplicate headers -> de-duped
	@printf 'a,,c,c\n1,2,3,4\n' > ${TMP_DIR}/$@-edge.csv
	@${DB2} ${TMP_DIR}/$@-edge.csv -o ${TMP_DIR}/$@-edge.db --overwrite >/dev/null 2>&1
	@sqlite3 ${TMP_DIR}/$@-edge.db .schema | sed 's/ IF NOT EXISTS//' > ${TMP_DIR}/$@.edge.out
	@${CMP} ${TMP_DIR}/$@.edge.out expected/$@.edge.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 4c. ragged rows (short pads with empty, long truncates to header width)
	@printf 'a,b,c\n1,2\n3,4,5,6\n' > ${TMP_DIR}/$@-rag.csv
	@${DB2} ${TMP_DIR}/$@-rag.csv -o ${TMP_DIR}/$@-rag.db --overwrite >/dev/null 2>&1
	@sqlite3 ${TMP_DIR}/$@-rag.db 'select * from mytable' > ${TMP_DIR}/$@.rag.out
	@${CMP} ${TMP_DIR}/$@.rag.out expected/$@.rag.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 4d. quoted fields with embedded comma and newline (round-trip via the parser)
	@printf 'a,b\n"x,y","l1\nl2"\n' > ${TMP_DIR}/$@-q.csv
	@${DB2} ${TMP_DIR}/$@-q.csv -o ${TMP_DIR}/$@-q.db --overwrite >/dev/null 2>&1
	@sqlite3 ${TMP_DIR}/$@-q.db "select a || '|' || b from mytable" > ${TMP_DIR}/$@.q.out
	@printf 'x,y|l1\nl2\n' | ${CMP} - ${TMP_DIR}/$@.q.out && ${TEST_PASS} || ${TEST_FAIL}
	@# 5. error cases (each must exit non-zero)
	@(! ${DB2} ${TMP_DIR}/$@-basic.csv >/dev/null 2>&1) && ${TEST_PASS} || ${TEST_FAIL}
	@(! ${DB2} ${TMP_DIR}/$@-basic.csv -o ${TMP_DIR}/$@-basic.db >/dev/null 2>&1) && ${TEST_PASS} || ${TEST_FAIL}
	@(! ${DB2} --from-csv --from-json ${TMP_DIR}/$@-basic.csv -o ${TMP_DIR}/$@-z.db --overwrite >/dev/null 2>&1) && ${TEST_PASS} || ${TEST_FAIL}

test-jq: test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@(${PREFIX} $< keys ./../../docs/db.schema.json ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
	@(${PREFIX} $< 'keys|.[]' ./../../docs/db.schema.json --raw-output ${REDIRECT1} ${TMP_DIR}/$@.raw.out)
	@${CMP} ${TMP_DIR}/$@.raw.out expected/$@.raw.out && ${TEST_PASS} || ${TEST_FAIL}

test-2json: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} ${BUILD_DIR}/bin/zsv_2db${EXE} ${BUILD_DIR}/bin/zsv_select${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

#	ajv validate --strict-tuples=false -s ${THIS_MAKEFILE_DIR}/../../docs/csv.schema.json -d expected/$@.out.json [suffix must be json]
	@(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv --compact ${REDIRECT1} ${TMP_DIR}/$@.compact.out && \
	${CMP} ${TMP_DIR}/$@.compact.out expected/$@.compact.out && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --object < ${TEST_DATA_DIR}/quoted2.csv ${REDIRECT1} ${TMP_DIR}/$@.out2 && \
	${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --no-header < ${TEST_DATA_DIR}/quoted2.csv ${REDIRECT1} ${TMP_DIR}/$@.out3 && \
	${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --database < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out4 && \
	${CMP} ${TMP_DIR}/$@.out4 expected/$@.out4 && ${TEST_PASS} || ${TEST_FAIL})

#	ajv validate --strict-tuples=false -s ${THIS_MAKEFILE_DIR}/../../docs/db.schema.json -d expected/$@.out4.json [suffix must be json]

	@(${PREFIX} $< --object --no-empty < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out5 && \
	${CMP} ${TMP_DIR}/$@.out5 expected/$@.out5 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --object --no-empty < ${TEST_DATA_DIR}/quoted4.csv ${REDIRECT1} ${TMP_DIR}/$@.out6 && \
	${CMP} ${TMP_DIR}/$@.out6 expected/$@.out6 && ${TEST_PASS} || ${TEST_FAIL})

	@${PREFIX} ${BUILD_DIR}/bin/zsv_select${EXE} -H 1999 -N worldcitiespop_mil.csv | \
	${PREFIX} ${BUILD_DIR}/bin/zsv_2json${EXE} --database --index "country_ix on country" --unique-index "ux on [#]" | \
	${PREFIX} ${BUILD_DIR}/bin/zsv_2db${EXE} -o ${TMP_DIR}/$@.db --table data --overwrite
	@(${PREFIX} $< --from-db ${TMP_DIR}/$@.db ${REDIRECT1} ${TMP_DIR}/$@.out7 && \
	${CMP} ${TMP_DIR}/$@.out7 expected/$@.out7 && ${TEST_PASS} || ${TEST_FAIL})
#	ajv validate --strict-tuples=false -s ${THIS_MAKEFILE_DIR}/../../docs/db.schema.json -d expected/$@.out7.json [suffix must be json]

ifneq ($(ZSV_NO_TOON),1)
	@# --from-toon: convert a TOON document to JSON
	@(printf '[2]{a,b}:\n  1,x\n  2,"y,z"\n' | ${PREFIX} $< --from-toon ${REDIRECT1} ${TMP_DIR}/$@.from-toon && \
	${CMP} ${TMP_DIR}/$@.from-toon expected/$@.from-toon && ${TEST_PASS} || ${TEST_FAIL})

	@# --from-toon is mutually exclusive with --from-db
	@(printf '[0]:\n' | $< --from-toon --from-db /dev/null >/dev/null 2>&1 && ${TEST_FAIL} || ${TEST_PASS})
endif

# 2toon mirrors 2json but emits TOON. Same inputs/options; the db input for
# --from-db is built the same way (2json | 2db) and read back with 2toon.
test-2toon: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} ${BUILD_DIR}/bin/zsv_2db${EXE} ${BUILD_DIR}/bin/zsv_2json${EXE} ${BUILD_DIR}/bin/zsv_select${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/2json.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/2json.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/2json.csv --compact ${REDIRECT1} ${TMP_DIR}/$@.compact.out && \
	${CMP} ${TMP_DIR}/$@.compact.out expected/$@.compact.out && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --object < ${TEST_DATA_DIR}/quoted2.csv ${REDIRECT1} ${TMP_DIR}/$@.out2 && \
	${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --no-header < ${TEST_DATA_DIR}/quoted2.csv ${REDIRECT1} ${TMP_DIR}/$@.out3 && \
	${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --database < ${TEST_DATA_DIR}/test/2json.csv ${REDIRECT1} ${TMP_DIR}/$@.out4 && \
	${CMP} ${TMP_DIR}/$@.out4 expected/$@.out4 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --object --no-empty < ${TEST_DATA_DIR}/test/2json.csv ${REDIRECT1} ${TMP_DIR}/$@.out5 && \
	${CMP} ${TMP_DIR}/$@.out5 expected/$@.out5 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --object --no-empty < ${TEST_DATA_DIR}/quoted4.csv ${REDIRECT1} ${TMP_DIR}/$@.out6 && \
	${CMP} ${TMP_DIR}/$@.out6 expected/$@.out6 && ${TEST_PASS} || ${TEST_FAIL})

	@${PREFIX} ${BUILD_DIR}/bin/zsv_select${EXE} -H 1999 -N worldcitiespop_mil.csv | \
	${PREFIX} ${BUILD_DIR}/bin/zsv_2json${EXE} --database --index "country_ix on country" --unique-index "ux on [#]" | \
	${PREFIX} ${BUILD_DIR}/bin/zsv_2db${EXE} -o ${TMP_DIR}/$@.db --table data --overwrite
	@(${PREFIX} $< --from-db ${TMP_DIR}/$@.db ${REDIRECT1} ${TMP_DIR}/$@.out7 && \
	${CMP} ${TMP_DIR}/$@.out7 expected/$@.out7 && ${TEST_PASS} || ${TEST_FAIL})

	@# --from-json: convert a JSON document to TOON
	@(printf '[{"a":1,"b":"x"},{"a":2,"b":"y,z"}]' | ${PREFIX} $< --from-json ${REDIRECT1} ${TMP_DIR}/$@.from-json && \
	${CMP} ${TMP_DIR}/$@.from-json expected/$@.from-json && ${TEST_PASS} || ${TEST_FAIL})

	@# round-trip JSON -> TOON (2toon --from-json) -> JSON (2json --from-toon) is the identity
	@(printf '[{"a":1,"b":"x"},{"a":2,"b":"y,z"}]' | ${PREFIX} $< --from-json | \
	${PREFIX} ${BUILD_DIR}/bin/zsv_2json${EXE} --from-toon ${REDIRECT1} ${TMP_DIR}/$@.roundtrip && \
	${CMP} ${TMP_DIR}/$@.roundtrip expected/$@.roundtrip && ${TEST_PASS} || ${TEST_FAIL})

test-2json-help test-2toon-help: test-%-help: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@[ "`${PREFIX} $< -h | wc -l`" -gt 15 ] && ${TEST_PASS} || ${TEST_FAIL} # check output nonempty
	@${PREFIX} $< -h >/dev/null && ${TEST_PASS} || ${TEST_FAIL} # check exit code

test-desc: test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< -q ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})
	@(${PREFIX} $< ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out2 && \
	${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL})
	@(${PREFIX} $< -H ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out3 && \
	${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL})
	@(${PREFIX} $< ${TEST_DATA_DIR}/test/$*-trim.csv ${REDIRECT1} ${TMP_DIR}/$@.trim && \
	${CMP} ${TMP_DIR}/$@.trim expected/$@.trim && ${TEST_PASS} || ${TEST_FAIL})

test-compare-tolerance: ${BUILD_DIR}/bin/zsv_compare${EXE}
	@(${PREFIX} $< ../../data/compare/tolerance1.csv ../../data/compare/tolerance2.csv ${REDIRECT1} ${TMP_DIR}/$@.out1 && \
	${CMP} ${TMP_DIR}/$@.out1 expected/$@.out1 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --tolerance 0.001 ../../data/compare/tolerance1.csv ../../data/compare/tolerance2.csv ${REDIRECT1} ${TMP_DIR}/$@.out2 && \
	${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --tolerance 0.0001 ../../data/compare/tolerance1.csv ../../data/compare/tolerance2.csv ${REDIRECT1} ${TMP_DIR}/$@.out3 && \
	${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< --tolerance 0.00001 ../../data/compare/tolerance1.csv ../../data/compare/tolerance2.csv ${REDIRECT1} ${TMP_DIR}/$@.out4 && \
	${CMP} ${TMP_DIR}/$@.out4 expected/$@.out4 && ${TEST_PASS} || ${TEST_FAIL})

test-compare: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} ${COMPARE_TOON_DEP}
	@${TEST_INIT}
	@(${PREFIX} $< compare/t1.csv compare/t2.csv compare/t3.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< compare/t1.csv compare/t4.csv ${REDIRECT1} ${TMP_DIR}/$@.out2 && \
	${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< -k C compare/t1.csv compare/t5.csv compare/t6.csv ${REDIRECT1} ${TMP_DIR}/$@.out3 && \
	${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< -k C --sort compare/t1.csv compare/t5.csv compare/t6-unsorted.csv ${REDIRECT1} ${TMP_DIR}/$@.out4 && \
	${CMP} ${TMP_DIR}/$@.out4 expected/$@.out4 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< -k C compare/t1.csv compare/t5.csv compare/t6-unsorted.csv ${REDIRECT1} ${TMP_DIR}/$@.out5 && \
	${CMP} ${TMP_DIR}/$@.out5 expected/$@.out5 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< compare/t1.csv compare/t2.csv compare/t3.csv --json ${REDIRECT1} ${TMP_DIR}/$@.out6 && \
	${CMP} ${TMP_DIR}/$@.out6 expected/$@.out6 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< compare/t1.csv compare/t2.csv compare/t3.csv --json-object ${REDIRECT1} ${TMP_DIR}/$@.out7 && \
	${CMP} ${TMP_DIR}/$@.out7 expected/$@.out7 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< compare/t1.csv compare/t7.csv compare/t3.csv --json-object -k c ${REDIRECT1} ${TMP_DIR}/$@.out8 && \
	${CMP} ${TMP_DIR}/$@.out8 expected/$@.out8 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< compare/t1.csv compare/t7.csv compare/t3.csv --print-key-colname -k c ${REDIRECT1} ${TMP_DIR}/$@.out9 && \
	${CMP} ${TMP_DIR}/$@.out9 expected/$@.out9 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< compare/t1.csv compare/t7.csv compare/t3.csv --print-key-colname -k c ${REDIRECT1} ${TMP_DIR}/$@.out9 && \
	${CMP} ${TMP_DIR}/$@.out9 expected/$@.out9 && ${TEST_PASS} || ${TEST_FAIL})

	@(${PREFIX} $< ../../data/compare/t1.csv ../../data/compare/t2.csv --add AccentCity --sort -k country -k city ${REDIRECT1} ${TMP_DIR}/$@.out10 && \
	${CMP} ${TMP_DIR}/$@.out10 expected/$@.out10 && ${TEST_PASS} || ${TEST_FAIL})

	@# --require-all-inputs: ignore rows whose key is missing from any input (inner join);
	@# keys 3 (only in a) and 4 (only in b) are dropped, the key-2 value diff is retained
	@(${PREFIX} $< -k id --require-all-inputs compare/reqall_a.csv compare/reqall_b.csv ${REDIRECT1} ${TMP_DIR}/$@.out11 && \
	${CMP} ${TMP_DIR}/$@.out11 expected/$@.out11 && ${TEST_PASS} || ${TEST_FAIL})

	@# --intersect is an alias for --require-all-inputs (byte-identical output)
	@(${PREFIX} $< -k id --intersect compare/reqall_a.csv compare/reqall_b.csv ${REDIRECT1} ${TMP_DIR}/$@.out11b && \
	${CMP} ${TMP_DIR}/$@.out11b expected/$@.out11 && ${TEST_PASS} || ${TEST_FAIL})

	@# --require-all-inputs in JSON mode
	@(${PREFIX} $< -k id --require-all-inputs --json compare/reqall_a.csv compare/reqall_b.csv ${REDIRECT1} ${TMP_DIR}/$@.out12 && \
	${CMP} ${TMP_DIR}/$@.out12 expected/$@.out12 && ${TEST_PASS} || ${TEST_FAIL})

	@# --columns: explicit ranges
	@(${PREFIX} $< --columns 1-3v4-6 compare/colrange1.csv ${REDIRECT1} ${TMP_DIR}/$@-colrange.out1 && \
	${CMP} ${TMP_DIR}/$@-colrange.out1 expected/$@-colrange.out1 && ${TEST_PASS} || ${TEST_FAIL})

	@# --columns: shorthand single-column spec
	@(${PREFIX} $< --columns 1v4 compare/colrange1.csv ${REDIRECT1} ${TMP_DIR}/$@-colrange.out2 && \
	${CMP} ${TMP_DIR}/$@-colrange.out2 expected/$@-colrange.out2 && ${TEST_PASS} || ${TEST_FAIL})

	@# --columns: column name lookup
	@(${PREFIX} $< --columns "Old_A vs New_A" compare/colrange2.csv ${REDIRECT1} ${TMP_DIR}/$@-colrange.out3 && \
	${CMP} ${TMP_DIR}/$@-colrange.out3 expected/$@-colrange.out3 && ${TEST_PASS} || ${TEST_FAIL})

	@# --columns: duplicate column names (second match used for right side)
	@(${PREFIX} $< --columns "ABC vs ABC" compare/colrange3.csv ${REDIRECT1} ${TMP_DIR}/$@-colrange.out4 && \
	${CMP} ${TMP_DIR}/$@-colrange.out4 expected/$@-colrange.out4 && ${TEST_PASS} || ${TEST_FAIL})

	@# --columns: single column name with duplicate (equivalent to "ABC vs ABC")
	@(${PREFIX} $< --columns "ABC" compare/colrange3.csv ${REDIRECT1} ${TMP_DIR}/$@-colrange.out5 && \
	${CMP} ${TMP_DIR}/$@-colrange.out5 expected/$@-colrange.out5 && ${TEST_PASS} || ${TEST_FAIL})

	@# --columns: error - single column with no duplicate
	@($< --columns "B" compare/colrange3.csv >/dev/null 2>&1 && ${TEST_FAIL} || ${TEST_PASS})

	@# --columns: error - requires exactly one file
	@($< --columns 1v4 compare/colrange1.csv compare/colrange1.csv >/dev/null 2>&1 && ${TEST_FAIL} || ${TEST_PASS})

	@# --columns: error - invalid spec
	@($< --columns bad compare/colrange1.csv >/dev/null 2>&1 && ${TEST_FAIL} || ${TEST_PASS})

ifneq ($(ZSV_NO_TOON),1)
	@# --toon: TOON output (same comparison as --json-object out7, rendered as TOON)
	@(${PREFIX} $< compare/t1.csv compare/t2.csv compare/t3.csv --toon ${REDIRECT1} ${TMP_DIR}/$@.toon && \
	${CMP} ${TMP_DIR}/$@.toon expected/$@.toon && ${TEST_PASS} || ${TEST_FAIL})

	@# --toon is equivalent to --json-object piped through `2toon --from-json`
	@(${PREFIX} $< compare/t1.csv compare/t2.csv compare/t3.csv --json-object | \
	${PREFIX} ${BUILD_DIR}/bin/zsv_2toon${EXE} --from-json ${REDIRECT1} ${TMP_DIR}/$@.toon-equiv && \
	${CMP} ${TMP_DIR}/$@.toon-equiv expected/$@.toon && ${TEST_PASS} || ${TEST_FAIL})

	@# get_default_output_format() does NOT affect the (non-redline) comparison output: with
	@# AI_AGENT set and no format flag, output stays the default CSV (byte-identical to out).
	@# The AI_AGENT default only selects TOON under --redline (see test-compare-redline-v2).
	@(AI_AGENT=1 ${PREFIX} $< compare/t1.csv compare/t2.csv compare/t3.csv ${REDIRECT1} ${TMP_DIR}/$@.ai-default && \
	${CMP} ${TMP_DIR}/$@.ai-default expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

	@# an explicit --toon still yields TOON even when AI_AGENT is unset (blank env)
	@(AI_AGENT= ${PREFIX} $< compare/t1.csv compare/t2.csv compare/t3.csv --toon ${REDIRECT1} ${TMP_DIR}/$@.toon-noenv && \
	${CMP} ${TMP_DIR}/$@.toon-noenv expected/$@.toon && ${TEST_PASS} || ${TEST_FAIL})

	@# an explicit --json yields JSON regardless of AI_AGENT (byte-identical to plain --json)
	@(AI_AGENT=1 ${PREFIX} $< compare/t1.csv compare/t2.csv compare/t3.csv --json ${REDIRECT1} ${TMP_DIR}/$@.json-override && \
	${CMP} ${TMP_DIR}/$@.json-override expected/$@.out6 && ${TEST_PASS} || ${TEST_FAIL})

	@# --json and --toon together is an error
	@($< compare/t1.csv compare/t2.csv compare/t3.csv --json --toon >/dev/null 2>&1 && ${TEST_FAIL} || ${TEST_PASS})
endif

include test-compare-redline.mk

test-sheet: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv test-sheet-cleanup test-sheet-all

test-sheet-cleanup:
	@rm -f tmux-*.log

test-sheet-all: \
	test-sheet-1 test-sheet-2 test-sheet-3 test-sheet-4 test-sheet-5 test-sheet-6 test-sheet-7 test-sheet-8 test-sheet-9 \
	test-sheet-10 test-sheet-11 test-sheet-12 test-sheet-13 test-sheet-14 test-sheet-15 test-sheet-16 test-sheet-17 \
	test-sheet-18 \
	test-sheet-subcommand \
	test-sheet-prop-cmd-opt \
	test-sheet-pivot-1 \
	test-sheet-pivot-V \
	test-sheet-sqlfilter-1 \
	test-sheet-sqlfilter-dupcol \
	test-sheet-errors-1
	@rm -f tmux-*.log zSE*
	@(for SESSION in $^; do ! tmux -L "$$SESSION" kill-server 2>/dev/null || true; done && ${TEST_PASS} || ${TEST_FAIL})

TMUX_TERM=xterm-256color
test-sheet-1: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ -v new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ Down Up && \
	${EXPECT} $@ && ${TEST_PASS} || (for x in `ls tmux-*.log 2>/dev/null`; do echo "Log $$x:" && cat $$x; done && ${TEST_FAIL}))

test-sheet-2: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "C-d" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-3: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "f" "sarmaj" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-4: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "C-d" "C-d" "C-u" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-5: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 160 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-6: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 50 -d -s $@ "${PREFIX} $< -d 3 ${TEST_DATA_DIR}/test/mixed-line-endings.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "G" "C-u" "C-u" "C-u" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-7: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< -d 3 ${TEST_DATA_DIR}/test/mixed-line-endings.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "G" "g" "g" "C-u" "/" "1234" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-8: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 160 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	export EXPECT_TIMEOUT=10 && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "f" "e" ENTER && \
	${EXPECT} $@ filtered && \
	tmux -L $@ send-keys -t $@ "G" "C-u" "k" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-9: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< -d 3 ${TEST_DATA_DIR}/test/mixed-line-endings.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "G" && \
	${EXPECT} $@ bottom && \
	tmux -L $@ send-keys -t $@ -N 4096 "k" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-10: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.tsv ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< -t worldcitiespop_mil.tsv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "f" "e" ENTER && \
	${EXPECT} $@ filtered && \
	tmux -L $@ send-keys -t $@ "G" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-11: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 50 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ -N 11 "C-d" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-12: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "f" "el" ENTER && \
	${EXPECT} $@ filtered-el && \
	tmux -L $@ send-keys -t $@ "f" "al" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-13: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 40 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	tmux -L $@ send-keys -t $@ "?" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-14: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 40 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	tmux -L $@ send-keys -t $@ "?" && \
	${EXPECT} $@ help && \
	tmux -L $@ send-keys -t $@ ESCAPE && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-15: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< -d 3 ${TEST_DATA_DIR}/test/crlf-line-ending.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "G" && \
	${EXPECT} $@ bottom && \
	tmux -L $@ send-keys -t $@ -N 4096 "k" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-16: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< ${TEST_DATA_DIR}/test/header-only.csv" && \
	${EXPECT} $@ indexed && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-17: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 50 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "500G" && \
	${EXPECT} $@ 500g && \
	tmux -L $@ send-keys -t $@ "0G" && \
	${EXPECT} $@ 0g && \
	tmux -L $@ send-keys -t $@ "1G" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

# Find column ("|") then find-next-column ("\") cycles through every header that
# matches the search term. "it" matches City, AccentCity and Latitude; the footer
# shows the selected cell value, confirming the cursor lands on each in turn.
test-sheet-18: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "|" "it" ENTER && \
	${EXPECT} $@ find-col && \
	tmux -L $@ send-keys -t $@ "\\" && \
	${EXPECT} $@ next-1 && \
	tmux -L $@ send-keys -t $@ "\\" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-subcommand: \
	test-sheet-subcommand-open-file-prompt test-sheet-subcommand-open-file-argument \
	test-sheet-subcommand-filter-file-prompt test-sheet-subcommand-filter-file-argument

test-sheet-subcommand-open-file-prompt: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $<" && \
	${EXPECT} $@ blank && \
	tmux -L $@ send-keys -t $@ ":open" ENTER && \
	${EXPECT} $@ open && \
	tmux -L $@ send-keys -t $@ "worldcitiespop_mil.csv" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-subcommand-open-file-argument: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $<" && \
	export EXPECT_TIMEOUT=20 && \
	${EXPECT} $@ blank && \
	tmux -L $@ send-keys -t $@ ":open worldcitiespop_mil.csv" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-subcommand-filter-file-prompt: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ blank && \
	tmux -L $@ send-keys -t $@ ":filter" ENTER && \
	${EXPECT} $@ filter && \
	tmux -L $@ send-keys -t $@ "ireland" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-subcommand-filter-file-argument: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ blank && \
	tmux -L $@ send-keys -t $@ ":filter \"ireland\"" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-prop-cmd-opt: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${BUILD_DIR}/bin/zsv_prop${EXE}
	@echo "TO DO: test-sheet-prop-cmd-opt"
#	@${TEST_INIT}
#	@${PREFIX} ${BUILD_DIR}/bin/zsv_prop${EXE} ${TEST_DATA_DIR}/echo-prop.csv --header-row-span 2 > /dev/null
#	@tmux kill-session 2>/dev/null >/dev/null || printf ''
#	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
#	@tmux -v new-session -x 80 -y 5 -d -s $@
#	@sleep 0.5
#	@tmux send-keys -t $@ "${PREFIX} $< ${TEST_DATA_DIR}/echo-prop.csv" ENTER
#	@sleep 0.1
#	@tmux send-keys -t $@ "f" "A" ENTER
#	@sleep 0.5
#	@tmux -v capture-pane -t $@ -p ${REDIRECT1} ${TMP_DIR}/$@.out
#	@tmux send-keys -t $@ "q"
#	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || (echo 'Incorrect output:' && cat ${TMP_DIR}/$@.out && ${TEST_FAIL})

test-sheet-pivot: test-sheet-pivot-1 test-sheet-pivot-V

test-sheet-pivot-1: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ l v && \
	${EXPECT} $@ groups && \
	tmux -L $@ send-keys -t $@ j j Enter && \
	${EXPECT} $@ drilldown && \
	tmux -L $@ send-keys -t $@ G && \
	${EXPECT} $@ \
	&& ${TEST_PASS} || ${TEST_FAIL})

test-sheet-pivot-V: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "V" "Country||'hi'" ENTER && \
	${EXPECT} $@ groups && \
	tmux -L $@ send-keys -t $@ j j j j j j j j j j j j j j j j j j j j j j j ENTER && \
	${EXPECT} $@ drilldown && \
	${TEST_PASS} || ${TEST_FAIL})

test-sheet-sqlfilter: test-sheet-sqlfilter-1 test-sheet-sqlfilter-dupcol

# Exercises the capability sqlfilter's dedupe-on enables: a file whose header
# repeats a column name can still be filtered in sheet. The initial view shows the
# raw duplicate header (Country, City, Country); after :where, sqlfilter builds the
# underlying table -- disambiguating the repeat to Country_2 -- and returns the
# filtered rows. Pre-change this filter could not build its table at all.
test-sheet-sqlfilter-dupcol: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@printf 'Country,City,Country\nus,new york,USA\nfr,paris,France\nde,berlin,Germany\n' > ${TMP_DIR}/$@.csv
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< ${TMP_DIR}/$@.csv" && \
	${EXPECT} $@ open && \
	tmux -L $@ send-keys -t $@ ":where" ENTER "City like 'b%'" ENTER && \
	${EXPECT} $@ filter && \
	${TEST_PASS} || ${TEST_FAIL})

test-sheet-sqlfilter-1: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ ":where" ENTER "city like 'ber%'" ENTER && \
	${EXPECT} $@ filter && \
	tmux -L $@ send-keys -t $@ ":where" ENTER "this is invalid" ENTER && \
	${EXPECT} $@ invalid && \
	tmux -L $@ send-keys -t $@ ":where" ENTER "City = -1" ENTER && \
	${EXPECT} $@ noresults && \
	${TEST_PASS} || ${TEST_FAIL})

test-sheet-errors: test-sheet-errors-1

${TMP_DIR}/test-sheet-errors-1.txt: worldcitiespop_mil.csv
	@head -1000 worldcitiespop_mil.csv | tr '\n' '|' > $@.tmp
	@echo 'x' >> $@.tmp
	@echo 'x' >> $@.tmp
	@echo 'x' >> $@.tmp
	@mv $@.tmp $@

test-sheet-errors-1: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TMP_DIR}/test-sheet-errors-1.txt
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< ${TMP_DIR}/test-sheet-errors-1.txt" && \
	${EXPECT} $@ indexed-errors && \
	tmux -L $@ send-keys -t $@ ":errors" ENTER && \
	${EXPECT} $@ errors && \
	tmux -L $@ send-keys -t $@ ESCAPE && \
	${EXPECT} $@ indexed-errors && \
	tmux -L $@ send-keys -t $@ ":errors-clear" ENTER && \
	${EXPECT} $@ indexed && \
	${TEST_PASS} || ${TEST_FAIL})

ifeq ($(HAVE_PCRE2_8),1)
test-sheet-filter-this: test-sheet-filter-this-1 test-sheet-filter-this-regex
else
test-sheet-filter-this: test-sheet-filter-this-1
endif

test-sheet-filter-this-1: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "l" "F" "casas" ENTER && \
	${EXPECT} $@ nodata && \
	tmux -L $@ send-keys -t $@ ESCAPE "l" "F" "mx" ENTER && \
	${EXPECT} $@ filter && \
	tmux -L $@ send-keys -t $@ ESCAPE ":filtercol" ENTER "mx" ENTER && \
	${EXPECT} $@ filter && \
	${TEST_PASS} || ${TEST_FAIL})

test-sheet-filter-this-regex: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "l" "F" "/casas" ENTER && \
	${EXPECT} $@ nodata && \
	tmux -L $@ send-keys -t $@ ESCAPE "l" "F" "/^mx" ENTER && \
	${EXPECT} $@ filterregex && \
	tmux -L $@ send-keys -t $@ ESCAPE ":filtercol" ENTER "/^mx" ENTER && \
	${EXPECT} $@ filterregex && \
	${TEST_PASS} || ${TEST_FAIL})

benchmark-sheet-index: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@if [ "${BIG_FILE}" = "none" ]; then \
		echo "Need to set BIG_FILE to a CSV file; ideally 10GB+"; \
	fi
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -L $@ kill-server 2>/dev/null; rm -f ${TMP_DIR}/tmux-$$(id -u)/$@ 2>/dev/null; \
	tmux -L $@ new-session -x 80 -y 50 -d -s "$@" "${PREFIX} $< ${BIG_FILE}" && \
	${EXPECT} $@ indexed && \
	tmux -L $@ send-keys -t $@ "G" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
