## Copyright (C) 2022 Guarnerix Inc dba Liquidaty - All Rights Reserved
## Unauthorized copying of this file, via any medium is strictly prohibited
## Proprietary and confidential
##

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))

### helper funcs
COLOR_NONE=\033[0m
COLOR_GREEN=\033[1;32m
COLOR_RED=\033[1;31m
COLOR_BLUE=\033[1;34m
COLOR_PINK=\033[1;35m
COLOR_YELLOW=\033[1;33m

TMP_DIR=${THIS_MAKEFILE_DIR}/tmp

TEST_INIT=mkdir -p ${TMP_DIR} && rm -rf ${TMP_DIR}/test* && printf "${COLOR_PINK}$@: ${COLOR_NONE}\n"
TEST_SKIP=printf "${COLOR_BLUE}$@: ${COLOR_YELLOW}Skipped${COLOR_NONE}\n"
TEST_PASS=printf "${COLOR_BLUE}$@: ${COLOR_GREEN}Passed${COLOR_NONE}\n"
TEST_FAIL=(printf "${COLOR_BLUE}$@: ${COLOR_RED}Failed!${COLOR_NONE}\n" && exit 1)

LEAKS=
ifneq ($(LEAKS),)
  PREFIX=leaks 2>/dev/null --atExit --
  SUFFIX=>${TMP_DIR}/leaks.txt; grep leak ${TMP_DIR}/leaks.txt | grep bytes \# # stop processing at this step
  CHECK=\# # don't run this step
else
  PREFIX=
  SUFFIX=>${TMP_DIR}/$@.out
  CHECK=
endif

ifeq ($(SELECT_EXE),)
  $(error Missing SELECT_EXE)
endif

ifeq ($(COUNT_EXE),)
  $(error Missing COUNT_EXE)
endif

$(info SELECT_EXE=$(SELECT_EXE) COUNT_EXE=$(COUNT_EXE))

help:
	@echo "# targets:"
	@echo "#    make test # run all tests"
	@echo "#    make test-parallel-1|test-parallel-2|test-parallel-3 # run individual tests"

test: test-parallel-1 test-parallel-2 test-parallel-3 test-parallel-4 test-parallel-5

test-parallel-1: $(SELECT_EXE) ../worldcitiespop_mil.csv
	@${TEST_INIT}
	@${SELECT_EXE} ../worldcitiespop_mil.csv > ${TMP_DIR}/$@.serial.tmp
	@mv ${TMP_DIR}/$@.serial.tmp ${TMP_DIR}/$@.serial
	@${SELECT_EXE} ../worldcitiespop_mil.csv -j 4 > ${TMP_DIR}/$@.parallel.tmp
	@mv ${TMP_DIR}/$@.parallel.tmp ${TMP_DIR}/$@.parallel
	@cmp ${TMP_DIR}/$@.serial ${TMP_DIR}/$@.parallel && ${TEST_PASS} || ${TEST_FAIL}
	@rm -f ${TMP_DIR}/$@.serial.tmp ${TMP_DIR}/$@.serial ${TMP_DIR}/$@.parallel.tmp ${TMP_DIR}/$@.parallel

test-parallel-2: $(COUNT_EXE) ../worldcitiespop_mil.csv
	@${TEST_INIT}
	@${COUNT_EXE} ../worldcitiespop_mil.csv > ${TMP_DIR}/$@.serial.tmp
	@mv ${TMP_DIR}/$@.serial.tmp ${TMP_DIR}/$@.serial
	@${COUNT_EXE} ../worldcitiespop_mil.csv -j 4 > ${TMP_DIR}/$@.parallel.tmp
	@mv ${TMP_DIR}/$@.parallel.tmp ${TMP_DIR}/$@.parallel
	@cmp ${TMP_DIR}/$@.serial ${TMP_DIR}/$@.parallel && ${TEST_PASS} || ${TEST_FAIL}
	@rm -f ${TMP_DIR}/$@.serial.tmp ${TMP_DIR}/$@.serial ${TMP_DIR}/$@.parallel.tmp ${TMP_DIR}/$@.parallel

chunk_break.csv: ../worldcitiespop_mil.csv
	@head -100000 ../worldcitiespop_mil.csv > chunk_break.csv.tmp
	@git apply chunk_break.diff
	@mv chunk_break.csv.tmp chunk_break.csv

${TMP_DIR}/chunk_clean.csv: chunk_break.csv
	@${SELECT_EXE} --whitespace-clean-no-newline < chunk_break.csv > $@

test-parallel-3: $(SELECT_EXE) chunk_break.csv ${TMP_DIR}/chunk_clean.csv
	@${TEST_INIT}
	@${SELECT_EXE} chunk_break.csv -j 3 --whitespace-clean-no-newline > ${TMP_DIR}/$@.out -v 2>${TMP_DIR}/$@.err
	@cmp ${TMP_DIR}/$@.out ${TMP_DIR}/chunk_clean.csv && ${TEST_PASS} || ${TEST_FAIL}
	@cmp ${TMP_DIR}/$@.err $@.expected.err && ${TEST_PASS} || ${TEST_FAIL}

test-parallel-4: $(COUNT_EXE) chunk_break.csv
	@${TEST_INIT}
	@${COUNT_EXE} chunk_break.csv -j 3 > ${TMP_DIR}/$@.out -v 2>${TMP_DIR}/$@.err
	@cmp ${TMP_DIR}/$@.out $@.expected.out && ${TEST_PASS} || ${TEST_FAIL}
	@cmp ${TMP_DIR}/$@.err $@.expected.err && ${TEST_PASS} || ${TEST_FAIL}

test-parallel-5: $(SELECT_EXE) ../worldcitiespop_mil.csv
	@${TEST_INIT}
	@${SELECT_EXE} --regex-search '^[BSZ]an Tala[qt]' ../worldcitiespop_mil.csv > ${TMP_DIR}/$@.out
	@${SELECT_EXE} -j 10 --regex-search '^[BSZ]an Tala[qt]' ../worldcitiespop_mil.csv > ${TMP_DIR}/$@.parallel.out
	@cmp ${TMP_DIR}/$@.out ${TMP_DIR}/$@.parallel.out && ${TEST_PASS} || ${TEST_FAIL}
	@[ `wc -l < ${TMP_DIR}/$@.out` = 49 ] && ${TEST_PASS} || ${TEST_FAIL}

clean:
	@rm -rf ${TMP_DIR}

.PHONY: test% clean
