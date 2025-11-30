# Makefile for use with GNU make

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

IS_WSL=$(shell grep -qi microsoft /proc/version 2>/dev/null && echo 1 || echo 0)
ifeq ($(IS_WSL),1)
  TEST_DATA_DIR=./../../data
  TMP_DIR=./../../tmp
endif

SOURCES=echo count count-pull select select-pull sql 2json serialize flatten pretty desc stack 2db 2tsv jq compare
ifneq ($(ZSV_EXTRAS),)
  SOURCES+=overwrite
endif
TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${SOURCES}))

TESTS=test-blank-leading-rows $(addprefix test-,${SOURCES}) test-rm test-mv test-2json-help test-paste test-flatten-2

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
  export CMP=cmp
endif

BIG_FILE ?= none
EXPECT_TIMEOUT ?= 5
EXPECT=../../scripts/test-expect.sh
export EXPECTED_PATH=expected

MAKE_BIN=$(notdir ${MAKE})

DATE_TIME:=$(shell date +%F-%H-%M-%S)
export TIMINGS_CSV:=${TMP_DIR}/timings-${DATE_TIME}.csv

help:
	@echo "To run all tests: ${MAKE_BIN} test [LEAKS=1]"
	@echo "To run individual test: ${MAKE_BIN} test-xxx"
	@echo "To run on cli: ${MAKE_BIN} CLI"
	@echo "  where xxx can be: blank-leading-rows ${SOURCES}"

clean:
	rm -rf ${TMP_DIR}

CLI:
	@echo "Testing CLI..."
	@${MAKE} CLI1=1 test -n | sed 's/\/[^ ]*\/bin\/zsv_/zsv /g' | sh

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
	@SELECT_EXE=${BUILD_DIR}/bin/zsv_select${EXE} COUNT_EXE=${BUILD_DIR}/bin/zsv_count${EXE} ${MAKE} -C parallel test
endif

test-check: test-check-1 test-check-2

test-check-1: ${BUILD_DIR}/bin/zsv_check${EXE}
	@${TEST_INIT}
	@$< ${TEST_DATA_DIR}/test/check.csv -o ${TMP_DIR}/$@.out1 && (echo "should have failed!") || [ 1 = 1 ]
	@cmp ${TMP_DIR}/$@.out1 expected/$@.out1 && ${TEST_PASS} || ${TEST_FAIL}
	@$< ${TEST_DATA_DIR}/test/check.csv --display-row -o ${TMP_DIR}/$@.out2 && (echo "should have failed!" && exit 1) || [ 1 = 1 ]
	@cmp ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}

test-check-2: ${BUILD_DIR}/bin/zsv_check${EXE}
	@${TEST_INIT}
	@$< --display-row ${TEST_DATA_DIR}/test/latin1.csv -o ${TMP_DIR}/$@.out && (echo "should have failed!") || [ 1 = 1 ]
	@cmp ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-overwrite: ${BUILD_DIR}/bin/zsv_overwrite${EXE} ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@EXE=${BUILD_DIR}/bin/zsv_overwrite${EXE} ECHO_EXE=${BUILD_DIR}/bin/zsv_echo${EXE} CMP="${CMP}" ${MAKE} -C overwrite test
endif

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
	@${PREFIX} echo '東京都' | $< -u '?' ${REDIRECT} ${TMP_DIR}/$@.out
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
  HELP_SELECT_LINES=40
  HELP_COUNT_LINES=6
else
  HELP_SELECT_LINES=39
  HELP_COUNT_LINES=6
endif
else
ifeq ($(HAVE_PCRE2_8),1)
  HELP_SELECT_LINES=43
  HELP_COUNT_LINES=8
else
  HELP_SELECT_LINES=42
  HELP_COUNT_LINES=8
endif
endif
test-cli: ${CLI}
	@${TEST_INIT}
	@[ "${CLI}" = "" ] && echo 1>&2 'test-cli: missing CLI env var' && exit 1 || exit 0
	@$< help select 2>&1 > ${TMP_DIR}/$@.out
	@[ "`head -1 ${TMP_DIR}/$@.out | tr -d '\r\n'`" = "select: extracts and outputs specified columns" ] && [ $$(( `cat ${TMP_DIR}/$@.out | wc -l` )) = "${HELP_SELECT_LINES}" ] && ${TEST_PASS} || ${TEST_FAIL}
	@$< help count 2>&1 > ${TMP_DIR}/$@.out
	@[ "`head -1 ${TMP_DIR}/$@.out | tr -d '\r\n'`" = "Usage: count [options]" ] && [ $$(( `cat ${TMP_DIR}/$@.out | wc -l` )) = "${HELP_COUNT_LINES}" ] && ${TEST_PASS} || ${TEST_FAIL}

test-1-count test-1-count-pull: test-1-% : ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@cat worldcitiespop_mil.csv | ${PREFIX} $< ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-1-count.out && ${TEST_PASS} || ${TEST_FAIL}

test-2-count test-2-count-pull: ${BUILD_DIR}/bin/zsv_count${EXE} ${TEST_DATA_DIR}/test/buffsplit_quote.csv
	@${TEST_INIT}
	@for x in 5000 5002 5004 5006 5008 5010 5013 5015 5017 5019 5021 5101 5105 5111 5113 5115 5117 5119 5121 5123 5125 5127 5129 5131 5211 5213 5215 5217 5311 5313 5315 5317 5413 5431 5433 5455 6133 ; do $< -r $$x ${TEST_DATA_DIR}/test/buffsplit_quote.csv ; done > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-2-count.out && ${TEST_PASS} || ${TEST_FAIL}

test-select test-select-pull: test-% : test-onlycrlf-% test-n-% test-6-% test-7-% test-8-% test-9-% test-10-% test-11-% test-12-% test-13-% test-search-% test-regex-% test-quotebuff-% test-fixed-1-% test-fixed-2-% test-fixed-3-% test-fixed-4-% test-merge-%

test-merge-select test-merge-select-pull: test-merge-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --merge ${TEST_DATA_DIR}/test/select-merge.csv ${REDIRECT} ${TMP_DIR}/test-merge-%.out
	@${CMP} ${TMP_DIR}/test-merge-%.out expected/test-merge-select.out && ${TEST_PASS} || ${TEST_FAIL}

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
	@${PREFIX} (echo "A1,B1" | $< --header-row "column1,column2") > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-11-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-12-select test-12-select-pull: test-12-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -n -x 1-2 -x 6 -x 8-10 -x 13- ${TEST_DATA_DIR}/test/desc.csv > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-12-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-13-select test-13-select-pull: test-13-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
#	@echo "test result:"
#	@echo 'aaa,bb,cxa,bbxx,y,z' | tr x '\n' | $< --header-row-span 2 | head -1
	@${PREFIX} [ "$$(echo 'aaa,bb,cxa,bbxx,y,z' | tr x '\n' | $< --header-row-span 2 | head -1)" = "aaa a,bb bb,c" ] && ${TEST_PASS} || ${TEST_FAIL}

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
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'hi' > ${TMP_DIR}/$@.csv
	@${PREFIX} ${BUILD_DIR}/bin/zsv_prop${EXE} ${TMP_DIR}/$@.csv -R 1 -d 2 ${REDIRECT} /dev/null
	@find ${TMP_DIR}/.zsv/data/$@.csv/props.json -type f >/dev/null
	@${PREFIX} ${BUILD_DIR}/bin/zsv_rm${EXE} ${TMP_DIR}/$@.csv -f ${REDIRECT} /dev/null
	@find ${TMP_DIR}/.zsv/data/$@.csv/props.json -type f 2>/dev/null && ${TEST_FAIL} || ${TEST_PASS}
endif

test-mv: ${BUILD_DIR}/bin/zsv_prop${EXE} ${BUILD_DIR}/bin/zsv_mv${EXE}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'hi' > ${TMP_DIR}/$@.csv
	@rm -rf ${TMP_DIR}/.zsv/data/$@.csv ${TMP_DIR}/.zsv/data/$@-moved.csv ${TMP_DIR}/$@-moved.csv
	@${PREFIX} ${BUILD_DIR}/bin/zsv_prop${EXE} ${TMP_DIR}/$@.csv -R 1 -d 2 ${REDIRECT} /dev/null
	@find ${TMP_DIR}/.zsv/data/$@.csv/props.json -type f >/dev/null
	@${PREFIX} ${BUILD_DIR}/bin/zsv_mv${EXE} ${TMP_DIR}/$@.csv ${TMP_DIR}/$@-moved.csv ${REDIRECT} /dev/null
	@find ${TMP_DIR}/.zsv/data/$@-moved.csv/props.json -type f >/dev/null && ${TEST_PASS} || ${TEST_FAIL}
endif

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

test-stack: test-stack1 test-stack2 test-stack3 test-stack4

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

test-sql: test-sql2 test-sql3 test-sql4 test-sql5
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

${BUILD_DIR}/bin/zsv_%${EXE}:
	${MAKE} -C .. $@ CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG}

test-2db: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv ${BUILD_DIR}/bin/zsv_2json${EXE} ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${BUILD_DIR}/bin/zsv_select${EXE} -H 24999 -N worldcitiespop_mil.csv > ${TMP_DIR}/$@.csv
	@${BUILD_DIR}/bin/zsv_2json${EXE} --database --index "country_ix on country" --unique-index "ux on [#]" ${TMP_DIR}/$@.csv > ${TMP_DIR}/$@.json
	@(${PREFIX} $< ${ARGS-$*} -o ${TMP_DIR}/$@.db --table data --overwrite < ${TMP_DIR}/test-2db.json ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
	@sqlite3 ${TMP_DIR}/$@.db .schema | sed 's/ IF NOT EXISTS//' | sed 's/"data"/data/g' > ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@sqlite3 ${TMP_DIR}/$@.db "select count(*) from data" > ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL}

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

	@${BUILD_DIR}/bin/zsv_select${EXE} -H 1999 -N worldcitiespop_mil.csv | ${BUILD_DIR}/bin/zsv_2json${EXE} --database --index "country_ix on country" --unique-index "ux on [#]" | ${BUILD_DIR}/bin/zsv_2db${EXE} -o ${TMP_DIR}/$@.db --table data --overwrite && (${PREFIX} $< --from-db ${TMP_DIR}/$@.db ${REDIRECT1} ${TMP_DIR}/$@.out7 && ${CMP} ${TMP_DIR}/$@.out7 expected/$@.out7 && ${TEST_PASS} || ${TEST_FAIL})
#	ajv validate --strict-tuples=false -s ${THIS_MAKEFILE_DIR}/../../docs/db.schema.json -d expected/$@.out7.json [suffix must be json]

test-2json-help: test-%-help: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@[ "`$< -h | wc -l`" -gt 15 ] && ${TEST_PASS} || ${TEST_FAIL} # check output nonempty
	@$< -h >/dev/null && ${TEST_PASS} || ${TEST_FAIL} # check exit code

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

test-compare: test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
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

test-sheet: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv test-sheet-cleanup test-sheet-all

test-sheet-cleanup:
	@rm -f tmux-*.log
	@tmux kill-server || printf ''

test-sheet-all: \
	test-sheet-1 test-sheet-2 test-sheet-3 test-sheet-4 test-sheet-5 test-sheet-6 test-sheet-7 test-sheet-8 test-sheet-9 \
	test-sheet-10 test-sheet-11 test-sheet-12 test-sheet-13 test-sheet-14 test-sheet-subcommand test-sheet-prop-cmd-opt \
	test-sheet-pivot \
	test-sheet-sqlfilter \
	test-sheet-errors
	@(for SESSION in $^; do ! tmux kill-session -t "$$SESSION" 2>/dev/null || echo ''; done && ${TEST_PASS} || ${TEST_FAIL})

TMUX_TERM=xterm-256color
test-sheet-1: ${BUILD_DIR}/bin/zsv_sheet${EXE}
#	For this first sheet test, separate new-session from the command so that if it fails we get better logging
	@${TEST_INIT}
	@tmux kill-session 2>/dev/null >/dev/null || printf ''
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux -v new-session -x 80 -y 5 -d -s $@ && \
	sleep 0.5 && \
	tmux send-keys -t $@ "clear" ENTER && \
	tmux send-keys -t $@ "${PREFIX} $< worldcitiespop_mil.csv" ENTER && \
	sleep 0.5 && \
	tmux send-keys -t $@ Down Up && \
	tmux -v capture-pane -t $@ -p ${REDIRECT1} ${TMP_DIR}/$@.out && \
	tmux -v send-keys -t $@ "q" && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || (for x in `ls tmux-*.log` ; do echo "Log $$x:" && cat $$x ; done && ${TEST_FAIL}))

test-sheet-2: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@(tmux new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "C-d" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-3: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@(tmux new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "f" "sarmaj" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-4: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@(tmux new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "C-d" "C-d" "C-u" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-5: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 160 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-6: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 50 -d -s $@ "${PREFIX} $< -d 3 ${TEST_DATA_DIR}/test/mixed-line-endings.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "G" "C-u" "C-u" "C-u" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-7: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 5 -d -s $@ "${PREFIX} $< -d 3 ${TEST_DATA_DIR}/test/mixed-line-endings.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "G" "g" "g" "C-u" "/" "1234" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-8: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 160 -y 5 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	export EXPECT_TIMEOUT=10 && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "f" "e" ENTER && \
	${EXPECT} $@ filtered && \
	tmux send-keys -t $@ "G" "C-u" "k" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-9: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< -d 3 ${TEST_DATA_DIR}/test/mixed-line-endings.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "G" && \
	tmux send-keys -t $@ -N 4096 "k" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-10: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.tsv ${TIMINGS_CSV}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< -t worldcitiespop_mil.tsv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "f" "e" ENTER && \
	${EXPECT} $@ filtered && \
	tmux send-keys -t $@ "G" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-11: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 50 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ -N 11 "C-d" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-12: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "f" "el" ENTER && \
	${EXPECT} $@ filtered-el && \
	tmux send-keys -t $@ "f" "al" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-13: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 40 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	tmux send-keys -t $@ "?" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-14: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv ${TIMINGS_CSV}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 40 -d -s $@ "${PREFIX} $< worldcitiespop_mil.csv" && \
	tmux send-keys -t $@ "?" && \
	${EXPECT} $@ help && \
	tmux send-keys -t $@ ESCAPE && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-15: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 6 -d -s $@ "${PREFIX} $< -d 3 ${TEST_DATA_DIR}/test/crlf-line-ending.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "G" && \
	${EXPECT} $@ bottom && \
	tmux send-keys -t $@ -N 4096 "k" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})

test-sheet-subcommand: \
	test-sheet-subcommand-open-file-prompt test-sheet-subcommand-open-file-argument \
	test-sheet-subcommand-filter-file-prompt test-sheet-subcommand-filter-file-argument

test-sheet-subcommand-open-file-prompt: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $<" && \
	${EXPECT} $@ blank && \
	tmux send-keys -t $@ ":open" ENTER && \
	${EXPECT} $@ open && \
	tmux send-keys -t $@ "worldcitiespop_mil.csv" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-subcommand-open-file-argument: ${BUILD_DIR}/bin/zsv_sheet${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $<" && \
	export EXPECT_TIMEOUT=20 && \
	${EXPECT} $@ blank && \
	tmux send-keys -t $@ ":open worldcitiespop_mil.csv" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-subcommand-filter-file-prompt: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ blank && \
	tmux send-keys -t $@ ":filter" ENTER && \
	${EXPECT} $@ filter && \
	tmux send-keys -t $@ "ireland" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-subcommand-filter-file-argument: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ blank && \
	tmux send-keys -t $@ ":filter \"ireland\"" ENTER && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

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
	@tmux kill-session 2>/dev/null >/dev/null || printf ''
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ l v && \
	${EXPECT} $@ groups && \
	tmux send-keys -t $@ j j Enter && \
	${EXPECT} $@ drilldown && \
	tmux send-keys -t $@ G && \
	${EXPECT} $@ \
	&& ${TEST_PASS} || ${TEST_FAIL})

test-sheet-pivot-V: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@tmux kill-session 2>/dev/null >/dev/null || printf ''
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "V" "Country||'hi'" ENTER && \
	${EXPECT} $@ groups && \
	tmux send-keys -t $@ j j j j j j j j j j j j j j j j j j j j j j j ENTER && \
	${EXPECT} $@ drilldown && \
	${TEST_PASS} || ${TEST_FAIL})

test-sheet-sqlfilter: test-sheet-sqlfilter-1

test-sheet-sqlfilter-1: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@tmux kill-session 2>/dev/null >/dev/null || printf ''
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ ":where" ENTER "city like 'ber%'" ENTER && \
	${EXPECT} $@ filter && \
	tmux send-keys -t $@ ":where" ENTER "this is invalid" ENTER && \
	${EXPECT} $@ invalid && \
	tmux send-keys -t $@ ":where" ENTER "City = -1" ENTER && \
	${EXPECT} $@ noresults && \
	${TEST_PASS} || ${TEST_FAIL})

test-sheet-errors: test-sheet-errors-1

/tmp/test-sheet-errors-1.txt: worldcitiespop_mil.csv
	head -1000 worldcitiespop_mil.csv | tr '\n' '|' > $@.tmp
	echo 'x' >> $@.tmp
	echo 'x' >> $@.tmp
	echo 'x' >> $@.tmp
	mv $@.tmp $@

test-sheet-errors-1: ${BUILD_DIR}/bin/zsv_sheet${EXE} /tmp/test-sheet-errors-1.txt
	@${TEST_INIT}
	@tmux kill-session 2>/dev/null >/dev/null || printf ''
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< /tmp/test-sheet-errors-1.txt" && \
	${EXPECT} $@ indexed-errors && \
	tmux send-keys -t $@ ":errors" ENTER && \
	${EXPECT} $@ errors && \
	tmux send-keys -t $@ ESCAPE && \
	${EXPECT} $@ indexed-errors && \
	tmux send-keys -t $@ ":errors-clear" ENTER && \
	${EXPECT} $@ indexed && \
	${TEST_PASS} || ${TEST_FAIL})

ifeq ($(HAVE_PCRE2_8),1)
test-sheet-filter-this: test-sheet-filter-this-1 test-sheet-filter-this-regex
else
test-sheet-filter-this: test-sheet-filter-this-1
endif

test-sheet-filter-this-1: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@tmux kill-session 2>/dev/null >/dev/null || printf ''
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "l" "F" "casas" ENTER && \
	${EXPECT} $@ nodata && \
	tmux send-keys -t $@ ESCAPE "l" "F" "mx" ENTER && \
	${EXPECT} $@ filter && \
	tmux send-keys -t $@ ESCAPE ":filtercol" ENTER "mx" ENTER && \
	${EXPECT} $@ filter && \
	${TEST_PASS} || ${TEST_FAIL})

test-sheet-filter-this-regex: ${BUILD_DIR}/bin/zsv_sheet${EXE}
	@${TEST_INIT}
	@tmux kill-session 2>/dev/null >/dev/null || printf ''
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 25 -d -s "$@" "${PREFIX} $< worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "l" "F" "/casas" ENTER && \
	${EXPECT} $@ nodata && \
	tmux send-keys -t $@ ESCAPE "l" "F" "/^mx" ENTER && \
	${EXPECT} $@ filterregex && \
	tmux send-keys -t $@ ESCAPE ":filtercol" ENTER "/^mx" ENTER && \
	${EXPECT} $@ filterregex && \
	${TEST_PASS} || ${TEST_FAIL})

benchmark-sheet-index: ${BUILD_DIR}/bin/zsv_sheet${EXE} ${TIMINGS_CSV}
	@${TEST_INIT}
	@if [ "${BIG_FILE}" = "none" ]; then \
		echo "Need to set BIG_FILE to a CSV file; ideally 10GB+"; \
	fi
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@(tmux new-session -x 80 -y 50 -d -s "$@" "${PREFIX} $< ${BIG_FILE}" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "G" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
