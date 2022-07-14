# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_DIR:=$(shell basename "${THIS_MAKEFILE_DIR}")
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

.POSIX:
.SUFFIXES:
.SUFFIXES: .o .c .a

CONFIGFILE ?= config.mk
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
  SO=so
  ifeq ($(BUILD_SUBDIR),Linux)
    CFLAGS+= -ldl -lpthread
  endif
else
  BUILD_SUBDIR=win/${DBG_SUBDIR}
  CFLAGS+= -D__USE_MINGW_ANSI_STDIO -D_ISOC99_SOURCE
  CFLAGS+= -Wl,--strip-all
  SO=dll
endif
CFLAGS+= ${CFLAGS_PIC}

ifeq ($(ZSV_EXTRAS),1)
  CFLAGS+= -DZSV_EXTRAS
endif

DEBUG=0
ifeq ($(DEBUG),0)
  CFLAGS+= -O3 -DNDEBUG -std=gnu11 -Wno-gnu-statement-expression -Wshadow -Wall -Wextra -Wno-missing-braces -pedantic -DSTDC_HEADERS -D_GNU_SOURCE -lm -mavx2 -ftree-vectorize -flto
else
  CFLAGS += -g
endif

THIS_LIB_BASE=$(shell cd ../.. && pwd)
CCBN=$(shell basename ${CC})
BUILD_DIR=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}
TARGET=${BUILD_DIR}/bin/zsvextmy.${SO}

COLOR_NONE=\033[0m
COLOR_GREEN=\033[1;32m
COLOR_RED=\033[1;31m
COLOR_BLUE=\033[1;34m
COLOR_PINK=\033[1;34m

TEST_PASS=echo "${COLOR_BLUE}$@: ${COLOR_GREEN}Passed${COLOR_NONE}"
TEST_FAIL=(echo "${COLOR_BLUE}$@: ${COLOR_RED}Failed!${COLOR_NONE}" && exit 1)
TEST_NAME=echo "${COLOR_PINK}$@: ${COLOR_NONE}"

UTILS1=
CFLAGS_SHARED=-shared
ifneq ($(findstring emcc,$(CC)),) # emcc
  CFLAGS_SHARED=-s SIDE_MODULE=1 -s LINKABLE=1
  INSTALLED_EXTENSION=${BINDIR}/zsvextmy.${SO}
else
  INSTALLED_EXTENSION=
endif

UTILS1+=writer
UTILS=$(addprefix ${BUILD_DIR}/objs/utils/,$(addsuffix .o,${UTILS1}))

CFLAGS+= -I${THIS_LIB_BASE}/include

all: ${TARGET}
	@echo Built ${TARGET}

ifneq ($(findstring emcc,$(CC)),) # emcc
install: ${INSTALLED_EXTENSION}

${INSTALLED_EXTENSION}: ${TARGET}
	@mkdir -p `dirname "$@"`
	cp -p $< $@
endif

CLI=${BUILD_DIR}/bin/cli

RUN_CLI=ZSV_CONFIG_DIR=/tmp ${CLI}

${BUILD_DIR}/bin/cli:
	(cd .. && make CC=${CC} CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG} ${BUILD_DIR}/bin/cli)

${BUILD_DIR}/objs/utils/%.o:
	(cd .. && make CONFIGFILE=${CONFIGFILEPATH} CC=${CC} DEBUG=${DEBUG} $@ )

test: test-1 test-2 test-3 test-4 test-5 test-thirdparty

test-1: test-%: ${CLI} ${TARGET}
	@${RUN_CLI} my-echo < ${THIS_LIB_BASE}/data/quoted.csv > /tmp/zsvext-$@.out
	@cmp /tmp/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-2: test-%: ${CLI} ${TARGET}
	@${RUN_CLI} my-echo -t < ${THIS_LIB_BASE}/data/hi.txt > /tmp/zsvext-$@.out
	@cmp /tmp/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-3: test-%: ${CLI} ${TARGET}
	@rm -f /tmp/zsvext-$@.out
	@${RUN_CLI} unregister my 2>/dev/null 1>/dev/null || [ 1==1 ]
	@${RUN_CLI} register my 2>&1 | grep -v [.]so >> /tmp/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} unregister my 2>&1 | grep -v [.]so >> /tmp/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} unregister my 2>&1 | grep -v [.]so >> /tmp/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} help 2>&1 | grep -v [.]so >> /tmp/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} register my 2>&1 | grep -v [.]so >> /tmp/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} help 2>&1 | grep -v [.]so >> /tmp/zsvext-$@.out || [ 1==1 ]
	@cmp /tmp/zsvext-$@.out 2>&1 test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-4: test-%: ${CLI} ${TARGET}
	@${RUN_CLI} unregister my 2>/dev/null 1>/dev/null || [ 1==1 ]
	@${RUN_CLI} license > /tmp/zsvext-$@.out 2>> /tmp/zsvext-$@.err
	@${RUN_CLI} register my 2>/dev/null 1>/dev/null
	@${RUN_CLI} license >> /tmp/zsvext-$@.out 2> /tmp/zsvext-$@.err
	@cmp /tmp/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}
	@cmp /tmp/zsvext-$@.err test/expected/zsvext-$@.err && ${TEST_PASS} || ${TEST_FAIL}

test-5: test-%:${TARGET}
	@${RUN_CLI} my-count newline.csv 2>/dev/null > /tmp/zsvext-$@.out
	@${RUN_CLI} my-count -q newline.csv 2>/dev/null >> /tmp/zsvext-$@.out
	@cmp /tmp/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-thirdparty: test-%: ${CLI} ${TARGET}
	@${RUN_CLI} unregister my 2>/dev/null 1>/dev/null || [ 1==1 ]
	@${RUN_CLI} thirdparty > /tmp/zsvext-$@.out
	@${RUN_CLI} register my 2>/dev/null 1>/dev/null
	@${RUN_CLI} thirdparty >> /tmp/zsvext-$@.out
	@cmp /tmp/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

clean:
	@rm -f ${TARGET} /tmp/zsvext-test*.out

${BUILD_DIR}/objs/%.o : ${THIS_LIB_BASE}/src/%.c ${PARSER_DEPS}
	make -C ${THIS_LIB_BASE}/src CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG} WIN=${WIN} $@

${TARGET}: my_extension.c ${UTILS}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${CFLAGS_SHARED} $< ${UTILS} -o $@

.PHONY: all test test-% clean install
