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

SOURCES=
ifneq ($(CLI1),1)
SOURCES+=echo
endif

SOURCES+= count select sql 2json serialize flatten pretty desc stack 2db 2tsv jq
TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${SOURCES}))

TESTS=test-blank-leading-rows $(addprefix test-,${SOURCES})

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

test: ${TESTS} #${TMP_DIR} ${TESTS}

# ${TMP_DIR}:
#	@mkdir -p ${TMP_DIR}

.SECONDARY: worldcitiespop_mil.csv

.PHONY: help test test-% test-stack clean

test-echo : ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

worldcitiespop_mil.csv:
	curl -LOk 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'

test-count: test-count-1 test-count-2

test-count-1: ${BUILD_DIR}/bin/zsv_count${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@cat worldcitiespop_mil.csv | ${PREFIX} $< ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-count-2: ${BUILD_DIR}/bin/zsv_count${EXE} ${TEST_DATA_DIR}/test/buffsplit_quote.csv
	@${TEST_INIT}
	@for x in 5000 5002 5004 5006 5008 5010 5013 5015 5017 5019 5021 5101 5105 5111 5113 5115 5117 5119 5121 5123 5125 5127 5129 5131 5211 5213 5215 5217 5311 5313 5315 5317 5413 5431 5433 5455 6133 ; do $< -r $$x ${TEST_DATA_DIR}/test/buffsplit_quote.csv ; done > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-select: test-select-n test-select-6 test-select-7 test-select-8 test-select-9 test-select-quotebuff test-select-fixed-1 test-select-merge

test-select-merge: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< --merge ${TEST_DATA_DIR}/test/select-merge.csv ${REDIRECT} ${TMP_DIR}/test-select-merge.out
	@${CMP} ${TMP_DIR}/test-select-merge.out expected/test-select-merge.out && ${TEST_PASS} || ${TEST_FAIL}

test-select-quotebuff: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${THIS_MAKEFILE_DIR}/select-quotebuff-gen.sh  | ${PREFIX} $< -B 4096 ${REDIRECT} /tmp/$@.out
# use a dummy ${CMP} call so that it works with LEAKS=1
	@${CMP} /dev/null /dev/null && sed 's/"/Q/g' < /tmp/$@.out | grep QQ >/dev/null && ${TEST_FAIL} || ${TEST_PASS}

test-select-n: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv -u "?" -R 4 -d 2 ${REDIRECT} ${TMP_DIR}/test-select.out
	@${CMP} ${TMP_DIR}/test-select.out expected/test-select.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/embedded.csv -e 'X' ${REDIRECT} ${TMP_DIR}/test-select.2.out
	@${CMP} ${TMP_DIR}/test-select.2.out expected/test-select.2.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/embedded_dos.csv -e 'X' ${REDIRECT} ${TMP_DIR}/test-select.3.out
	@${CMP} ${TMP_DIR}/test-select.3.out expected/test-select.3.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< ${TEST_DATA_DIR}/loans_1.csv -u "?" -R 4 -d 2 -N ${REDIRECT} ${TMP_DIR}/test-select.4.out
	@${CMP} ${TMP_DIR}/test-select.4.out expected/test-select.4.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $<  ${TEST_DATA_DIR}/quoted.csv -e 'x' ${REDIRECT} ${TMP_DIR}/test-select.5.out
	@${CMP} ${TMP_DIR}/test-select.5.out expected/test-select.5.out && ${TEST_PASS} || ${TEST_FAIL}

test-select-6: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/tab.txt -t ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-select-7: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/white.csv ${REDIRECT} ${TMP_DIR}/$@.out1
	@${CMP} ${TMP_DIR}/$@.out1 expected/$@.out1 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --whitespace-clean ${TEST_DATA_DIR}/test/white.csv ${REDIRECT} ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --whitespace-clean-no-newline ${TEST_DATA_DIR}/test/white.csv ${REDIRECT} ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL}

test-select-8: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/white_utf8.csv ${REDIRECT} ${TMP_DIR}/$@.out1
	@${CMP} ${TMP_DIR}/$@.out1 expected/$@.out1 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --whitespace-clean ${TEST_DATA_DIR}/test/white_utf8.csv ${REDIRECT} ${TMP_DIR}/$@.out2
	@${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} $< --whitespace-clean-no-newline ${TEST_DATA_DIR}/test/white_utf8.csv ${REDIRECT} ${TMP_DIR}/$@.out3
	@${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL}

test-select-9: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/test/quoted3.csv -q ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-select-fixed-1: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/fixed.csv --fixed 3,7,12,18,20,21,22 ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

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

test-stack: test-stack1 test-stack2

test-stack1: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/stack[12].csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack2: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_INIT}
	@${PREFIX} $< ${TEST_DATA_DIR}/stack2-[12].csv ${REDIRECT} ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-2tsv test-sql test-serialize test-flatten test-pretty : test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})
#	@if [ "$@" = "test-2tsv" ] ; then echo "TO DO: update 2tsv to output Excel format-- see data/Excel.tsv"; fi

test-sql: test-sql2 test-sql3 test-sql4
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
	@(${PREFIX} $< keys ${THIS_MAKEFILE_DIR}/../../docs/db.schema.json ${REDIRECT1} ${TMP_DIR}/$@.out)
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-2json: test-%: ${BUILD_DIR}/bin/zsv_%${EXE} ${BUILD_DIR}/bin/zsv_2db${EXE} ${BUILD_DIR}/bin/zsv_select${EXE} worldcitiespop_mil.csv
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})

#	ajv validate --strict-tuples=false -s ${THIS_MAKEFILE_DIR}/../../docs/csv.schema.json -d expected/$@.out.json [suffix must be json]

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


test-desc: test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_INIT}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX} $< -q < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} ${TMP_DIR}/$@.out && \
	${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})
	@(${PREFIX} $< < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT2} ${TMP_DIR}/$@.out2 && \
	${CMP} ${TMP_DIR}/$@.out2 expected/$@.out2 && ${TEST_PASS} || ${TEST_FAIL})
	@(${PREFIX} $< -H < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT2} ${TMP_DIR}/$@.out3 && \
	${CMP} ${TMP_DIR}/$@.out3 expected/$@.out3 && ${TEST_PASS} || ${TEST_FAIL})
