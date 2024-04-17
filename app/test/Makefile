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

THIS_LIB_BASE=$(shell cd ../.. && pwd)
CCBN=$(shell basename ${CC})
BUILD_DIR=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}
TMP_DIR=${THIS_LIB_BASE}/tmp
TEST_DATA_DIR=${THIS_LIB_BASE}/data

SOURCES= echo count count-pull select select-pull sql 2json serialize flatten pretty desc stack 2db 2tsv jq compare
TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${SOURCES}))

TESTS=test-blank-leading-rows $(addprefix test-,${SOURCES}) test-rm test-mv test-2json-help

COLOR_NONE=\033[0m
COLOR_GREEN=\033[1;32m
COLOR_RED=\033[1;31m
COLOR_BLUE=\033[1;34m
COLOR_PINK=\033[1;35m

TEST_PASS=echo "${COLOR_BLUE}$@: ${COLOR_GREEN}Passed${COLOR_NONE}"
TEST_FAIL=(echo "${COLOR_BLUE}$@: ${COLOR_RED}Failed!${COLOR_NONE}" && exit 1)
TEST_INIT=mkdir -p ${TMP_DIR} && echo "${COLOR_PINK}$@: ${COLOR_NONE}"

ARGS-sql='select [Loan Number] from data'

LEAKS=
ifneq ($(LEAKS),)
  PREFIX=leaks 2>/dev/null --atExit --
  REDIRECT=>${TMP_DIR}/leaks.txt; grep leak ${TMP_DIR}/leaks.txt | grep bytes \# # stop processing at this step
  REDIRECT1=>${TMP_DIR}/leaks.txt; grep leak ${TMP_DIR}/leaks.txt | grep bytes ) \# # stop processing at this step
  REDIRECT2=>${TMP_DIR}/leaks.txt; grep leak ${TMP_DIR}/leaks.txt | grep bytes ) \# # stop processing at this step
  CMP=\# # don't run this step
else
  PREFIX=
  REDIRECT=>
  REDIRECT1=>
  REDIRECT2=-o
  CMP=cmp
endif


help:
	@echo "To run all tests: make test [LEAKS=1]"
	@echo "To run individual test: make test-xxx"
	@echo "To run on cli: make CLI"
	@echo "  where xxx can be: blank-leading-rows ${SOURCES}"

clean:
	rm -rf ${TMP_DIR}

CLI:
	@echo "Testing CLI..."
	@make CLI1=1 test -n | sed 's/\/[^ ]*\/bin\/zsv_/zsv /g' | sh

test: ${TESTS}

.SECONDARY: worldcitiespop_mil.csv

.PHONY: help test test-% test-stack clean

test-prop:
	EXE=${BUILD_DIR}/bin/zsv_prop${EXE} make -C prop test

test-echo : test-echo1 test-echo-overwrite test-echo-eol test-echo-overwrite-csv test-echo-chars test-echo-trim test-echo-skip-until test-echo-contiguous test-echo-trim-columns test-echo-trim-columns-2

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

test-echo-chars: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} echo '東京都' | $< -u '?' ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-overwrite: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv --overwrite 'sqlite3://${TEST_DATA_DIR}/loans_1-overwrite.db?sql=select row,col,value from overwrites order by row,col' ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-echo-overwrite-csv: ${BUILD_DIR}/bin/zsv_echo${EXE} ${TEST_DATA_DIR}/loans_1-overwrite.csv
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv --overwrite '${TEST_DATA_DIR}/loans_1-overwrite.csv' ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

worldcitiespop_mil.csv:
	curl -LOk 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'

test-count test-count-pull: test-% : test-1-% test-2-%

test-cli: ${CLI}
	@${TEST_INIT}
	@[ "${CLI}" = "" ] && echo 1>&2 'test-cli: missing CLI env var' && exit 1 || exit 0
	@$< help select 2>&1 > ${TMP_DIR}/$@.out
	@[ "`head -1 ${TMP_DIR}/$@.out`" = "select: streaming CSV parser" ] && [ $$(( `cat ${TMP_DIR}/$@.out | wc -l` )) = "36" ] && ${TEST_PASS} || ${TEST_FAIL}
	@$< help count 2>&1 > ${TMP_DIR}/$@.out
	@[ "`head -1 ${TMP_DIR}/$@.out`" = "Usage: count [options]" ] && [ $$(( `cat ${TMP_DIR}/$@.out | wc -l` )) = "5" ] && ${TEST_PASS} || ${TEST_FAIL}

test-1-count test-1-count-pull: test-1-% : ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@cat worldcitiespop_mil.csv | ${PREFIX} $< ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-1-count.out && ${TEST_PASS} || ${TEST_FAIL}

test-2-count test-2-count-pull: ${BUILD_DIR}/bin/zsv_count${EXE} ${TEST_DATA_DIR}/test/buffsplit_quote.csv
	@${TEST_INIT}
	@for x in 5000 5002 5004 5006 5008 5010 5013 5015 5017 5019 5021 5101 5105 5111 5113 5115 5117 5119 5121 5123 5125 5127 5129 5131 5211 5213 5215 5217 5311 5313 5315 5317 5413 5431 5433 5455 6133 ; do $< -r $$x ${TEST_DATA_DIR}/test/buffsplit_quote.csv ; done > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-2-count.out && ${TEST_PASS} || ${TEST_FAIL}

test-select test-select-pull: test-% : test-n-% test-6-% test-7-% test-8-% test-9-% test-10-% test-11-% test-12-% test-quotebuff-% test-fixed-1-% test-fixed-2-% test-fixed-3-% test-fixed-4-% test-merge-%

test-merge-select test-merge-select-pull: test-merge-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --merge ${TEST_DATA_DIR}/test/select-merge.csv ${REDIRECT} ${TMP_DIR}/test-merge-%.out
	@${CMP} ${TMP_DIR}/test-merge-%.out expected/test-merge-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-quotebuff-select test-quotebuff-select-pull: test-quotebuff-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${THIS_MAKEFILE_DIR}/select-quotebuff-gen.sh  | ${PREFIX} $< -B 4096 ${REDIRECT} /tmp/$@.out
# use a dummy ${CMP} call so that it works with LEAKS=1
	@${CMP} /dev/null /dev/null && sed 's/"/Q/g' < /tmp/$@.out | grep QQ >/dev/null && ${TEST_FAIL} || ${TEST_PASS}

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

	@${PREFIX} $<  ${TEST_DATA_DIR}/quoted.csv -e 'x' ${REDIRECT} ${TMP_DIR}/test-5-$*.out
	@${CMP} ${TMP_DIR}/test-5-$*.out expected/test-5-select.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $<  ${TEST_DATA_DIR}/stack1.csv --no-header -H 10 ${REDIRECT} ${TMP_DIR}/test-no-header-$*.out
	@${CMP} ${TMP_DIR}/test-no-header-$*.out expected/test-no-header-select.out && ${TEST_PASS} || ${TEST_FAIL}

	@${PREFIX} $<  ${TEST_DATA_DIR}/stack1.csv --prepend-header 'my,Header.' -H 10 ${REDIRECT} ${TMP_DIR}/test-prepend-header-$*.out
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
	@${CMP} ${TMP_DIR}/$@.out expected/test-10-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-10-select-pull:
	@echo 'N/a (test-10-select-pull)'

test-11-select test-11-select-pull: test-11-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} (echo "A1,B1" | $< --header-row "column1,column2") > /tmp/$@.out
	@cmp /tmp/$@.out expected/test-11-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-12-select test-12-select-pull: test-12-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -n -x 1-2 -x 6 -x 8-10 -x 13- ${TEST_DATA_DIR}/test/desc.csv > /tmp/$@.out
	@${CMP} /tmp/$@.out expected/test-12-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-13-select test-13-select-pull: test-13-% : ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@${PREFIX} [ "$$(echo 'aaa,bb,c\na,bb\nx,y,z' | $< --header-row-span 2 | head -1)" = "aaa a,bb bb,c" ] && ${TEST_PASS} || ${TEST_FAIL}

test-fixed-1-select test-fixed-1-select-pull: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed.csv --fixed 3,7,12,18,20,21,22 ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-fixed-1-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-fixed-2-select test-fixed-2-select-pull: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed-auto.txt --fixed-auto ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-fixed-2-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-fixed-3-select test-fixed-3-select-pull: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed-auto2.txt --fixed-auto ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-fixed-3-select.out && ${TEST_PASS} || ${TEST_FAIL}

test-fixed-4-select test-fixed-4-select-pull: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed-auto3.txt --fixed-auto ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/test-fixed-4-select.out && ${TEST_PASS} || ${TEST_FAIL}

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
	@${PREFIX} $< ${TEST_DATA_DIR}/test/blank-leading-rows.csv ${REDIRECT} ${TMP_DIR}/$@.out 2>&1
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-blank-leading-rows-2: ${BUILD_DIR}/bin/zsv_serialize${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -S ${TEST_DATA_DIR}/test/blank-leading-rows.csv ${REDIRECT} ${TMP_DIR}/$@.out 2>&1
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-blank-leading-rows-3: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -R 1 ${TEST_DATA_DIR}/test/blank-leading-rows.csv ${REDIRECT} ${TMP_DIR}/$@.out 2>&1
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-blank-leading-rows-4: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< -R 2 ${TEST_DATA_DIR}/test/blank-leading-rows.csv ${REDIRECT} ${TMP_DIR}/$@.out 2>&1
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack: test-stack1 test-stack2 test-stack3

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
	@echo 'a,b,c' > /tmp/1.csv
	@${PREFIX} $< /tmp/1.csv ${REDIRECT1} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-sql test-flatten : test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
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

test-2tsv: test-2tsv-1 test-2tsv-2

test-2tsv-1 test-2tsv-2: test-% : ${BUILD_DIR}/bin/zsv_2tsv${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for 2tsv" && exit 1) || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

${THIS_MAKEFILE_DIR}/../../data/quoted5.csv: ${THIS_MAKEFILE_DIR}/../../data/quoted5.csv.bz2
	bzip2 -d -c $< > $@

test-serialize-quoted: ${BUILD_DIR}/bin/zsv_serialize${EXE} ${THIS_MAKEFILE_DIR}/../../data/quoted5.csv
	@${TEST_INIT}
	@rm -f ${TMP_DIR}/$@.out
	@(${PREFIX} $< ${THIS_MAKEFILE_DIR}/../../data/quoted5.csv ${REDIRECT1} ${TMP_DIR}/$@.out1 && \
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
	@echo 'a,b,c' > /tmp/1.csv
	@(${PREFIX} $< /tmp/1.csv 'select * from data' ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}


${BUILD_DIR}/bin/zsv_%${EXE}:
	make -C .. $@ CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG}

test-2db: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} worldcitiespop_mil.csv ${BUILD_DIR}/bin/zsv_2json${EXE} ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${BUILD_DIR}/bin/zsv_select${EXE} -L 25000 -N worldcitiespop_mil.csv | ${BUILD_DIR}/bin/zsv_2json${EXE} --database --index "country_ix on country" --unique-index "ux on [#]" > ${TMP_DIR}/$@.json
	@(${PREFIX} $< ${ARGS-$*} -o ${TMP_DIR}/$@.db --table data --overwrite < ${TMP_DIR}/test-2db.json ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}
	@sqlite3 ${TMP_DIR}/$@.db .schema | sed 's/ IF NOT EXISTS//' | sed 's/"data"/data/g' > ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@sqlite3 ${TMP_DIR}/$@.db "select count(*) from data" > ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL}

test-jq: test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@(${PREFIX} $< keys ${THIS_MAKEFILE_DIR}/../../docs/db.schema.json ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

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

	@${BUILD_DIR}/bin/zsv_select${EXE} -L 2000 -N worldcitiespop_mil.csv | ${BUILD_DIR}/bin/zsv_2json${EXE} --database --index "country_ix on country" --unique-index "ux on [#]" | ${BUILD_DIR}/bin/zsv_2db${EXE} -o ${TMP_DIR}/$@.db --table data --overwrite && (${PREFIX} $< --from-db ${TMP_DIR}/$@.db ${REDIRECT1} ${TMP_DIR}/$@.out7 && ${CMP} ${TMP_DIR}/$@.out7 expected/$@.out7 && ${TEST_PASS} || ${TEST_FAIL})

#	ajv validate --strict-tuples=false -s ${THIS_MAKEFILE_DIR}/../../docs/db.schema.json -d expected/$@.out7.json [suffix must be json]

test-2json-help: test-%-help: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@[ "`$< -h | wc -l`" -gt 17 ] && ${TEST_PASS} || ${TEST_FAIL} # check output nonempty
	@$< -h >/dev/null && ${TEST_PASS} || ${TEST_FAIL} # check exit code

test-desc: test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< -q < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})
	@(${PREFIX} $< < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT2} ${TMP_DIR}/$@.out2 && \
	${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL})
	@(${PREFIX} $< -H < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT2} ${TMP_DIR}/$@.out3 && \
	${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL})
	@(${PREFIX} $< < ${TEST_DATA_DIR}/test/$*-trim.csv ${REDIRECT2} ${TMP_DIR}/$@.trim && \
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

