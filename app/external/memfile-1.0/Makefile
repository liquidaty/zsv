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

CC ?= cc
AWK ?= awk
AR ?= ar
RANLIB ?= ranlib
SED ?= sed

WIN=
DEBUG=0
ifeq ($(WIN),)
  WIN=0
  ifneq ($(findstring w64,$(CC)),) # e.g. mingw64
    WIN=1
  endif
endif

CFLAGS+=${CFLAG_O} ${CFLAGS_OPT}
CFLAGS+=${CFLAGS_AUTO}
CFLAGS+=-I.

ifeq ($(VERBOSE),1)
  CFLAGS+= ${CFLAGS_VECTORIZE_OPTIMIZED} ${CFLAGS_VECTORIZE_MISSED} ${CFLAGS_VECTORIZE_ALL}
endif

VERSION= $(shell (git describe --always --dirty --tags 2>/dev/null || echo "v0.0.0-memfile") | sed 's/^v//')

ifneq ($(findstring emcc,$(CC)),) # emcc
  NO_THREADING=1
endif

ifeq ($(NO_THREADING),1)
  CFLAGS+= -DNO_THREADING
endif

ifeq ($(DEBUG),0)
  CFLAGS+= -DNDEBUG -O3  ${CFLAGS_LTO}
else
  CFLAGS += ${CFLAGS_DEBUG}
endif

ifeq ($(DEBUG),1)
  DBG_SUBDIR+=dbg
else
  DBG_SUBDIR+=rel
endif

ifeq ($(WIN),0)
  BUILD_SUBDIR=$(shell uname)/${DBG_SUBDIR}
  WHICH=which
  EXE=
  CFLAGS+= -fPIC
else
  BUILD_SUBDIR=win/${DBG_SUBDIR}
  WHICH=where
  EXE=.exe
  CFLAGS+= -fpie
  CFLAGS+= -D__USE_MINGW_ANSI_STDIO -D_ISOC99_SOURCE -Wl,--strip-all
endif

CFLAGS+= -std=gnu11 -Wno-gnu-statement-expression -Wshadow -Wall -Wextra -Wno-missing-braces -pedantic -D_GNU_SOURCE

CFLAGS+= ${MEMFILE_OPTIONAL_CFLAGS}

CCBN=$(shell basename ${CC})
THIS_LIB_BASE=$(shell cd .. && pwd)
INCLUDE_DIR=${THIS_LIB_BASE}/include
BUILD_DIR=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}

LIB_SUFFIX?=
MEMFILE_OBJ=${BUILD_DIR}/objs/memfile.o
LIBMEMFILE_A=libmemfile${LIB_SUFFIX}.a
LIBMEMFILE=${BUILD_DIR}/lib/${LIBMEMFILE_A}
LIBMEMFILE_INSTALL=${LIBDIR}/${LIBMEMFILE_A}

MEMFILE_OBJ_OPTS=

help:
	@echo "Make options:"
	@echo "  `basename ${MAKE}` build|install|uninstall|clean"
	@echo
	@echo "Optional make variables:"
	@echo "  [CONFIGFILE=config.mk] [VERBOSE=1] [LIBDIR=${LIBDIR}] [INCLUDEDIR=${INCLUDEDIR}] [LIB_SUFFIX=]"
	@echo

build: ${LIBMEMFILE}

${LIBMEMFILE}: ${MEMFILE_OBJ}
	@mkdir -p `dirname "$@"`
	@rm -f $@
	$(AR) rcv $@ $?
	$(RANLIB) $@
	$(AR) -t $@ # check it is there
	@echo Built $@

install: ${LIBMEMFILE_INSTALL}
	@mkdir -p  $(INCLUDEDIR)
	@cp -pR include/*.h $(INCLUDEDIR)
	@echo "include files copied to $(INCLUDEDIR)"

${LIBMEMFILE_INSTALL}: ${LIBMEMFILE}
	@mkdir -p `dirname "$@"`
	@cp -p ${LIBMEMFILE} "$@"
	@echo "libmemfile installed to $@"

uninstall:
	@rm -rf ${INCLUDEDIR}/memfile*
	 rm  -f ${LIBDIR}/libmemfile*

clean:
	rm -rf ${BUILD_DIR}/objs ${LIBMEMFILE}

.PHONY: build install uninstall clean  ${LIBMEMFILE_INSTALL}

${BUILD_DIR}/objs/memfile.o: src/memfile.c include/memfile.h
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -DMEMFILE_VERSION=\"${VERSION}\" -I${INCLUDE_DIR} ${MEMFILE_OBJ_OPTS} -o $@ -c $<
