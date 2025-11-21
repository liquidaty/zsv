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

###

help:
	@echo "# run all tests:"
	@echo "    make test"

ifeq ($(EXE),)
  $(error EXE is not defined)
endif

test: test-parallel-1

test-parallel-1: $(EXE) ../worldcitiespop_mil.csv
	@${TEST_INIT}
	@${EXE} ../worldcitiespop_mil.csv > ${TMP_DIR}/$@.serial.tmp
	@mv ${TMP_DIR}/$@.serial.tmp ${TMP_DIR}/$@.serial
	@${EXE} ../worldcitiespop_mil.csv -j 4 > ${TMP_DIR}/$@.parallel.tmp
	@mv ${TMP_DIR}/$@.parallel.tmp ${TMP_DIR}/$@.parallel
	@cmp ${TMP_DIR}/$@.serial ${TMP_DIR}/$@.parallel && ${TEST_PASS} || ${TEST_FAIL}
	@rm -f ${TMP_DIR}/$@.serial.tmp ${TMP_DIR}/$@.serial ${TMP_DIR}/$@.parallel.tmp ${TMP_DIR}/$@.parallel

clean:
	@rm -rf ${TMP_DIR}

.PHONY: test% clean
