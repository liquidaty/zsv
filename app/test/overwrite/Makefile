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

TEST_INIT=mkdir -p ${TMP_DIR} && rm -rf ${TMP_DIR}/test* && printf "${COLOR_PINK}test-overwrite-$@: ${COLOR_NONE}\n"
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

help:
	@echo "# run all tests:"
	@echo "    make test"

ifeq ($(EXE),)
  $(error EXE is not defined)
endif

test: \
	test-1 test-2 test-excel-cells test-add test-remove test-old-value test-force \
	test-bulk-add test-bulk-remove test-echo-overwrite-auto test-timestamp

test-1:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv clear
	@${CHECK} [ "`${EXE} dummy.csv list`" = "row,column,value,timestamp,author" ] && ${TEST_PASS} || ${TEST_FAIL}

TIMESTAMP=$(shell date +"%s")
HEADER=row,column,value,timestamp,author

test-2:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv clear
	@${PREFIX} ${EXE} dummy.csv add 2-1 ABC --no-timestamp
	@${CHECK} [ "`${EXE} dummy.csv list`" = "`printf \"$(HEADER)\n2,1,ABC,,\n\"`" ] && ${TEST_PASS} || ${TEST_FAIL}

test-excel-cells:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv clear
	@${PREFIX} ${EXE} dummy.csv add C2 EXCEL --no-timestamp
	@${CHECK} [ "`${EXE} dummy.csv list`" = "`printf \"$(HEADER)\n1,2,EXCEL,,\n\"`" ] && ${TEST_PASS} || ${TEST_FAIL}
	@${CHECK} [ "`${EXE} dummy.csv list --A1`" = "`printf \"$(HEADER)\nC2,EXCEL,,\n\"`" ] && ${TEST_PASS} || ${TEST_FAIL}

test-add:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear
	@${PREFIX} ${EXE} dummy2.csv add 1-2 VAL1 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 2-2 VAL2 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 2-3 VAL3 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 2-4 VAL4 --no-timestamp
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,2,VAL1,,\n2,2,VAL2,,\n2,3,VAL3,,\n2,4,VAL4,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${ECHO_EXE} --apply-overwrites dummy2.csv > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-remove:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear
	@${PREFIX} ${EXE} dummy2.csv add 1-1 V1 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 2-1 V2 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 3-1 V3 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 4-1 V4 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv remove 2-1
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,1,V1,,\n3,1,V3,,\n4,1,V4,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${EXE} dummy2.csv remove 4-1
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,1,V1,,\n3,1,V3,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${EXE} dummy2.csv remove --all
	@${CHECK} [ ! -f ".zsv/overwrite/dummy2.csv/overwrite.sqlite3" ] \
			&& ${TEST_PASS} || ${TEST_FAIL}

test-old-value:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear
	@${PREFIX} ${EXE} dummy2.csv add 1-1 V1 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 2-1 V2 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 3-1 V3 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 4-1 V4 --no-timestamp
	@${CHECK} [ "`printf \"$$(${PREFIX} ${EXE} dummy2.csv add 3-1 NEW_V4 --no-timestamp 2>&1)\"`" = "`cat expected/$@-fail.out`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${EXE} dummy2.csv remove 2-1 --old-value V3
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,1,V1,,\n2,1,V2,,\n3,1,V3,,\n4,1,V4,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${EXE} dummy2.csv add 2-1 NEW_V2 --old-value V2 --no-timestamp
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,1,V1,,\n2,1,NEW_V2,,\n3,1,V3,,\n4,1,V4,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${EXE} dummy2.csv remove 3-1 --old-value V3
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,1,V1,,\n2,1,NEW_V2,,\n4,1,V4,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}

test-force:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear
	@${PREFIX} ${EXE} dummy2.csv add 1-2 VAL1 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 2-2 VAL2 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 2-3 VAL3 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv add 2-4 VAL4 --no-timestamp
	@${CHECK} [ "`printf \"$$(${PREFIX} ${EXE} dummy2.csv add 2-4 NEW_VAL4 --no-timestamp 2>&1)\"`" = "`cat expected/$@-fail.out`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,2,VAL1,,\n2,2,VAL2,,\n2,3,VAL3,,\n2,4,VAL4,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}
	@${PREFIX} ${EXE} dummy2.csv add 2-4 NEW_VAL4 --no-timestamp --force
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,2,VAL1,,\n2,2,VAL2,,\n2,3,VAL3,,\n2,4,NEW_VAL4,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}

test-bulk-add:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear
	@${PREFIX} ${EXE} dummy2.csv add 1-1 OLD_V1 --no-timestamp # for old value test
	@${PREFIX} ${EXE} dummy2.csv add 2-1 OLD_VAL2 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv bulk-add overwrite.csv
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n1,1,VAL1,1731620001,\n2,1,OLD_VAL2,,\n3,1,VAL3,,\n4,1,VAL4,1731620001,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}

test-bulk-remove:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear
	@${PREFIX} ${EXE} dummy2.csv add 1-1 OLD_V1 --no-timestamp # for old value test
	@${PREFIX} ${EXE} dummy2.csv add 2-1 OLD_VAL2 --no-timestamp
	@${PREFIX} ${EXE} dummy2.csv bulk-add overwrite.csv
	@${PREFIX} ${EXE} dummy2.csv bulk-remove remove.csv
	@${CHECK} [ "`${EXE} dummy2.csv list`" = "`printf \"$(HEADER)\n3,1,VAL3,,\n\"`" ] \
		&& ${TEST_PASS} || ${TEST_FAIL}

STAT_MOD_TS=$(shell if stat -c %Y Makefile >/dev/null 2>/dev/null; then echo 'stat -c %Y' ; else echo 'stat -f %m' ; fi)
test-timestamp:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy2.csv clear
	@(${PREFIX} ${EXE} dummy2.csv add 1-2 ABC && ${STAT_MOD_TS} .zsv/data/dummy2.csv/overwrite.sqlite3 > ${TMP_DIR}/timestamp1.txt)
	@(${PREFIX} ${EXE} dummy2.csv add 1-3 ABC && ${STAT_MOD_TS} .zsv/data/dummy2.csv/overwrite.sqlite3 > ${TMP_DIR}/timestamp2.txt)
	@{ \
		EXPECTED_TS1=`cat ${TMP_DIR}/timestamp1.txt`; \
		EXPECTED_TS2=`cat ${TMP_DIR}/timestamp2.txt`; \
		LOWER_TS1=$$((EXPECTED_TS1 - 3)); \
		LOWER_TS2=$$((EXPECTED_TS2 - 3)); \
		OUTPUT="`${EXE} dummy2.csv list | tr -d '\n'`"; \
		echo "$$OUTPUT" | grep -q "${HEADER}1,2,ABC,\($$LOWER_TS1\|$$EXPECTED_TS1\),1,3,ABC,\($$LOWER_TS2\|$$EXPECTED_TS2\)," && ${TEST_PASS} || ${TEST_FAIL}; \
	}

test-echo-overwrite-auto:
	@${TEST_INIT}
	@${PREFIX} ${EXE} dummy.csv clear
	@${PREFIX} ${EXE} dummy.csv add 1-0 ABC
	@${PREFIX} ${EXE} dummy.csv add 2-2 123
	@${ECHO_EXE} --apply-overwrites dummy.csv > ${TMP_DIR}/$@.out
	@${CMP} ${TMP_DIR}/$@.out expected/$@.out && ${TEST_PASS} || ${TEST_FAIL}

clean:
	@rm -rf ${TMP_DIR}

.PHONY: help test% clean
