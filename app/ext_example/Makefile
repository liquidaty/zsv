#Makefile for use with GNU make

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

IS_WSL=$(shell grep -qi microsoft /proc/version 2>/dev/null && echo 1 || echo 0)

ifeq ($(WIN),0)
  EXE=
  EXT=
else
  EXE=.exe
  EXT=.win
endif

export CMP=cmp
export TMP_DIR=./../../tmp

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

ifeq ($(ZSVSHEET_BUILD),1)
  CFLAGS+=-DZSVSHEET_BUILD
endif

ifeq ($(ZSV_EXTRAS),1)
  CFLAGS+= -DZSV_EXTRAS
endif

DEBUG=0
ifeq ($(DEBUG),0)
  CFLAGS+= -O3 -DNDEBUG -std=gnu11 -Wno-gnu-statement-expression -Wshadow -Wall -Wextra -Wno-missing-braces -pedantic -DSTDC_HEADERS -D_GNU_SOURCE -lm -ftree-vectorize -flto
else
  CFLAGS += -g
endif

export THIS_LIB_BASE=$(shell cd ../.. && pwd)
CCBN=$(shell basename ${CC})
BUILD_DIR=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}
TARGET=${BUILD_DIR}/bin/zsvextmy.${SO}
TARGET_SHEET=${BUILD_DIR}/bin/zsvextmysheet.${SO}

COLOR_NONE=\033[0m
COLOR_GREEN=\033[1;32m
COLOR_RED=\033[1;31m
COLOR_BLUE=\033[1;34m
COLOR_PINK=\033[1;34m
COLOR_YELLOW=\033[1;33m

TEST_INIT=printf "${COLOR_PINK}$@: ${COLOR_NONE}\n"
TEST_SKIP=printf "${COLOR_BLUE}$@: ${COLOR_YELLOW}Skipped${COLOR_NONE}\n"
TEST_PASS=printf "${COLOR_BLUE}$@: ${COLOR_GREEN}Passed${COLOR_NONE}\n"
TEST_FAIL=(printf "${COLOR_BLUE}$@: ${COLOR_RED}Failed!${COLOR_NONE}\n" && exit 1)

EXPECT=../../scripts/test-expect.sh
export EXPECTED_PATH=test/expected

CFLAGS_SHARED=-shared
ifneq ($(findstring emcc,$(CC)),) # emcc
  CFLAGS_SHARED=-s SIDE_MODULE=1 -s LINKABLE=1
  INSTALLED_EXTENSION=${BINDIR}/zsvextmy.${SO}
else
  INSTALLED_EXTENSION=
endif

CFLAGS+= -I${THIS_LIB_BASE}/include -I${PREFIX}/include

all: ${TARGET} ${TARGET_SHEET}
	@echo Built ${TARGET}
	@echo Built ${TARGET_SHEET}

ifneq ($(findstring emcc,$(CC)),) # emcc
install: ${INSTALLED_EXTENSION}

${INSTALLED_EXTENSION}: ${TARGET} ${TARGET_SHEET}
	@mkdir -p `dirname "$@"`
	cp -p $< $@
endif

CLI=${BUILD_DIR}/bin/cli${EXE}

RUN_CLI=ZSV_CONFIG_DIR=${TMP_DIR} ${CLI}

${BUILD_DIR}/bin/cli:
	${MAKE} -C .. build-cli CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG}

${BUILD_DIR}/objs/utils/%.o:
	(cd .. && ${MAKE} CONFIGFILE=${CONFIGFILEPATH} CC=${CC} DEBUG=${DEBUG} $@ )

TESTS=test-1 test-2 test-3 test-4 test-5 test-thirdparty
ifeq ($(ZSVSHEET_BUILD),1)
  TESTS+=test-sheet-extension-1 test-sheet-extension-2
endif

test: ${TESTS}

test-1: test-%: ${CLI} ${TARGET}
	@${TEST_INIT}
	@${RUN_CLI} my-echo < ${THIS_LIB_BASE}/data/quoted.csv > ${TMP_DIR}/zsvext-$@.out
	@${CMP} ${TMP_DIR}/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-2: test-%: ${CLI} ${TARGET}
	@${TEST_INIT}
	@${RUN_CLI} my-echo -t < ${THIS_LIB_BASE}/data/hi.txt > ${TMP_DIR}/zsvext-$@.out
	@${CMP} ${TMP_DIR}/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-3: test-%: ${CLI} ${TARGET}
	@${TEST_INIT}
	@rm -f ${TMP_DIR}/zsvext-$@.out
	@${RUN_CLI} unregister my 2>/dev/null 1>/dev/null || [ 1==1 ]
	@${RUN_CLI} register my 2>&1 | grep -v [.]${SO} >> ${TMP_DIR}/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} unregister my 2>&1 | grep -v [.]${SO} >> ${TMP_DIR}/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} unregister my 2>&1 | grep -v [.]${SO} >> ${TMP_DIR}/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} help 2>&1 | grep -v [.]${SO} >> ${TMP_DIR}/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} register my 2>&1 | grep -v [.]${SO} >> ${TMP_DIR}/zsvext-$@.out || [ 1==1 ]
	@${RUN_CLI} help 2>&1 | grep -v [.]${SO} >> ${TMP_DIR}/zsvext-$@.out || [ 1==1 ]
	@${CMP} ${TMP_DIR}/zsvext-$@.out 2>&1 test/expected/zsvext-$@.out${EXT} && ${TEST_PASS} || ${TEST_FAIL}

test-4: test-%: ${CLI} ${TARGET}
	@${TEST_INIT}
	@${RUN_CLI} unregister my 2>/dev/null 1>/dev/null || [ 1==1 ]
	@${RUN_CLI} license > ${TMP_DIR}/zsvext-$@.out 2>> ${TMP_DIR}/zsvext-$@.err
	@${RUN_CLI} register my 2>/dev/null 1>/dev/null
	@${RUN_CLI} license >> ${TMP_DIR}/zsvext-$@.out 2> ${TMP_DIR}/zsvext-$@.err
	@${CMP} ${TMP_DIR}/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}
	@${CMP} ${TMP_DIR}/zsvext-$@.err test/expected/zsvext-$@.err && ${TEST_PASS} || ${TEST_FAIL}

test-5: test-%:${TARGET}
	@${TEST_INIT}
	@${RUN_CLI} my-count newline.csv 2>/dev/null > ${TMP_DIR}/zsvext-$@.out
	@${RUN_CLI} my-count -q newline.csv 2>/dev/null >> ${TMP_DIR}/zsvext-$@.out
	@${CMP} ${TMP_DIR}/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

test-thirdparty: test-%: ${CLI} ${TARGET}
	@${TEST_INIT}
	@${RUN_CLI} unregister my 2>/dev/null 1>/dev/null || [ 1==1 ]
	@${RUN_CLI} thirdparty > ${TMP_DIR}/zsvext-$@.out
	@${RUN_CLI} register my 2>/dev/null 1>/dev/null
	@${RUN_CLI} thirdparty >> ${TMP_DIR}/zsvext-$@.out
	@${CMP} ${TMP_DIR}/zsvext-$@.out test/expected/zsvext-$@.out && ${TEST_PASS} || ${TEST_FAIL}

../test/worldcitiespop_mil.csv:
	make -C ../test worldcitiespop_mil.csv

DATE_TIME:=$(shell date +%F-%H-%M-%S)
export TIMINGS_CSV:=${TMP_DIR}/timings-${DATE_TIME}.csv
TMUX_TERM=xterm-256color
test-sheet-extension-1: ${CLI} ${TARGET} ../test/worldcitiespop_mil.csv
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@rm -f ${TMP_DIR}/zsvext-$@.out tmux-*.log
	@tmux kill-session -t $@ || echo 'No tmux session to kill'
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@${RUN_CLI} unregister my 2>/dev/null 1>/dev/null || [ 1==1 ]
	@${RUN_CLI} register my 2>&1 | grep -v [.]${SO} || [ 1==1 ]
	@(ZSV_CONFIG_DIR=${TMP_DIR} tmux -v new-session -x 80 -y 5 -d -s $@ "$< sheet ../test/worldcitiespop_mil.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "t" "hello" Enter Up Down && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

test-sheet-extension-2: ${CLI} ${TARGET}
	@${TEST_INIT}
ifeq ($(IS_WSL),1)
	@${TEST_SKIP}
else
	@rm -f ${TMP_DIR}/zsvext-$@.out tmux-*.log
	@tmux kill-session -t $@ || echo 'No tmux session to kill'
	@echo 'set-option default-terminal "${TMUX_TERM}"' > ~/.tmux.conf
	@${RUN_CLI} unregister my 2>/dev/null 1>/dev/null || [ 1==1 ]
	@${RUN_CLI} register my 2>&1 | grep -v [.]${SO} || [ 1==1 ]
	@(ZSV_CONFIG_DIR=${TMP_DIR} tmux -v new-session -x 120 -y 5 -d -s $@ "$< sheet -d 3 ../../data/test/mixed-line-endings.csv" && \
	${EXPECT} $@ indexed && \
	tmux send-keys -t $@ "T" && \
	${EXPECT} $@ transformed && \
	tmux send-keys -t $@ "G" && \
	${EXPECT} $@ && ${TEST_PASS} || ${TEST_FAIL})
endif

clean:
	@rm -f ${TARGET} ${TARGET_SHEET} ${TMP_DIR}/zsvext-test*.out

${BUILD_DIR}/objs/%.o : ${THIS_LIB_BASE}/src/%.c ${PARSER_DEPS}
	${MAKE} -C ${THIS_LIB_BASE}/src CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG} WIN=${WIN} $@

LIBZSV=${PREFIX}/lib/libzsv.a
${LIBZSV}:
	${MAKE} -C ${THIS_LIB_BASE}/src CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG} WIN=${WIN} install

LIBZSVUTIL=${PREFIX}/lib/libzsvutil.a
${LIBZSVUTIL}:
	${MAKE} -C ${THIS_LIB_BASE}/app CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG} WIN=${WIN} install-util-lib

YAJL_SRC_DIR=${THIS_MAKEFILE_DIR}/../external/yajl
YAJL_INCLUDE=-I${YAJL_SRC_DIR}/build/yajl-2.1.1/include
YAJL_HELPER_INCLUDE=-I${THIS_MAKEFILE_DIR}/../external/yajl_helper
${TARGET_SHEET}: LIBS="../external/sqlite3/sqlite3.c" -lzsvutil -lzsv -L${PREFIX}/lib -ljq -lutf8proc
${TARGET}: LIBS=-lzsvutil -lzsv -L${PREFIX}/lib
${TARGET} ${TARGET_SHEET}: ${BUILD_DIR}/bin/zsvext%.${SO} : %_extension.c ${LIBZSV} ${LIBZSVUTIL}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${CFLAGS_SHARED} $< -o $@ ${LIBS} ${YAJL_INCLUDE} ${YAJL_HELPER_INCLUDE}

.PHONY: all test test-% clean install ${PREFIX}/lib/libzsvutil.a
