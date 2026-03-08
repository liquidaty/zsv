# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))

CONFIGFILE ?= ../config.mk
$(info Using config file ${CONFIGFILE})

CONFIGFILEPATH:=$(shell ls ${CONFIGFILE} >/dev/null 2>/dev/null && realpath ${CONFIGFILE})
ifeq ($(CONFIGFILEPATH),)
  ifeq ($(shell dirname ${CONFIGFILE}),.)
    CONFIGFILEPATH:=$(shell ls ../${CONFIGFILE} >/dev/null 2>/dev/null && realpath ../${CONFIGFILE})
    ifneq ($(CONFIGFILEPATH),)
      $(info Config file ${CONFIGFILE} not found; using ../${CONFIGFILE})
    endif
  endif
endif

ifeq ($(CONFIGFILEPATH),)
  $(error Config file ${CONFIGFILE} not found)
else
  include ${CONFIGFILEPATH}
endif

CC ?= cc

CFLAGS_EXE ?=

LIB_SUFFIX=
LIBZSV_L=-lzsv${LIB_SUFFIX}
LIBZSV_A=libzsv${LIB_SUFFIX}.a
LIBZSV=${BUILD_DIR}/lib/${LIBZSV_A}
LIBZSV_INSTALL=${LIBDIR}/${LIBZSV_A}

# if we are incorporating these commands into another project, we might
# want to use different values for ZSV_TEMPFILE_SUFFIX and ZSV_CACHE_PREFIX
# to do: set these in the configure script
ifneq ($(ZSV_TEMPFILE_SUFFIX),)
  CFLAGS+= -DZSV_TEMPFILE_SUFFIX='${ZSV_TEMPFILE_SUFFIX}'
endif
ifneq ($(ZSV_CACHE_PREFIX),)
  CFLAGS+= -DZSV_CACHE_PREFIX='${ZSV_CACHE_PREFIX}'
endif

CFLAGS+=-fsigned-char
ifneq ($(WIN),)
  CFLAGS+= -Wno-incompatible-pointer-types
  LIBS+=-lShlwapi
endif

ifneq ($(ZSV_IS_PROP_FILE_HANDLER),)
	CFLAGS+=-DZSV_IS_PROP_FILE_HANDLER=${ZSV_IS_PROP_FILE_HANDLER}
endif
ifneq ($(ZSV_IS_PROP_FILE_DEPTH),)
	CFLAGS+=-DZSV_IS_PROP_FILE_DEPTH=${ZSV_IS_PROP_FILE_DEPTH}
endif

ifeq ($(ZSV_NO_ONLY_CRLF),1)
  CFLAGS+= -DZSV_NO_ONLY_CRLF
endif

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
  NO_LTO=1
  DBG_SUBDIR+=dbg
else
  DBG_SUBDIR+=rel
endif

ifeq ($(NO_LTO),1)
  CFLAGS+= -fno-lto
else
  CFLAGS+=${CFLAGS_LTO}
  LDFLAGS_OPT+= ${LDFLAGS_OPT_LTO}
endif

NO_STDIN=
ifeq ($(NO_STDIN),1)
  CFLAGS+= -DNO_STDIN
endif

CFLAGS+= ${CFLAGS_PIC}

CONFIGURE_HOST?=
ifeq ($(CONFIGURE_HOST),)
ifneq ($(HOST),)
  CONFIGURE_HOST=$(HOST)
endif
endif

ifeq ($(WIN),0)
  BUILD_SUBDIR=$(shell uname)/${DBG_SUBDIR}
  EXE=
  ifneq ($(findstring emcc,$(CC)),) # emcc
    NO_THREADING ?= 1
    NO_PLAYGROUND ?= 1
    EXE=.em.js
    ifeq ($(NO_PLAYGROUND),0) # wasm playground
      LDFLAGS += -sEXPORTED_RUNTIME_METHODS="['callMain']" -sINVOKE_RUN=0 -sALLOW_MEMORY_GROWTH=1
    else
      CFLAGS += -DNO_PLAYGROUND
      LDFLAGS += -sEXPORTED_FUNCTIONS="['_free','_malloc']"
      LDFLAGS += -sEXPORTED_RUNTIME_METHODS="['setValue','allocateUTF8','stringToUTF8Array','lengthBytesUTF8','writeArrayToMemory']"
      LDFLAGS += -sRESERVED_FUNCTION_POINTERS=4 -sALLOW_MEMORY_GROWTH=1
    endif
    LDFLAGS += -sFORCE_FILESYSTEM=1 -lidbfs.js -sMAIN_MODULE
    ifneq ($(NO_THREADING),1)
      CFLAGS+=-pthread
      LDFLAGS += -sUSE_PTHREADS=1 -sPTHREAD_POOL_SIZE=2
    endif
  endif
else
  BUILD_SUBDIR=win/${DBG_SUBDIR}
  EXE=.exe
  CFLAGS+= -D__USE_MINGW_ANSI_STDIO -D_ISOC99_SOURCE -D_GNU_SOURCE
  CFLAGS+= -Wl,--strip-all
  CONFIGURE_HOST=x86_64-w64-mingw32
  CFLAGS+=-Dsetenv\(a,b,c\)=_putenv_s\(a,b\)
endif

JQ_CONFIGURE_HOST=
ifneq ($(CONFIGURE_HOST),)
  JQ_CONFIGURE_HOST=--host=$(CONFIGURE_HOST)
endif

CFLAGS+=-std=gnu11
CFLAGS+=-Wunused
CFLAGS_O ?=
ifeq ($(CFLAGS_O),)
  ifeq ($(DEBUG),0)
    CFLAGS_O=-O3
  else
    CFLAGS_O=-O0
  endif
endif

CFLAGS+=${CFLAGS_O}

CFLAGS+=-Wshadow -Wall -Wextra
ifeq ($(DEBUG),0)
  CFLAGS+=-DNDEBUG -Wno-gnu-statement-expression -Wno-missing-braces -pedantic -DSTDC_HEADERS -D_GNU_SOURCE ${CFLAGS_OPT} #-ftree-vectorize

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
  CFLAGS += ${CFLAGS_DEBUG}
endif

CCBN=$(shell basename ${CC})
CFLAGS+= -I${PREFIX}/include
THIS_LIB_BASE:=$(shell cd .. && pwd)
INCLUDE_DIR:=${THIS_LIB_BASE}/include
BUILD_DIR:=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}
UTILS1=writer file err signal mem clock arg dl string dirs prop cache jq os index chunk

ZSV_EXTRAS ?=

ifneq ($(findstring emcc,$(CC)),) # emcc
  ZSV_EXTRAS=1
  UTILS1+=emcc/fs_api
  ifneq ($(NO_THREADING),1)
    LDFLAGS+=-pthread
  endif
else # not emcc
  CFLAGS+= ${CFLAGS_AVX} ${CFLAGS_SSE}
  LDFLAGS+=-lpthread # Linux explicitly requires
endif

ifeq ($(ZSV_EXTRAS),1)
  UTILS1+= overwrite overwrite_writer
endif

UTILS=$(addprefix ${BUILD_DIR}/objs/utils/,$(addsuffix .o,${UTILS1}))

ifeq ($(NO_THREADING),1)
  CFLAGS+= -DNO_THREADING
  ZSV_NO_PARALLEL=1
endif

ifeq ($(ZSV_NO_PARALLEL),1)
  CFLAGS+=-DZSV_NO_PARALLEL
endif


ifeq ($(ZSV_EXTRAS),1)
  CFLAGS+= -DZSV_EXTRAS
endif

OBJECTS=${UTILS}

ifeq ($(NO_MEMMEM),1)
  OBJECTS+= ${BUILD_DIR}/objs/utils/memmem.o
  CFLAGS+=-DNO_MEMMEM
  UTIL_A_OBJ_WIN=memmem
endif

ZSV=$(BINDIR)/zsv${EXE}

SOURCES=echo paste check count count-pull select select-pull 2tsv 2json serialize flatten pretty stack desc sql 2db compare prop rm mv jq
ifeq ($(ZSV_EXTRAS),1)
  SOURCES+=overwrite
endif
CLI_SOURCES=echo select desc count paste check 2tsv pretty sql flatten 2json serialize stack 2db compare prop rm mv jq
ifeq ($(ZSV_EXTRAS),1)
  CLI_SOURCES+=overwrite
endif

ifeq ($(ZSVSHEET_BUILD),1)
  SOURCES+=sheet
  CLI_SOURCES+=sheet
  CFLAGS+=-DZSVSHEET_BUILD
  CFLAGS+=${CFLAGS_NCURSES}
  LDFLAGS+=${LDFLAGS_NCURSES}
endif

CFLAGS+= -DUSE_JQ

STATIC_LIB_FLAGS=
ifeq ($(STATIC_BUILD),1)
  STATIC_LIB_FLAGS=${LDFLAGS_STATIC}
else
  ifneq ($(STATIC_LIBS),)
    STATIC_LIB_FLAGS=${LDFLAGS_STATIC}
  endif
endif

STANDALONE_PFX=${BUILD_DIR}/bin/zsv_

TARGETS=$(addprefix ${STANDALONE_PFX},$(addsuffix ${EXE},${SOURCES})) ${CLI}

BUILDS=$(addprefix build-,${SOURCES})
CLEANS=$(addprefix clean-,${SOURCES})

## cli
VERSION= $(shell (git describe --always --dirty --tags 2>/dev/null || echo "v0.0.0-zsv") | sed 's/^v//')
CLI_OBJ_PFX=${BUILD_DIR}/objs/cli_
CLI_APP_OBJECT=${CLI_OBJ_PFX}cli.o
CLI=${BUILD_DIR}/bin/cli${EXE}
ifneq ($(findstring emcc,$(CC)),) # emcc
  CLI_ADDITIONAL=${BUILD_DIR}/bin/cli.em.wasm
endif
CLI_OBJECTS=$(addprefix ${CLI_OBJ_PFX},$(addsuffix .o,${CLI_SOURCES}))

CFLAGS+=${CFLAGS_AUTO}

ifeq ($(VERBOSE),1)
  CFLAGS+= ${CFLAGS_VECTORIZE_OPTIMIZED} ${CFLAGS_VECTORIZE_MISSED} ${CFLAGS_VECTORIZE_ALL}
endif

INIH_SRC=${THIS_MAKEFILE_DIR}/external/inih
INIH_INCLUDE=${THIS_MAKEFILE_DIR}/external/inih
INIH_OBJECT=${BUILD_DIR}-external/inih/inih.o
CLI_INCLUDE+= -I${INIH_INCLUDE}

YAJL_SRC_DIR=${THIS_MAKEFILE_DIR}/external/yajl
YAJL_OBJ1=yajl yajl_alloc yajl_buf yajl_encode yajl_gen yajl_lex yajl_parser yajl_tree yajl_version
YAJL_OBJ=$(addprefix ${BUILD_DIR}-external/yajl/,$(addsuffix .o,${YAJL_OBJ1}))
YAJL_INCLUDE=-I${YAJL_SRC_DIR}/build/yajl-2.1.1/include

YAJL_HELPER_OBJ=${BUILD_DIR}-external/yajl_helper/yajl_helper.o
YAJL_HELPER_INCLUDE=-I${THIS_MAKEFILE_DIR}/external/yajl_helper
CFLAGS+=${YAJL_HELPER_INCLUDE} ${YAJL_INCLUDE}

# jq
JQ_TARBALL=${THIS_MAKEFILE_DIR}/external/jq-1.6.tar.gz
JQ_SRC=${BUILD_DIR}-external/jq-src

JQ_PREFIX ?=
ifeq ($(JQ_PREFIX),)
  JQ_PREFIX=${BUILD_DIR}-external/jq-build
  JQ_INCLUDE_DIR=${JQ_PREFIX}/include
  JQ_BUNDLE_LIB=${JQ_PREFIX}/lib/libjq.a
  JQ_LIB=${JQ_BUNDLE_LIB}
else
  JQ_INCLUDE_DIR=${JQ_PREFIX}/include
  JQ_LIB=
endif

## json writer
JSONWRITER_SRC=${THIS_MAKEFILE_DIR}/external/json_writer-1.01
JSONWRITER_INCLUDE=-I${THIS_MAKEFILE_DIR}/external/json_writer-1.01
JSONWRITER_OBJECT=${BUILD_DIR}-external/json_writer-1.01/jsonwriter.o

## memfile
MEMFILE_SRC=${THIS_MAKEFILE_DIR}/external/memfile-1.0/src
MEMFILE_INCLUDE=-I${THIS_MAKEFILE_DIR}/external/memfile-1.0/include
MEMFILE_OBJECT=${BUILD_DIR}-external/memfile-1.0/memfile.o

## utf8proc
UTF8PROC_SRC=${THIS_MAKEFILE_DIR}/external/utf8proc-2.6.1
UTF8PROC_INCLUDE=${THIS_MAKEFILE_DIR}/external/utf8proc-2.6.1
UTF8PROC_OBJECT=${BUILD_DIR}-external/utf8proc-2.6.1/utf8proc.o
CFLAGS+= -I${UTF8PROC_INCLUDE} -DUTF8PROC -DUTF8PROC_STATIC

# sqlite3
SQLITE_SRC=${THIS_MAKEFILE_DIR}/external/sqlite3/sqlite3*.c
SQLITE_EXT=${BUILD_DIR}-external/sqlite3/sqlite3_and_csv_vtab.o
SQLITE_EXT_INCLUDE=-I${THIS_MAKEFILE_DIR}/external/sqlite3

SQL_INTERNAL_OBJECT=${BUILD_DIR}/objs/sql_internal.o

# everything uses prop, which in turn uses yajl and jq and json and sqlite3
OBJECTS+= ${YAJL_OBJ} ${YAJL_HELPER_OBJ} ${BUILD_DIR}/objs/utils/json.o ${SQLITE_EXT}
MORE_SOURCE+= ${YAJL_INCLUDE} ${YAJL_HELPER_INCLUDE} -I${JQ_INCLUDE_DIR} ${SQLITE_EXT_INCLUDE}
MORE_LIBS+=${JQ_LIB} ${LDFLAGS_JQ}

help:
	@echo "To build: ${MAKE} [DEBUG=1] [STATIC_BUILD=1] [clean] [clean-all] [BINDIR=${BINDIR}] [JQ_PREFIX=/usr/local] <install|all|install-util-lib|test>"
	@echo
	@echo "If JQ_PREFIX is not defined, libjq will be built in the build dir"
	@echo "If STATIC_BUILD is set to 1, will build with -static flag, if the compiler supports it"
	@echo
	@echo "To build and test individual apps, run:"
	@echo "  ${MAKE} test"
	@echo "which will build and test all apps, or to build/test a single app:"
	@echo "  ${MAKE} test-xx"
	@echo "where xx is any of:"
	@echo "  echo count count-pull paste check select select-pull 2tsv 2json serialize flatten pretty stack desc sql 2db prop rm mv overwrite"
	@echo ""
	@echo "To run benchmarks:"
	@echo "  ${MAKE} benchmark"
	@echo ""

install: ${ZSV}

benchmark: ${STANDALONE_PFX}select${EXE} ${STANDALONE_PFX}count${EXE} ${STANDALONE_PFX}2tsv${EXE}
	make -C benchmark all

uninstall-all: uninstall uninstall-util-lib

uninstall:
	rm -rf ${ZSV}

ZSV_UTIL_A=${LIBDIR}/libzsvutil.a
${ZSV_UTIL_A}:SQLITE_EXT=
${ZSV_UTIL_A}:SQLITE_EXT_INCLUDE=
${ZSV_UTIL_A}: ${BUILD_DIR}/objs/utils/util.a
	@mkdir -p `dirname $@`
	cp -p $< $@

UTIL_A_OBJ:=writer file dirs-no-jq os ${UTIL_A_OBJ_WIN}
UTIL_A_OBJ:=$(addprefix ${BUILD_DIR}/objs/utils/,$(addsuffix .o,${UTIL_A_OBJ}))

${BUILD_DIR}/objs/utils/util.a: ${UTIL_A_OBJ}
	$(AR) rcv $@ $^ # $?
	$(RANLIB) $@
	$(AR) -t $@ # check it is there
	@echo Built $@

uninstall-util-lib:
	@rm -f ${ZSV_UTIL_A}

install-util-lib: ${ZSV_UTIL_A}

build: ${CLI}

all: ${TARGETS}

${LIBZSV_INSTALL}:
	${MAKE} -C ../src CONFIGFILE=${CONFIGFILEPATH} install DEBUG=${DEBUG}

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

${BUILDS}: build-%: ${STANDALONE_PFX}%${EXE}
	@echo Built ${STANDALONE_PFX}$*${EXE}

${CLEANS}: clean-%:
	rm -f ${STANDALONE_PFX}$*${EXE}
	rm -f ${BUILD_DIR}/*/*$*.o

.PHONY: all install cli build build-% clean clean-% test test-% lib-% check install-util-lib uninstall-util-lib clean-util-lib

.SECONDARY: ${OBJECTS}

.POSIX:
.SUFFIXES:
.SUFFIXES: .o .c .a

${BUILD_DIR}/objs/sql_internal.o ${BUILD_DIR}/objs/utils/index.o ${BUILD_DIR}/objs/utils/chunk.o: ${BUILD_DIR}/objs/%.o: %.c %.h # ${BUILD_DIR}/objs/%.o: %.c %.h
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -o $@ -c $<

${BUILD_DIR}/objs/utils/%.o : utils/%.c ${INCLUDE_DIR}/zsv/utils/%.h ${JQ_LIB}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -I${UTF8PROC_INCLUDE} -DINCLUDE_SRC -o $@ -c utils/$*.c ${MORE_SOURCE}

${BUILD_DIR}/objs/utils/dirs-no-jq.o : utils/dirs.c ${INCLUDE_DIR}/zsv/utils/dirs.h
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -I${UTF8PROC_INCLUDE} -DINCLUDE_SRC -o $@ -c utils/dirs-no-jq.c ${MORE_SOURCE}

${BUILD_DIR}/objs/zsv_%.o: %.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -I${UTF8PROC_INCLUDE} -c $< -o $@

${UTF8PROC_OBJECT}: ${UTF8PROC_SRC}/utf8proc.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${UTF8PROC_INCLUDE} -c ${UTF8PROC_SRC}/utf8proc.c -o $@

${INIH_OBJECT}: ${INIH_SRC}/ini.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INIH_INCLUDE} -DINI_HANDLER_LINENO=1 -DINI_CALL_HANDLER_ON_NEW_SECTION=1 -c $< -o $@

${CLI_APP_OBJECT} : cli_ini.c builtin/*.c ${JQ_LIB}
${CLI_APP_OBJECT} ${CLI_OBJECTS}: ${CLI_OBJ_PFX}%.o: %.c ${UTF8PROC_SRC}/utf8proc.c # ${MORE_OBJECTS}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -DVERSION=\"${VERSION}\" -DZSV_CLI ${CLI_INCLUDE} ${MEMFILE_INCLUDE} -I${THIS_MAKEFILE_DIR}/external/sglib -I${INCLUDE_DIR} -I${UTF8PROC_INCLUDE} -c $< -o $@ ${MORE_SOURCE}

${CLI}: cli_internal.c.in cli_internal.h cli_internal.h.in ${CLI_APP_OBJECT} ${CLI_OBJECTS} ${OBJECTS} ${UTF8PROC_OBJECT} cli_ini.c ${INIH_OBJECT} ${LIBZSV_INSTALL} ${MORE_OBJECTS} ${SQL_INTERNAL_OBJECT}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${CFLAGS_EXE} -I${INCLUDE_DIR} -o $@ ${CLI_APP_OBJECT} ${CLI_OBJECTS} ${OBJECTS} ${UTF8PROC_OBJECT} ${INIH_OBJECT} -L${LIBDIR} ${LIBZSV_L} ${LDFLAGS} ${LDFLAGS_OPT} ${MORE_OBJECTS} ${MORE_SOURCE} ${MORE_LIBS} ${STATIC_LIB_FLAGS}
	@echo Built $@

cli_internal.h.in: ${THIS_MAKEFILE_DIR}/../include/zsv/ext/implementation_private.h
	cat $< | awk '/ZSV_EXT_EXPORT/,/;/' | sed 's/ZSV_EXT_EXPORT *//g' | sed $$'s/ *;.*/;\\\n/' | sed 's/(/)(/' | sed 's/zsv_ext_\([a-z]*\))/(*\1)/' | grep -v '^$$' > $@

cli_internal.c.in: cli_internal.h.in
	cat cli_internal.h.in | sed 's/\(.*(\*\)\([^)]*\)\(.*\);/zsv_ext_func_assign((\1\3), \2);/' > $@

${SQLITE_EXT}: ${SQLITE_SRC}

# ${JQ_INCLUDE_DIR}: ${JQ_LIB}

${JQ_SRC}: ${JQ_TARBALL}
	@rm -rf $@-tmp
	@mkdir -p $@-tmp
	cd $@-tmp && tar xf ${JQ_TARBALL}
	mv $@-tmp/jq-1.6 $@
	rm -rf $@-tmp

lib-jq: ${JQ_LIB}
	@echo "Using jq library ${JQ_LIB}"

${JQ_BUNDLE_LIB}: ${JQ_SRC} # -D_REENTRANT needed for clang to not break
	@if [ -e ${JQ_SRC}-conf ]; then \
		rm -rf ${JQ_SRC}-conf; \
	fi
	# copy the directory because its timestamp gets updated after libjq is built
	@cp -a ${JQ_SRC} ${JQ_SRC}-conf
	cd ${JQ_SRC}-conf \
	&& CC="${CC}" CFLAGS="${CFLAGS} -w -D_REENTRANT" ./configure \
	    --prefix="${JQ_PREFIX}" \
	    --disable-maintainer-mode \
	    --without-oniguruma \
	    --no-recursion \
	    --disable-docs \
	    --disable-shared \
	    --enable-static \
	    ${JQ_CONFIGURE_HOST} \
	&& (${MAKE} install -k)

${JSONWRITER_OBJECT}: ${JSONWRITER_SRC}/jsonwriter.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${JSONWRITER_INCLUDE} -DINCLUDE_UTILS $< -c -o $@

${MEMFILE_OBJECT}: ${MEMFILE_SRC}/memfile.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${MEMFILE_INCLUDE} -DINCLUDE_UTILS $< -c -o $@

# sglib
MORE_SOURCE+=-I${THIS_MAKEFILE_DIR}/external/sglib

# 2json, desc, compare, flatten use jsonwriter
${CLI} ${STANDALONE_PFX}2json${EXE} ${STANDALONE_PFX}desc${EXE} ${STANDALONE_PFX}compare${EXE} ${STANDALONE_PFX}flatten${EXE}: ${JSONWRITER_OBJECT}
${CLI} ${STANDALONE_PFX}2json${EXE} ${STANDALONE_PFX}desc${EXE} ${STANDALONE_PFX}compare${EXE} ${STANDALONE_PFX}flatten${EXE}: MORE_OBJECTS+= ${JSONWRITER_OBJECT}
${STANDALONE_PFX}2json${EXE} ${CLI_OBJ_PFX}2json.o ${STANDALONE_PFX}desc${EXE} ${CLI_OBJ_PFX}desc.o ${STANDALONE_PFX}compare${EXE} ${CLI_OBJ_PFX}compare.o ${STANDALONE_PFX}flatten${EXE} ${CLI_OBJ_PFX}flatten.o: MORE_SOURCE+=${JSONWRITER_INCLUDE} ${SQLITE_EXT_INCLUDE}

ifeq ($(USE_SIMDUTF),1)
CFLAGS+=-DUSE_SIMDUTF
SIMDUTF_OBJECT=${BUILD_DIR}/check/simdutf_wrapper.o
${SIMDUTF_OBJECT}: check/simdutf_wrapper.cpp
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -std=c++17 -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -c check/simdutf_wrapper.cpp -o $@

${CLI} ${STANDALONE_PFX}check${EXE}: ${SIMDUTF_OBJECT}
${CLI} ${STANDALONE_PFX}check${EXE}: MORE_OBJECTS+=${SIMDUTF_OBJECT} -lstdc++
endif

# check if we are on arm64
ifeq ($(ARM64),)
  ifneq ($(shell uname -a | grep Darwin | grep arm64),)
    ARM64=1
  endif
endif

# CLI, select and sheet use PCRE2-8
BULITIN_PCRE2_PREFIX=${BUILD_DIR}-external/pcre2
ifeq ($(HAVE_PCRE2_8),1)
  NO_PCRE2_JIT=
  # TO DO: make PCRE2_STATIC a configure option / detection
  PCRE2_STATIC=1
  CFLAGS+=-DHAVE_PCRE2_8
  # add dependency
  ${CLI} ${STANDALONE_PFX}select${EXE} ${STANDALONE_PFX}sheet${EXE}: ${BUILD_DIR}/objs/utils/pcre2-8/pcre2-8.o

  PCRE2_LIB=-lpcre2-8
  ifeq ($(PCRE2_STATIC),1)
    CFLAGS+= -DPCRE2_STATIC
    ifeq ($(PCRE2_8_PREFIX),builtin)
      PCRE2_8_PREFIX=${BULITIN_PCRE2_PREFIX}
      # add dependency
      ${CLI} ${STANDALONE_PFX}select${EXE} ${STANDALONE_PFX}sheet${EXE}: ${PCRE2_8_PREFIX}/lib/libpcre2-8.a

    endif
    PCRE2_LIB=${PCRE2_8_PREFIX}/lib/libpcre2-8.a
  endif

  ifeq ($(NO_PCRE2_JIT),)
    ifeq ($(ARM64),1)
      # see https://github.com/zherczeg/sljit/issues/99
      NO_PCRE2_JIT=1
    endif
  endif

  ifeq ($(NO_PCRE2_JIT),1)
    CFLAGS+=-DNO_PCRE2_JIT
  endif

  LDFLAGS_PCRE2_8=
  INCLUDE_PCRE2_8=${PREFIX}/include
  ifneq ($(PCRE2_8_PREFIX),$(PREFIX))
    ifneq ($(PCRE2_STATIC),1)
      LDFLAGS_PCRE2_8=-L${PCRE2_8_PREFIX}/lib
    endif
    INCLUDE_PCRE2_8=${PCRE2_8_PREFIX}/include
  endif

  ${CLI} ${STANDALONE_PFX}select${EXE} ${STANDALONE_PFX}sheet${EXE}: MORE_LIBS+=${BUILD_DIR}/objs/utils/pcre2-8/pcre2-8.o ${LDFLAGS_PCRE2_8} ${PCRE2_LIB}
endif

# pcre2-8.o actually only needs ${PCRE2_8_PREFIX}/include/pcre2.h, but if we are using builtin pcre2, then
# that will only exist after the builtin library has been built
ifeq ($(PCRE2_8_PREFIX),$(BULITIN_PCRE2_PREFIX))
  PCRE2_8_O_DEP=${PCRE2_8_PREFIX}/lib/libpcre2-8.a
else
  PCRE2_8_O_DEP=
endif

${BUILD_DIR}/objs/utils/pcre2-8/pcre2-8.o: ${BUILD_DIR}/objs/utils/%.o : utils/%.c utils/%.h ${PCRE2_8_O_DEP}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_PCRE2_8} -o $@ -c utils/$*.c

${BULITIN_PCRE2_PREFIX}/lib/libpcre2-8.a:
	${MAKE} -C external -f ${CONFIGFILEPATH} -f build.mk $@ BUILD_DIR=${BUILD_DIR} CONFIGURE_HOST=${CONFIGURE_HOST}

# utils/db uses jsonwriter
${BUILD_DIR}/objs/utils/db.o : MORE_SOURCE+= ${JSONWRITER_INCLUDE} ${SQLITE_EXT_INCLUDE}

# flatten uses memfile
${CLI} ${STANDALONE_PFX}flatten${EXE}: ${MEMFILE_OBJECT}
${CLI} ${STANDALONE_PFX}flatten${EXE}: MORE_OBJECTS+= ${MEMFILE_OBJECT} ${JSONWRITER_INCLUDE}
${STANDALONE_PFX}flatten${EXE}: MORE_SOURCE+=${MEMFILE_INCLUDE} ${SQLITE_EXT_INCLUDE}

# 2json uses utils/db
${CLI} ${STANDALONE_PFX}2json${EXE}: ${BUILD_DIR}/objs/utils/db.o
${CLI} ${STANDALONE_PFX}2json${EXE}: MORE_OBJECTS+= ${BUILD_DIR}/objs/utils/db.o

# pretty uses termcap
${CLI} ${STANDALONE_PFX}pretty${EXE}: MORE_LIBS+=${LDFLAGS_TERMCAP}

${CLI} ${STANDALONE_PFX}sheet${EXE} ${STANDALONE_PFX}sql${EXE}: ${SQL_INTERNAL_OBJECT}
${CLI} ${STANDALONE_PFX}sheet${EXE} ${STANDALONE_PFX}sql${EXE}: MORE_OBJECTS+=${SQL_INTERNAL_OBJECT}

${STANDALONE_PFX}%${EXE}: %.c ${OBJECTS} ${MORE_OBJECTS} ${LIBZSV_INSTALL} ${UTF8PROC_OBJECT}
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -I${INCLUDE_DIR} -o $@ $< ${OBJECTS} ${MORE_OBJECTS} ${MORE_SOURCE} -L${LIBDIR} ${LIBZSV_L} ${UTF8PROC_OBJECT} ${LDFLAGS} ${LDFLAGS_OPT} ${MORE_LIBS} ${STATIC_LIB_FLAGS}

${BUILD_DIR}-external/sqlite3/sqlite3_and_csv_vtab.o: ${BUILD_DIR}-external/%.o : external/%.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -w -I${INCLUDE_DIR} ${YAJL_INCLUDE} ${YAJL_HELPER_INCLUDE} -o $@ -c $<

${YAJL_OBJ}: ${BUILD_DIR}-external/yajl/%.o : external/yajl/src/%.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -w ${YAJL_INCLUDE} -c external/yajl/src/$*.c -o $@

${YAJL_HELPER_OBJ}: external/yajl_helper/yajl_helper.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${YAJL_INCLUDE} ${YAJL_HELPER_INCLUDE} -c $< -o $@

check test:	test-standalone test-cli test-shared-lib

test-shared-lib:
	@echo "TO DO: create tests for using shared library"

test-standalone:
	@${MAKE} -C test test QUIET=1 LEAKS=${LEAKS} CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG}

test-cli: ${CLI}
	@${MAKE} -C test $@ QUIET=1 LEAKS=${LEAKS} CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG} CLI=${CLI}

clean-all: clean clean-external clean-obj clean-lib clean-util-lib

clean-external:
	@rm -rf ${JQ_SRC}

clean-util-lib:
	@rm -f ${BUILD_DIR}/objs/utils/util.a

clean-lib:
	@rm -rf ${BUILD_DIR}-external

clean: clean-obj clean-internal
	rm -rf ${BUILD_DIR}
	${MAKE} -C test clean CONFIGFILE=${CONFIGFILEPATH} DEBUG=${DEBUG}

clean-obj:
	rm -rf ${BUILD_DIR}/bin ${INIH_OBJECT} ${JSONWRITER_OBJECT} ${UTF8PROC_OBJECT} ${MEMFILE_OBJECT}
	rm -rf external/utf8proc-2.6.1

clean-internal:
	$(RM) cli_internal.c.in cli_internal.h.in

${UTF8PROC_SRC}/utf8proc.c ${UTF8PROC_SRC}/utf8proc.h: ${THIS_MAKEFILE_DIR}/external/utf8proc-2.6.1.tar.gz
	@cd external && tar xf utf8proc-2.6.1.tar.gz && chown -R `whoami` utf8proc-2.6.1
	@touch $@
