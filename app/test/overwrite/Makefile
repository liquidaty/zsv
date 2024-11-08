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
TEST_INIT=mkdir -p ${TMP_DIR} && rm -rf ${TMP_DIR}/test* && echo "${COLOR_PINK}test-overwrite-$@: ${COLOR_NONE}"

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

test: test-1 test-2 test-excel-cells test-add test-echo-overwrite-auto

test-1:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv clear 
	@${CHECK} [ "`${EXE} dummy.csv list`" = "row,column,value,timestamp,author" ] && ${TEST_PASS} || ${TEST_FAIL}

TIMESTAMP=$(shell date +"%s")
HEADER=row,column,value,timestamp,author

test-2:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv clear 
	@${PREFIX} ${EXE} dummy.csv add 2-1 ABC && date +"%s" > ${TMP_DIR}/timestamp1.txt
	@${CHECK} [ "`${EXE} dummy.csv list`" = "`printf \"$(HEADER)\n2,1,ABC,$$(cat ${TMP_DIR}/timestamp1.txt),\n\"`" ] && ${TEST_PASS} || ${TEST_FAIL}

test-excel-cells:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv clear 
	@${PREFIX} ${EXE} dummy.csv add C2 EXCEL && date +"%s" > ${TMP_DIR}/timestamp1.txt
	@${CHECK} [ "`${EXE} dummy.csv list`" = "`printf \"$(HEADER)\n1,2,EXCEL,$$(cat ${TMP_DIR}/timestamp1.txt),\n\"`" ] && ${TEST_PASS} || ${TEST_FAIL}

test-add:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear 
	@(${PREFIX} ${EXE} dummy2.csv add 1-2 VAL1 && date +"%s" > ${TMP_DIR}/timestamp1.txt)
	@(${PREFIX} ${EXE} dummy2.csv add 2-2 VAL2 && date +"%s" > ${TMP_DIR}/timestamp2.txt)
	@(${PREFIX} ${EXE} dummy2.csv add 2-3 VAL3 && date +"%s" > ${TMP_DIR}/timestamp3.txt)
	@(${PREFIX} ${EXE} dummy2.csv add 2-4 VAL4 && date +"%s" > ${TMP_DIR}/timestamp4.txt)
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,2,VAL1,$$(cat ${TMP_DIR}/timestamp1.txt),\n2,2,VAL2,$$(cat ${TMP_DIR}/timestamp2.txt),\n2,3,VAL3,$$(cat ${TMP_DIR}/timestamp3.txt),\n2,4,VAL4,$$(cat ${TMP_DIR}/timestamp4.txt),\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${ECHO_EXE} --overwrite-auto dummy2.csv > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-no-timestamp:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear 
	@${PREFIX} ${EXE} dummy2.csv add 1-2 ABC && date +"%s" > ${TMP_DIR}/timestamp1.txt
	@${PREFIX} ${EXE} dummy2.csv add 2-2 XYZ --no-timestamp
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,2,ABC,$$(cat ${TMP_DIR}/timestamp1.txt),\n2,2,XYZ,,\n\"`" ] && ${TEST_PASS} || ${TEST_FAIL}

test-echo-overwrite-auto:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv clear 
	@${PREFIX} ${EXE} dummy.csv add 1-0 ABC
	@${PREFIX} ${EXE} dummy.csv add 2-2 123 
	@${ECHO_EXE} --overwrite-auto dummy.csv > ${TMP_DIR}/$@.out 
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

clean:
	@rm -rf ${TMP_DIR}

.PHONY: test% clean
