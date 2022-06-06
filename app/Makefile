# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

CONFIGFILE ?= ../config.mk
ifneq ($(CONFIGFILE),)
  ifeq ($(shell realpath `dirname ${CONFIGFILE}`),`realpath .`)
    $(error howdydy)
  endif
endif

$(info Using config file ${CONFIGFILE})
include ${CONFIGFILE}

CONFIGFILEPATH=$(shell ls ${CONFIGFILE} >/dev/null 2>/dev/null && realpath ${CONFIGFILE})
ifeq ($(CONFIGFILEPATH),)
  $(error Config file ${CONFIGFILE} not found)
endif

CC ?= cc

CFLAGS_EXE ?=

LIBZSV_A=libzsv.a
LIBZSV=${BUILD_DIR}/lib/${LIBZSV_A}
LIBZSV_INSTALL=$(PREFIX)/lib/${LIBZSV_A}

STRING_LIB_INCLUDE=
STRINGLIB_INCLUDE_FLAG=
ifneq ($(STRING_LIB_INCLUDE),)
  CFLAGS+= -DSTRING_LIB_INCLUDE='${STRING_LIB_INCLUDE}'
endif

STRING_LIB_OBJ=

DEBUG=0
WIN=
ifeq ($(WIN),)
  WIN=0
  ifneq ($(findstring w64,$(CC)),) # e.g. mingw64
    WIN=1
  endif
endif

MORE_OBJECTS=
NO_LTO=0

ifeq ($(DEBUG),1)
  DBG_SUBDIR+=dbg
else
  DBG_SUBDIR+=rel
endif

NO_STDIN=
ifeq ($(NO_STDIN),1)
  CFLAGS+= -DNO_STDIN
endif

CFLAGS+= ${CFLAGS_PIC}
ifeq ($(WIN),0)
  BUILD_SUBDIR=$(shell uname)/${DBG_SUBDIR}
  EXE=

  ifneq ($(findstring emcc,$(CC)),) # emcc
    NO_THREADING ?= 1
    EXE=.em.js
    EXPORTED_FUNCTIONS='_free','_malloc'
    CFLAGS+= -s EXPORTED_FUNCTIONS="[${EXPORTED_FUNCTIONS}]" -s EXPORTED_RUNTIME_METHODS="['setValue','allocateUTF8','stringToUTF8Array','lengthBytesUTF8','writeArrayToMemory']" -s RESERVED_FUNCTION_POINTERS=4 -s ALLOW_MEMORY_GROWTH=1
    CFLAGS_EXE+= -s FORCE_FILESYSTEM=1 -lidbfs.js -s MAIN_MODULE
    ifeq ($(NO_THREADING),0)
      CFLAGS_EXE+= -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=2 -s PTHREAD_POOL_SIZE=8
    endif
  endif
else
  BUILD_SUBDIR=win/${DBG_SUBDIR}
  EXE=.exe
  CFLAGS+= -D__USE_MINGW_ANSI_STDIO -D_ISOC99_SOURCE
  CFLAGS+= -Wl,--strip-all
endif

ifeq ($(DEBUG),0)
  CFLAGS+= -O3 -DNDEBUG -std=gnu11 -Wno-gnu-statement-expression -Wshadow -Wall -Wextra -Wno-missing-braces -pedantic -DSTDC_HEADERS -D_GNU_SOURCE -ftree-vectorize ${CFLAGS_OPT}

  ifeq ($(PGO),1)
    CFLAGS+= -fprofile-generate -fprofile-dir=/tmp/p4
  else
    ifeq ($(PGO),2)
      CFLAGS+= -fprofile-use=/tmp/p4
    else
      $(echo "No profiling set. To use PGO, compile with PGO=1, then run with data, then compile again with PGO=2")
    endif
  endif

else
  CFLAGS += -g
endif

CCBN=$(shell basename ${CC})
CFLAGS+= -I${PREFIX}/include
THIS_LIB_BASE=$(shell cd .. && pwd)
INCLUDE_DIR=${THIS_LIB_BASE}/include
BUILD_DIR=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}
UTILS1=writer file err signal mem clock arg dl string dirs
LDFLAGS=

ZSV_EXTRAS ?=
ifneq ($(WIN),0)
 UTILS1+= os
endif

ifneq ($(findstring emcc,$(CC)),) # emcc
  ZSV_EXTRAS=1
  UTILS1+=emcc/fs_api
  ifeq ($(NO_THREADING),0)
    LDFLAGS+=-pthread
  endif
else # not emcc
  CFLAGS+= ${CFLAGS_AVX2}
  LDFLAGS+=-lpthread # Linux explicitly requires
endif
UTILS=$(addprefix ${BUILD_DIR}/objs/utils/,$(addsuffix .o,${UTILS1}))

ifeq ($(ZSV_EXTRAS),1)
  CFLAGS+= -DZSV_EXTRAS
endif

OBJECTS=${UTILS}

ifeq ($(NO_MEMMEM),1)
  OBJECTS+= ${BUILD_DIR}/objs/utils/memmem.o
  CFLAGS+=-DNO_MEMMEM
endif

ZSV=$(BINDIR)/zsv${EXE}

SOURCES= echo count select 2json serialize flatten pretty stack desc 2tsv sql # noop
CLI_SOURCES=select desc count pretty sql flatten 2json 2tsv serialize stack

TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${SOURCES})) ${CLI}

BUILDS=$(addprefix build-,${SOURCES})
CLEANS=$(addprefix clean-,${SOURCES})
TESTS=$(addprefix test-,${SOURCES})

## cli
VERSION = $(shell git describe --always --dirty 2>/dev/null || echo "v0.0-zsv")
CLI_APP_OBJECT=${BUILD_DIR}/objs/cli_cli.o
CLI=${BUILD_DIR}/bin/cli${EXE}
ifneq ($(findstring emcc,$(CC)),) # emcc
    CLI_ADDITIONAL=${BUILD_DIR}/bin/cli.em.wasm
endif
CLI_OBJECTS=$(addprefix ${BUILD_DIR}/objs/cli_,$(addsuffix .o,${CLI_SOURCES}))

USE_SGLIB=flatten stack desc
USE_SGLIB_TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${USE_SGLIB}))

USE_JSONWRITER=2json desc
USE_JSONWRITER_TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${USE_JSONWRITER}))

CFLAGS+=${CFLAGS_AUTO}
ifeq ($(NO_LTO),1)
  CFLAGS+= -fno-lto
else
  CFLAGS+=${CFLAGS_LTO}
  LDFLAGS_OPT+= ${LDFLAGS_OPT_LTO}
endif

ifeq ($(VERBOSE),1)
  CFLAGS+= ${CFLAGS_VECTORIZE_OPTIMIZED} ${CFLAGS_VECTORIZE_MISSED} ${CFLAGS_VECTORIZE_ALL}
endif

USE_TERMCAP=pretty
USE_TERMCAP_TARGETS=$(addprefix ${BUILD_DIR}/bin/zsv_,$(addsuffix ${EXE},${USE_TERMCAP}))

#ifeq ($(USE_LIB_TERMCAP),1)
#  TERMCAP_LIB=-ltermcap
#endif

INIH_SRC=external/inih
INIH_INCLUDE=external/inih
INIH_OBJECT=${BUILD_DIR}/external/inih/inih.o
CLI_INCLUDE+= -I${INIH_INCLUDE}

## json writer
JSONWRITER_SRC=external/json_writer-1.0
JSONWRITER_INCLUDE=external/json_writer-1.0
JSONWRITER_OBJECT=${BUILD_DIR}/external/json_writer-1.0/jsonwriter.o

## utf8proc
UTF8PROC_SRC=external/utf8proc-2.6.1
UTF8PROC_INCLUDE=external/utf8proc-2.6.1
UTF8PROC_OBJECT=${BUILD_DIR}/external/utf8proc-2.6.1/utf8proc.o
CFLAGS+= -I${UTF8PROC_INCLUDE} -DUTF8PROC -DUTF8PROC_STATIC

# sqlite3
SQLITE_EXT=${BUILD_DIR}/external/sqlite3/sqlite3_and_csv_vtab.o
SQLITE_SRC=${THIS_MAKEFILE_DIR}/external/sqlite3/sqlite3*.c
SQLITE_EXT_INCLUDE=-Iexternal/sqlite3

help:
	@echo "To build: ${MAKE} [DEBUG=1] [clean] <all|test>"
	@echo
	@echo "To build and test individual apps, run:"
	@echo "  ${MAKE} test"
	@echo "which will build and test all apps, or to build/test a single app:"
	@echo "  ${MAKE} test-xx"
	@echo "where xx is any of:"
	@echo "  echo count select 2json serialize flatten pretty stack desc 2tsv sql"
	@echo ""

install: ${ZSV}

uninstall:
	rm -rf ${ZSV}

build: all

all: ${TARGETS}

${LIBZSV_INSTALL}:
	${MAKE} -C ../src CONFIGFILE=${CONFIGFILEPATH} install

${ZSV}: ${CLI}
	@mkdir -p `dirname "$@"`
ifneq ($(findstring emcc,$(CC)),) # emcc
	cp -p $< `dirname "$@"`/
	cp -p ${CLI_ADDITIONAL} `dirname "$@"`/
else
	cp -p $< $@
endif

cli: build-cli

build-cli: ${CLI}

clean-cli:
	@rm -f  ${CLI_APP_OBJECT} ${CLI_OBJECTS}

${BUILDS}: build-%: ${BUILD_DIR}/bin/zsv_%${EXE}

${CLEANS}: clean-%:
	rm -f ${BUILD_DIR}/bin/zsv_$*${EXE}
	rm -f ${BUILD_DIR}/*/*$*.o

.PHONY: all install cli build build-% clean clean-% test test-%

.SECONDARY: ${OBJECTS}

.POSIX:
.SUFFIXES:
.SUFFIXES: .o .c .a

${BUILD_DIR}/objs/utils/%.o : utils/%.c ${INCLUDE_DIR}/zsv/utils/%.h
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -I${UTF8PROC_INCLUDE} -DINCLUDE_SRC -o $@ -c utils/$*.c

${BUILD_DIR}/objs/zsv_%.o: %.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -I${UTF8PROC_INCLUDE} -c $< -o $@

${UTF8PROC_OBJECT}: ${UTF8PROC_SRC}/utf8proc.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${UTF8PROC_INCLUDE} -c $< -o $@

${INIH_OBJECT}: ${INIH_SRC}/ini.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INIH_INCLUDE} -DINI_HANDLER_LINENO=1 -DINI_CALL_HANDLER_ON_NEW_SECTION=1 -c $< -o $@

${CLI_APP_OBJECT} : cli_ini.c builtin/*.c ${JSONWRITER_OBJECT}
${CLI_APP_OBJECT} ${CLI_OBJECTS}: ${BUILD_DIR}/objs/cli_%.o: %.c ${UTF8PROC_OBJECT}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -DVERSION=\"${VERSION}\" -DZSV_CLI -DMAIN=main_$* -I${JSONWRITER_INCLUDE} ${CLI_INCLUDE} -Iexternal/sglib -I${INCLUDE_DIR} -c $< -o $@ ${SQLITE_EXT_INCLUDE}

${CLI}: cli_internal ${CLI_APP_OBJECT} ${CLI_OBJECTS} ${OBJECTS} ${JSONWRITER_OBJECT} ${UTF8PROC_OBJECT} cli_ini.c ${INIH_OBJECT} ${LIBZSV_INSTALL} ${SQLITE_EXT}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${CFLAGS_EXE} -I${INCLUDE_DIR} ${SQLITE_EXT_INCLUDE} -o $@ ${CLI_APP_OBJECT} ${CLI_OBJECTS} ${OBJECTS} ${JSONWRITER_OBJECT}  ${UTF8PROC_OBJECT} ${TERMCAP_LIB} ${INIH_OBJECT} ${SQLITE_EXT} -L${PREFIX}/lib -lzsv ${LDFLAGS} ${LDFLAGS_OPT}
	@echo Built $@

cli_internal: cli_internal.c.in cli_internal.h cli_internal.h.in

cli_internal.h.in: ${THIS_MAKEFILE_DIR}/../include/zsv/ext/implementation_private.h
	cat $< | perl -ne 'print if /ZSV_EXT_EXPORT/ .. /;/' | sed 's/ZSV_EXT_EXPORT *//g' | sed $$'s/ *;.*/;\\\n/' | sed 's/(/)(/' | sed 's/zsv_ext_\([a-z]*\))/(*\1)/' | grep -v '^$$' > $@

cli_internal.c.in: cli_internal.h.in
	cat cli_internal.h.in | sed 's/\(.*(\*\)\([^)]*\)\(.*\);/zsv_ext_func_assign((\1\3), \2);/' > $@

${USE_SGLIB_TARGETS}: MORE_SOURCE=-Iexternal/sglib

${USE_TERMCAP_TARGETS}: MORE_OBJECTS+=${TERMCAP_LIB}

${SQLITE_EXT}: ${SQLITE_SRC}
${BUILD_DIR}/bin/zsv_sql${EXE}: ${SQLITE_EXT} $@ # circular dependency intentional
${BUILD_DIR}/bin/zsv_sql${EXE}: MORE_OBJECTS+=${BUILD_DIR}/external/sqlite3/sqlite3_and_csv_vtab.o
${BUILD_DIR}/bin/zsv_sql${EXE}: MORE_SOURCE+=-Iexternal/sqlite3

${JSONWRITER_OBJECT}: ${JSONWRITER_SRC}/jsonwriter.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${JSONWRITER_INCLUDE} -DINCLUDE_UTILS $< -c -o $@

${USE_JSONWRITER_TARGETS}: ${JSONWRITER_OBJECT}
${USE_JSONWRITER_TARGETS}: MORE_OBJECTS+= ${JSONWRITER_OBJECT}
${USE_JSONWRITER_TARGETS}: MORE_SOURCE+= -I${JSONWRITER_INCLUDE}

${BUILD_DIR}/bin/zsv_%${EXE}: %.c ${OBJECTS} ${MORE_OBJECTS} ${LIBZSV_INSTALL} ${UTF8PROC_OBJECT}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -o $@ $< ${OBJECTS} ${MORE_OBJECTS} ${MORE_SOURCE} -L${PREFIX}/lib -lzsv ${UTF8PROC_OBJECT} ${LDFLAGS} ${LDFLAGS_OPT}
	@echo Built $@

${BUILD_DIR}/external/sqlite3/sqlite3_and_csv_vtab.o: ${BUILD_DIR}/external/%.o : external/%.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -o $@ -c $<

test: ${TESTS}

test-%: ${BUILD_DIR}/bin/zsv_%${EXE}
	@cd test && ${MAKE} $@ QUIET=1 LEAKS=${LEAKS} CONFIGFILE=${CONFIGFILEPATH}

clean: clean-obj clean-lib
	rm -rf ${BUILD_DIR}

clean-obj:
	rm -rf ${BUILD_DIR}/bin ${INIH_OBJECT} ${JSONWRITER_OBJECT} ${UTF8PROC_OBJECT}

clean-lib:
	${MAKE} -C ../src clean CONFIGFILE=${CONFIGFILEPATH}

external/utf8proc-2.6.1/utf8proc.c: external/utf8proc-2.6.1.tar.gz
	@cd external && tar xf ../$<
	@touch $@
