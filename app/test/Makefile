# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_DIR:=$(shell basename "${THIS_MAKEFILE_DIR}")
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

CONFIGFILE ?= ../../config.mk
$(info Using config file ${CONFIGFILE})
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
TEST_DATA_DIR=${THIS_LIB_BASE}/data

SOURCES=echo select sql 2json serialize flatten pretty desc stack
TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${SOURCES}))

TESTS=$(addprefix test-,${SOURCES})

COLOR_NONE=\033[0m
COLOR_GREEN=\033[1;32m
COLOR_RED=\033[1;31m
COLOR_BLUE=\033[1;34m
COLOR_PINK=\033[1;35m

TEST_PASS=echo "${COLOR_BLUE}$@: ${COLOR_GREEN}Passed${COLOR_NONE}"
TEST_FAIL=echo "${COLOR_BLUE}$@: ${COLOR_RED}Failed!${COLOR_NONE}"
TEST_NAME=echo "${COLOR_PINK}$@: ${COLOR_NONE}"

ARGS-sql='select [Loan Number] from data'

LEAKS=
ifneq ($(LEAKS),)
  PREFIX=leaks 2>/dev/null --atExit -- 
  REDIRECT=>/tmp/leaks.txt; grep leak /tmp/leaks.txt | grep bytes \# # stop processing at this step
  REDIRECT1=>/tmp/leaks.txt; grep leak /tmp/leaks.txt | grep bytes ) \# # stop processing at this step
  CMP=\# # don't run this step
else
  PREFIX=
  REDIRECT=>
  REDIRECT1=>
  CMP=cmp
endif


help:
	@echo "To run all tests: make test [LEAKS=1]"
	@echo "To run individual test: make test-xxx"
	@echo "  where xxx can be: ${SOURCES}"

test: ${TESTS}

.PHONY: help test test-% test-stack

test-echo: ${BUILD_DIR}/bin/zsv_echo${EXE}
	@${TEST_NAME}
	@${PREFIX}$< ${TEST_DATA_DIR}/loans_1.csv ${REDIRECT} /tmp/$@.out
	@${CMP} /tmp/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

worldcitiespop_mil.csv:
	curl -LOk 'https://burntsushi.net/stuff/worldcitiespop_mil.csv'

test-count: ${BUILD_DIR}/bin/zsv_count worldcitiespop_mil.csv
	@${TEST_NAME}
	@cat worldcitiespop_mil.csv | ${PREFIX}$< ${REDIRECT} /tmp/$@.out
	@${CMP} /tmp/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-select: test-select-n test-select-6

test-select-n: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_NAME}
	@${PREFIX}$< ${TEST_DATA_DIR}/loans_1.csv -u "?" -R 4 -d 2 ${REDIRECT} /tmp/test-select.out
	@${CMP} /tmp/test-select.out expected/test-select.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX}$< ${TEST_DATA_DIR}/test/embedded.csv -e 'X' ${REDIRECT} /tmp/test-select.2.out
	@${CMP} /tmp/test-select.2.out expected/test-select.2.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX}$< ${TEST_DATA_DIR}/test/embedded_dos.csv -e 'X' ${REDIRECT} /tmp/test-select.3.out
	@${CMP} /tmp/test-select.3.out expected/test-select.3.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX}$< ${TEST_DATA_DIR}/loans_1.csv -u "?" -R 4 -d 2 -N ${REDIRECT} /tmp/test-select.4.out
	@${CMP} /tmp/test-select.4.out expected/test-select.4.out && ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX}$<  ${TEST_DATA_DIR}/quoted.csv -e 'x' ${REDIRECT} /tmp/test-select.5.out
	@${CMP} /tmp/test-select.5.out expected/test-select.5.out && ${TEST_PASS} || ${TEST_FAIL}

test-select-6: ${BUILD_DIR}/bin/zsv_select${EXE}
	@${TEST_NAME}
	@${PREFIX}$<  ${TEST_DATA_DIR}/test/tab.txt -T ${REDIRECT} /tmp/$@.out
	@${CMP} /tmp/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack: test-stack1 test-stack2

test-stack1: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_NAME}
	@${PREFIX}$< ${TEST_DATA_DIR}/stack[12].csv ${REDIRECT} /tmp/$@.out
	@${CMP} /tmp/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-stack2: ${BUILD_DIR}/bin/zsv_stack${EXE}
	@${TEST_NAME}
	@${PREFIX}$< ${TEST_DATA_DIR}/stack2-[12].csv ${REDIRECT} /tmp/$@.out
	@${CMP} /tmp/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-2tsv test-sql test-2json test-serialize test-flatten test-pretty test-desc : test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@${TEST_NAME}
	@( ( ! [ -s "${TEST_DATA_DIR}/test/$*.csv" ] ) && echo "No test input for $*") || \
	(${PREFIX}$< ${ARGS-$*} < ${TEST_DATA_DIR}/test/$*.csv ${REDIRECT1} /tmp/$@.out && \
	${CMP} /tmp/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL})



