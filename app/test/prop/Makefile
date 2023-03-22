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
TEST_INIT=mkdir -p ${TMP_DIR} && rm -rf ${TMP_DIR}/test* && echo "${COLOR_PINK}$@: ${COLOR_NONE}"

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

test: test-1 test-2 test-3 test-4 test-5 test-6 test-7 test-8 test-copy test-clean test-export test-import clean

test-1:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -R 2 ${SUFFIX}
	@touch .zsv/data/dummy.csv/hello.json
	@${PREFIX} ${EXE} dummy.csv --clear ${SUFFIX}
	@ls .zsv/data/dummy.csv/hello.json 2>/dev/null && ${TEST_FAIL} || ${TEST_PASS}
	@${CHECK} [ "`${EXE} dummy.csv`" = "{}" ] && ${TEST_PASS} || ${TEST_FAIL}

test-2:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -R 2 ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" = '{"skip-head":2}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-3:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -d 3 ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" = '{"header-row-span":3,"skip-head":2}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-4:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -R - ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" = '{"header-row-span":3}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-5:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -d - ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" = '{}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-6:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv -d 3 ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv|jq -c -S`" = '{"header-row-span":3}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-7:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv --clear ${SUFFIX}
	@${CHECK} [ "`${EXE} dummy.csv`" = "{}" ] && ${TEST_PASS} || ${TEST_FAIL}

test-8:
	@${TEST_INIT}
	@${CHECK} ${EXE} detect.csv --clear
	@${PREFIX} ${EXE} detect.csv --auto --save ${SUFFIX}
	@${CHECK} [ "`${EXE} detect.csv --list-files`" = "props.json" ] && ${TEST_PASS} || ${TEST_FAIL}
	@${CHECK} [ "`${EXE} detect.csv|jq -c -S`" = '{"header-row-span":2,"skip-head":2}' ] && ${TEST_PASS} || ${TEST_FAIL}
	@${EXE} detect.csv --clear

test-copy:
	@${TEST_INIT}
	@rm -rf ${TMP_DIR}/$@
	@mkdir -p ${TMP_DIR}/$@/.zsv/data/abc.csv
	@echo '{}' > ${TMP_DIR}/$@/.zsv/data/abc.csv/props.json
	@touch ${TMP_DIR}/$@/abc.csv
	@[ "`${EXE} ${TMP_DIR}/$@/abc.csv --copy ${TMP_DIR}/$@/def.csv 2>&1`" = "${TMP_DIR}/$@/def.csv: No such file or directory" ] && ${TEST_PASS} || ${TEST_FAIL}
	@touch ${TMP_DIR}/$@/def.csv
	@${EXE} ${TMP_DIR}/$@/abc.csv --copy ${TMP_DIR}/$@/def.csv
	@cmp ${TMP_DIR}/$@/.zsv/data/abc.csv/props.json ${TMP_DIR}/$@/.zsv/data/def.csv/props.json && ${TEST_PASS} || ${TEST_FAIL}

test-clean:
	@${TEST_INIT}
	@rm -rf ${TMP_DIR}/$@
	@mkdir -p ${TMP_DIR}/$@/.zsv/data/abc.csv
	@echo '{}' > ${TMP_DIR}/$@/.zsv/data/abc.csv/props.json
	@mkdir -p ${TMP_DIR}/$@/.zsv/data/def.csv
	@echo '{}' > ${TMP_DIR}/$@/.zsv/data/def.csv/props.json
	@touch ${TMP_DIR}/$@/abc.csv
	@${EXE} ${TMP_DIR}/$@ --clean
	@ [ ! -d ${TMP_DIR}/$@/.zsv/data/def.csv ] && [ -d ${TMP_DIR}/$@/.zsv/data/abc.csv ] && ${TEST_PASS} || ${TEST_FAIL}

test-export:
	@${TEST_INIT}
	@rm -rf ${TMP_DIR}/$@
	@mkdir -p ${TMP_DIR}/$@/.zsv/data/abc.csv
	@echo '{"a":1}' > ${TMP_DIR}/$@/.zsv/data/abc.csv/props.json
	@touch ${TMP_DIR}/$@/abc.csv
	@ [ "`${EXE} ${TMP_DIR}/$@/abc.csv --export - | jq -c`" = '{"props.json":{"a":1}}' ] && ${TEST_PASS} || ${TEST_FAIL}

test-import:
	@${TEST_INIT}
	@rm -rf ${TMP_DIR}/$@
	@mkdir -p ${TMP_DIR}/$@/.zsv/data/abc.csv
	@[ "`echo '{"props.json":{"b":1}}' | ${EXE} ${TMP_DIR}/$@/def.csv --import - 2>&1`" = "${TMP_DIR}/$@/def.csv: No such file or directory" ] && ${TEST_PASS} || ${TEST_FAIL}
	@echo '{"props.json":{"b":1}}' | ${EXE} ${TMP_DIR}/$@/def.csv --import - -f
	@[ "`jq -c < ${TMP_DIR}/$@/.zsv/data/def.csv/props.json`" = '{"b":1}' ] && ${TEST_PASS} || ${TEST_FAIL}
	@touch ${TMP_DIR}/$@/def.csv
	@[ "`echo '{"props.json":{"b":1}}' | ${EXE} ${TMP_DIR}/$@/def.csv --import - 2>&1`" = "${TMP_DIR}/$@/.zsv/data/def.csv/props.json: File exists" ] && ${TEST_PASS} || ${TEST_FAIL}
	@[ "`echo '{"props.json":{"b":1}}' | ${EXE} ${TMP_DIR}/$@/def.csv --import - -f --dry`" = "${TMP_DIR}/$@/.zsv/data/def.csv/props.json" ] && ${TEST_PASS} || ${TEST_FAIL}

clean:
	@rm -rf ${TMP_DIR}

.PHONY: test% clean
