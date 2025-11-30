# Makefile for use with GNU make

THIS_MAKEFILE_DIR:=$(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
THIS_DIR:=$(shell basename "${THIS_MAKEFILE_DIR}")
THIS_MAKEFILE:=$(lastword $(MAKEFILE_LIST))

.POSIX:
.SUFFIXES:
.SUFFIXES: .o .c .a

CONFIGFILE ?=../config.mk
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

ifeq ($(BUILD_SHAREDLIB),1)
  include ${THIS_MAKEFILE_DIR}/../make/sharedlib.mk # sets SHAREDLIB_EXT
endif

CFLAGS+= ${CFLAG_O} ${CFLAGS_OPT}
CFLAGS+= ${CFLAGS_AUTO}

CFLAGS+=-fsigned-char

ifeq ($(VERBOSE),1)
  CFLAGS+= ${CFLAGS_VECTORIZE_OPTIMIZED} ${CFLAGS_VECTORIZE_MISSED} ${CFLAGS_VECTORIZE_ALL}
endif

VERSION= $(shell (git describe --always --dirty --tags 2>/dev/null || echo "v0.0.0-zsv") | sed 's/^v//')

ZSV_EXTRAS ?=
ifneq ($(findstring emcc,$(CC)),) # emcc
  ZSV_EXTRAS=1
  NO_THREADING=1
else # not emcc
  CFLAGS+= ${CFLAGS_AVX} ${CFLAGS_SSE}
endif

ifeq ($(NO_THREADING),1)
  CFLAGS+= -DNO_THREADING
endif

ifeq ($(ZSV_NO_ONLY_CRLF),1)
  CFLAGS+= -DZSV_NO_ONLY_CRLF
endif

ifeq ($(ZSV_EXTRAS),1)
  CFLAGS+= -DZSV_EXTRAS
endif

ifeq ($(DEBUG),0)
  CFLAGS+= -DNDEBUG -O3  ${CFLAGS_LTO}

  ifeq ($(PGO),1)
    CFLAGS+= -fprofile-generate -fprofile-dir=/tmp/p4
  else
    ifeq ($(PGO),2)
      CFLAGS+= -fprofile-use=/tmp/p4
    else
      $(info No profiling set. To use PGO, compile with PGO=1, then run with data, then compile again with PGO=2)
    endif
  endif
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

CFLAGS+= ${ZSV_OPTIONAL_CFLAGS}

CCBN=$(shell basename ${CC})
THIS_LIB_BASE=$(shell cd .. && pwd)
INCLUDE_DIR=${THIS_LIB_BASE}/include
BUILD_DIR=${THIS_LIB_BASE}/build/${BUILD_SUBDIR}/${CCBN}

NO_UTF8_CHECK=1

LIB_SUFFIX?=
ZSV_OBJ=${BUILD_DIR}/objs/zsv.o
LIBZSV_STATIC=libzsv${LIB_SUFFIX}.a
LIBZSV_STATIC_BUILD=${BUILD_DIR}/lib/${LIBZSV_STATIC}
LIBZSV_BUILD=${LIBZSV_STATIC_BUILD}
LIBZSV_INSTALL=${LIBDIR}/${LIBZSV_STATIC}

ifeq ($(BUILD_SHAREDLIB),1)
  LIBZSV_SHARED=libzsv.${SHAREDLIB_EXT}
  LIBZSV_SHARED_BUILD+=${BUILD_DIR}/lib/${LIBZSV_SHARED}
  LIBZSV_BUILD+=${LIBZSV_SHARED_BUILD}
  LIBZSV_INSTALL+=${LIBDIR}/${LIBZSV_SHARED}
  ifeq ($(WIN),)
    CFLAGS+= ${CFLAGS_OPENMP}
  endif
else
    CFLAGS+= ${CFLAGS_OPENMP}
endif

ZSV_OBJ_OPTS=
ifeq ($(NO_UTF8_CHECK),1)
  ZSV_OBJ_OPTS+= -DNO_UTF8_CHECK
endif

help:
	@echo "Make options:"
	@echo "  `basename ${MAKE}` build|install|uninstall|clean"
	@echo
	@echo "Optional ake variables:"
	@echo "  [CONFIGFILE=config.mk] [NO_UTF8_CHECK=1] [VERBOSE=1] [LIBDIR=${LIBDIR}] [INCLUDEDIR=${INCLUDEDIR}] [LIB_SUFFIX=]"
	@echo

build: ../include/zsv.h ${LIBZSV_BUILD}

${LIBZSV_STATIC_BUILD}: ${ZSV_OBJ}
	@mkdir -p `dirname "$@"`
	@rm -f $@
	$(AR) rcv $@ $?
	$(RANLIB) $@
	$(AR) -t $@ # check it is there
	@echo Built $@

${LIBZSV_SHARED_BUILD}: zsv.c zsv_internal.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} ${CFLAGS_VISIBILITY_HIDDEN} -DZSV_BUILD_SHARED -DZSV_VERSION=\"${VERSION}\" -I${INCLUDE_DIR} ${ZSV_OBJ_OPTS} -shared -fPIC -o $@ $< -I../app/external/sqlite3

../include/zsv.h:
ifeq ($(ZSV_EXTRAS),1)
	@sed 's/__ZSV_EXTRAS__DEFINE__/#ifndef ZSV_EXTRAS\n#define ZSV_EXTRAS\n#endif/' < ../include/zsv.h.in > $@
else
	@sed 's/__ZSV_EXTRAS__DEFINE__//' < ../include/zsv.h.in > $@
endif

install: ../include/zsv.h ${LIBZSV_INSTALL}
	@mkdir -p  $(INCLUDEDIR)
	@cp -p ../include/*.h $(INCLUDEDIR)
	@cp -pR ../include/zsv $(INCLUDEDIR)/
	@echo "include files copied to $(INCLUDEDIR)"

${LIBZSV_INSTALL}: ${LIBDIR}/% : ${BUILD_DIR}/lib/%
	@mkdir -p `dirname "$@"`
	cp -p "$<" "$@"
	@echo "$* installed to $@"

uninstall:
	@rm -rf ${INCLUDEDIR}/zsv*
	 rm  -f ${LIBDIR}/libzsv*

clean:
	rm -rf ${BUILD_DIR}/objs ${LIBZSV_BUILD}
	rm -f ../include/zsv.h

.PHONY: build install uninstall clean  ${LIBZSV_INSTALL}

${BUILD_DIR}/objs/zsv.o: zsv.c zsv_internal.c
	@mkdir -p `dirname "$@"`
	${CC} ${CFLAGS} -DZSV_VERSION=\"${VERSION}\" -I${INCLUDE_DIR} ${ZSV_OBJ_OPTS} -o $@ -c $< -I../app/external/sqlite3
