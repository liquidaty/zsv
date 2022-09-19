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

TMP_DIR=${THIS_MAKEFILE_DIR}/tmp

TEST_PASS=echo "${COLOR_BLUE}$@: ${COLOR_GREEN}Passed${COLOR_NONE}"
TEST_FAIL=(echo "${COLOR_BLUE}$@: ${COLOR_RED}Failed!${COLOR_NONE}" && exit 1)
TEST_INIT=mkdir -p ${TMP_DIR} && rm -f ${TMP_DIR}/test* && echo "${COLOR_PINK}$@: ${COLOR_NONE}"

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

test: test-1 test-2 test-3 test-4 test-5 test-6 test-7 test-8 clean

test-1:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv --clear ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv`" == "{}" ] && ${TEST_PASS} || ${TEST_FAIL}

test-2:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -R 2 ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" == '{"skip-head":2}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-3:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -d 3 ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" == '{"header-row-span":3,"skip-head":2}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-4:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -R - ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" == '{"header-row-span":3}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-5:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -d - ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" == '{}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-6:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -d 3 ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" == '{"header-row-span":3}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-7:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv --clear ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv`" == "{}" ] && ${TEST_PASS} || ${TEST_FAIL}

test-8:
	@${TEST_INIT}
	@${CHECK} ${EXE} detect.csv --clear
	@${PREFIX} ${EXE} detect.csv --auto --save ${SUFFIX}
	@${CHECK} [ "`${EXE} detect.csv|jq -c -S`" == '{"header-row-span":2,"skip-head":2}' ] && ${TEST_PASS} || ${TEST_FAIL}
	@${EXE} detect.csv --clear

clean:
	@rm -rf ${TMP_DIR}

.PHONY: test% clean
